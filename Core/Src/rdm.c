/* Responder ADSW100: framing/discovery adattati da DRV_STG4/RSH7;
   nessuna dipendenza da router, controller, firmware updater o nuovi framework. */
#include "h_files.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
extern struct netif gnetif;
static uint8_t uid[6], response[257];
static bool enabled, muted, identify, save_pending;
static uint32_t changed_at, last_request_at, save_errors;
static uint32_t valid_requests, late_requests;
/* Solo RAM: E1.37-2 richiede APPLY prima di attivare DHCP/IP statico. */
static bool staged_dhcp, staged_static;
static uint8_t next_dhcp;
static uint32_t next_ip, next_mask;
/* Un solo lavoro rete in volo: il controller deve attendere il TC della risposta.
   I candidati E1.37-2 restano RAM; nessun nuovo campo persistente. */
typedef struct
{
    uint8_t fields, dhcp;
    uint32_t ip, mask, gateway, dns1, dns2;
} network_job_t;
enum
{
    NET_JOB_STATIC=1, NET_JOB_DHCP=2, NET_JOB_GATEWAY=4, NET_JOB_DNS1=8, NET_JOB_DNS2=16
};
static network_job_t network_job;
static uint32_t network_job_tc, network_job_faults;
static bool network_job_wait_tc;

/* Main: pubblica soltanto dopo il TC effettivo, mai in ISR o durante il TX ACK.
   Chiamata anche prima del prossimo GET per garantire SET seguito subito da GET. */
static void CompleteNetworkJob(void)
{
    if (network_job.fields && network_job_wait_tc && DMX512_TxFailureCount() != network_job_faults)
    {
        /* Nessun TC valido per l'ACK abortito: mantieni i candidati per il retry. */
        network_job.fields = 0;
        return;
    }
    if (!network_job.fields || (network_job_wait_tc &&
        (DMX512_TxBusy() || (int32_t)(DMX512_TxCompletionCount()-network_job_tc) < 0))) return;
    if (network_job.fields & NET_JOB_STATIC)
    {
        (void)CFG_SetValue(CFG_IP, network_job.ip);
        (void)CFG_SetValue(CFG_MASK, network_job.mask);
        staged_static = false;
    }
    if (network_job.fields & NET_JOB_DHCP)
    {
        (void)CFG_SetValue(CFG_USE_DHCP, network_job.dhcp);
        staged_dhcp = false;
    }
    if (network_job.fields & NET_JOB_GATEWAY) (void)CFG_SetValue(CFG_GATEWAY, network_job.gateway);
    if (network_job.fields & NET_JOB_DNS1) (void)CFG_SetValue(CFG_DNS1, network_job.dns1);
    if (network_job.fields & NET_JOB_DNS2) (void)CFG_SetValue(CFG_DNS2, network_job.dns2);
    network_job.fields = 0;
    save_pending = true;
    changed_at = HAL_GetTick();
}

static char device_label[33]; /* Volatile: nessuna stringa aggiunta alla EEPROM. */
static const uint16_t supported[] = {0x0050,
                                     0x0051,
                                     0x0060,
                                     0x0080,
                                     0x0081,
                                     0x0082,
                                     0x00C0,
                                     0x00E0,
                                     0x00E1,
                                     0x00F0,
                                     0x0700,
                                     0x0701,
                                     0x0702,
                                     0x0703,
                                     0x0705,
                                     0x0706,
                                     0x0709,
                                     0x070A,
                                     0x070B,
                                     0x1000,
                                     PID_DMX_PORT_MODE,
                                     PID_SOURCE_MODE,
                                     PID_ARTNET_UNIVERSE,
                                     PID_SACN_UNIVERSE,
                                     PID_SACN_TRANSPORT,
                                     PID_SACN_PRIORITY,
                                     PID_AUTO_PRIORITY,
                                     PID_DISPLAY_AUTO_OFF,
                                     PID_RELAY_OFF_TIME};
typedef struct
{
    cfg_parameter_t parameter;
    uint16_t maximum;
    uint8_t size;
    const char *label;
} custom_parameter_t;
static const custom_parameter_t custom[] = {
    {CFG_DMX_PORT_MODE, 2, 1, "DMX port mode"},          {CFG_SOURCE_MODE, 3, 1, "Input source mode"},
    {CFG_ARTNET_UNIVERSE, 32767, 2, "Art-Net universe"}, {CFG_SACN_UNIVERSE, 63999, 2, "sACN universe"},
    {CFG_SACN_TRANSPORT, 1, 1, "sACN transport"},        {CFG_SACN_PRIORITY, 200, 1, "sACN minimum priority"},
    {CFG_AUTO_PRIORITY, 5, 1, "Auto protocol order"},    {CFG_DISPLAY_AUTO_OFF, 1, 1, "Display auto off"},
    {CFG_OFF_TIME, 30, 1, "Relay off time seconds"}};
static uint16_t Word(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}
static void PutWord(uint8_t *p, uint16_t v)
{
    p[0] = v >> 8;
    p[1] = v;
}
static uint32_t Dword(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static void PutDword(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

bool RDM_PacketValid(const uint8_t *q, uint16_t length)
{
    if (!q || length < 26 || length > 257 || q[0] != 0xCC || q[1] != 1 || q[2] < 24 ||
        length != (uint16_t)q[2] + 2u || q[23] != q[2] - 24u)
        return false;
    uint16_t sum = 0;
    for (unsigned i = 0; i < q[2]; ++i)
        sum += q[i];
    return sum == Word(q + q[2]);
}

void RDM_Init(void)
{
    /* Stesso dominio EUI verificato di URC01/DRV_STG4: byte finale OUI distinto.
       La mappa conserva tutti i 24 bit seriali e l'ultimo byte OUI, senza hash. */
    static const uint32_t oui[] = {0x0004A3, 0x001EC0, 0xD88039, 0x5410EC, 0xFCC23D,
                                   0x801F12, 0x049162, 0x682719, 0xE8EB1B, 0x803428,
                                   0xFC0FE7, 0x9C956E, 0x44B7D0, 0xD8478F, 0x408432};
    uint8_t eui[6];
    enabled = muted = identify = save_pending = false;
    staged_dhcp = staged_static = false;
    valid_requests = late_requests = 0;
    memset(&network_job, 0, sizeof network_job);
    memset(uid, 0, sizeof uid);
    if (EE_Read(0xFA, eui, 6) != HAL_OK)
        return;
    uint32_t prefix = (uint32_t)eui[0] << 16 | (uint32_t)eui[1] << 8 | eui[2];
    for (unsigned i = 0; i < sizeof oui / sizeof oui[0]; ++i)
    {
        if (prefix == oui[i])
        {
            PutWord(uid, BRAND_ESTA_MANUFACTURER_ID);
            memcpy(uid + 2, eui + 2, 4);
            enabled = true;
            break;
        }
    }
    snprintf(device_label, sizeof device_label, "%s", Brand_Current()->model);
}

void RDM_FormatDiagnostics(char *out, size_t capacity)
{
    if (!out || !capacity) return;
    snprintf(out, capacity, "rdm=%u uid=%02X%02X:%02X%02X%02X%02X rx=%lu late=%lu\r\n",
             (unsigned)enabled, uid[0], uid[1], uid[2], uid[3], uid[4], uid[5],
             (unsigned long)valid_requests, (unsigned long)late_requests);
}

bool RDM_Enabled(void)
{
    return enabled;
}
bool RDM_SavePending(void)
{
    return save_pending;
}

/* Memorizzazione differita: mai I2C durante il turnaround o in ISR. */
void RDM_Task(void)
{
    CompleteNetworkJob();
    if (identify)
    {
        HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin,
                          (HAL_GetTick() / 250u) & 1u ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
    if (save_pending && !DMX512_BusBusy() && HAL_GetTick() - changed_at >= 250u &&
        HAL_GetTick() - last_request_at >= 250u)
    {
        if (EE_SaveConfig() == HAL_OK)
            save_pending = false;
        else
        {
            ++save_errors;
            changed_at = HAL_GetTick();
        }
    }
}

/* ACK/NACK normali conservano transaction, PID e sub-device del richiedente. */
static bool Reply(const uint8_t *q, const uint8_t *data, uint8_t count, uint8_t type, uint32_t end)
{
    response[0] = 0xCC;
    response[1] = 1;
    response[2] = 24u + count;
    memcpy(response + 3, q + 9, 6);
    memcpy(response + 9, uid, 6);
    response[15] = q[15];
    response[16] = type;
    response[17] = 0;
    response[18] = q[18];
    response[19] = q[19];
    response[20] = q[20] + 1u;
    response[21] = q[21];
    response[22] = q[22];
    response[23] = count;
    if (count)
        memcpy(response + 24, data, count);
    uint16_t sum = 0;
    for (unsigned i = 0; i < response[2]; ++i)
        sum += response[i];
    PutWord(response + response[2], sum);
    return DMX512_SendPacket(response, response[2] + 2u, false, end);
}

static void Discovery(uint32_t end)
{
    memset(response, 0xFE, 7);
    response[7] = 0xAA;
    uint16_t sum = 0;
    for (unsigned i = 0; i < 6; ++i)
    {
        response[8 + 2 * i] = uid[i] | 0xAA;
        response[9 + 2 * i] = uid[i] | 0x55;
        sum += response[8 + 2 * i] + response[9 + 2 * i];
    }
    response[20] = (sum >> 8) | 0xAA;
    response[21] = (sum >> 8) | 0x55;
    response[22] = sum | 0xAA;
    response[23] = sum | 0x55;
    (void)DMX512_SendPacket(response, 24, true, end);
}

void RDM_Receive(const uint8_t *q, uint16_t length, uint32_t end)
{
    CompleteNetworkJob();
    if (!RDM_PacketValid(q, length))
        return;
    ++valid_requests;
    last_request_at = HAL_GetTick();
    DMX512_ObserveController(); /* Anche risposte altrui impediscono due sorgenti sulla linea. */
    if (!enabled || !DMX512_CanReply()) return;
    if (DMX512_Cycles() - end > DMX_RDM_SERVICE_US * (SystemCoreClock / 1000000u))
    {
        ++late_requests;
        return;
    }
    uint16_t manufacturer = Word(q + 3), pid = Word(q + 21), sub = Word(q + 18);
    bool broadcast = (manufacturer == 0xFFFF || manufacturer == BRAND_ESTA_MANUFACTURER_ID) && q[5] == 0xFF &&
                     q[6] == 0xFF && q[7] == 0xFF && q[8] == 0xFF;
    if (!broadcast && memcmp(q + 3, uid, 6))
        return;
    uint8_t cc = q[20], pdl = q[23];
    if (cc == 0x10)
    {
        if (sub)
            return;
        if (pid == 1 && pdl == 12 && !muted && memcmp(uid, q + 24, 6) >= 0 && memcmp(uid, q + 30, 6) <= 0)
            Discovery(end);
        else if ((pid == 2 || pid == 3) && !pdl)
        {
            muted = pid == 2;
            uint8_t control[2] = {0, 0};
            if (!broadcast)
                Reply(q, control, 2, 0, end);
        }
        return;
    }
    if (cc != 0x20 && cc != 0x30)
        return;
    bool set = cc == 0x30;
    if (broadcast && !set)
        return;
    uint8_t data[231] = {0}, n = 0;
    uint16_t nack = 0xFFFF;
    bool changed = false;
    network_job_t job = {0};
    if (sub && !(set && sub == 0xFFFF))
        nack = 9;
    else if (pid >= PID_DMX_PORT_MODE && pid <= PID_RELAY_OFF_TIME)
    {
        const custom_parameter_t *c = &custom[pid - PID_DMX_PORT_MODE];
        if (pdl != (set ? c->size : 0))
            nack = 1;
        else if (set)
        {
            uint32_t value = c->size == 2 ? Word(q + 24) : q[24];
            if (!CFG_SetValue(c->parameter, value))
                nack = 6;
            else
                changed = true;
        }
        else
        {
            uint32_t value;
            CFG_GetValue(c->parameter, &value);
            if (c->size == 2)
                PutWord(data, value);
            else
                data[0] = value;
            n = c->size;
        }
    }
    else if (pid == 0x0051 && !set)
    {
        if (pdl != 2)
            nack = 1;
        else
        {
            uint16_t requested = Word(q + 24);
            if (requested < PID_DMX_PORT_MODE || requested > PID_RELAY_OFF_TIME)
                nack = 0;
            else
            {
                const custom_parameter_t *c = &custom[requested - PID_DMX_PORT_MODE];
                PutWord(data, requested);
                data[2] = c->size;
                data[3] = c->size == 2 ? 5 : 3;
                data[4] = 3;
                if (requested == PID_RELAY_OFF_TIME)
                    data[6] = 0x15; /* UNITS_SECOND */
                PutDword(data + 8, requested == PID_SACN_UNIVERSE ? 1 : 0);
                PutDword(data + 12, c->maximum);
                PutDword(data + 16, requested == PID_SACN_UNIVERSE      ? 1
                                    : requested == PID_RELAY_OFF_TIME   ? 30
                                    : requested == PID_DISPLAY_AUTO_OFF ? 1
                                    : requested == PID_SOURCE_MODE      ? CFG_DEFAULT_DMX_MODE
                                                                        : 0);
                n = (uint8_t)(20 + strlen(c->label));
                memcpy(data + 20, c->label, n - 20);
            }
        }
    }
    else if (pid == 0x00F0 || pid == 0x1000 || pid == 0x00E0)
    {
        unsigned size = pid == 0x00F0 ? 2 : 1;
        if (pdl != (set ? size : 0))
            nack = 1;
        else if (set)
        {
            if (pid == 0x00F0)
            {
                if (!CFG_SetValue(CFG_DMX_ADDRESS, Word(q + 24)))
                    nack = 6;
                else
                    changed = true;
            }
            else if (pid == 0x1000 && q[24] <= 1)
            {
                identify = q[24] != 0;
                if (!identify)
                    LCD_Backlight_Wake();
            }
            else if (pid != 0x00E0 || q[24] != 1)
                nack = 6;
        }
        else
        {
            if (pid == 0x00F0)
            {
                PutWord(data, dmx_address);
                n = 2;
            }
            else if (pid == 0x1000)
            {
                data[0] = identify;
                n = 1;
            }
            else
            {
                data[0] = 1;
                data[1] = 1;
                n = 2;
            }
        }
    }
    else if (pid == 0x0082)
    {
        if (set)
        {
            if (pdl > 32)
                nack = 1;
            else
            {
                for (unsigned i = 0; i < pdl; ++i)
                    if (q[24 + i] < 32 || q[24 + i] > 126)
                        nack = 6;
                if (nack == 0xFFFF)
                {
                    memcpy(device_label, q + 24, pdl);
                    device_label[pdl] = 0;
                }
            }
        }
        else if (pdl)
            nack = 1;
        else
        {
            n = strlen(device_label);
            memcpy(data, device_label, n);
        }
    }
    else if (pid == 0x0703 || pid == 0x0706)
    {
        unsigned size = pid == 0x0703 ? 5 : 9;
        if (pdl != (set ? size : 4))
            nack = 1;
        else if (Dword(q + 24) != 1)
            nack = 6;
        else if (set)
        {
            if (pid == 0x0703)
            {
                if (!CFG_ValidateValue(CFG_USE_DHCP, q[28]))
                    nack = 6;
                else
                {
                    next_dhcp = q[28];
                    staged_dhcp = true;
                }
            }
            else if (q[32] > 32)
                nack = 6;
            else
            {
                uint32_t raw;
                memcpy(&raw, q + 28, 4);
                if (!CFG_ValidateValue(CFG_IP, raw))
                    nack = 6;
                else
                {
                    next_ip = raw;
                    next_mask = lwip_htonl(q[32] ? UINT32_MAX << (32 - q[32]) : 0);
                    staged_static = true;
                }
            }
        }
        else
        {
            PutDword(data, 1);
            n = size;
            if (pid == 0x0703)
                data[4] = staged_dhcp ? next_dhcp : use_dhcp;
            else
            {
                uint32_t configured_ip = staged_static ? next_ip : use_dhcp ? 0 : ip_address;
                memcpy(data + 4, &configured_ip, 4);
                uint32_t m = lwip_ntohl(staged_static ? next_mask : use_dhcp ? 0 : net_mask);
                while (m & 0x80000000u)
                {
                    ++data[8];
                    m <<= 1;
                }
            }
        }
    }
    else if (pid == 0x070A || pid == 0x070B)
    {
        unsigned size = pid == 0x070A ? 8 : 5;
        if (pdl != (set ? size : pid == 0x070B ? 1 : 0))
            nack = 1;
        else if (pid == 0x070B && q[24] > 1)
            nack = 6;
        else if (pid == 0x070A && set && Dword(q + 24) != 0)
        {
            /* SET route con gateway richiede NO_DEFAULT_ROUTE (ID=0).
               Ethernet non offre il routing point-to-point (ID=1, gateway=0). */
            nack = Dword(q+24) == 1 && Dword(q+28) == 0 ? 0x000B : 6;
        }
        else if (set)
        {
            uint32_t raw;
            memcpy(&raw, q + (pid == 0x070A ? 28 : 25), 4);
            if (!CFG_ValidateValue(pid == 0x070A ? CFG_GATEWAY : q[24] ? CFG_DNS2 : CFG_DNS1, raw))
                nack = 6;
            else
            {
                job.fields = pid == 0x070A ? NET_JOB_GATEWAY : q[24] ? NET_JOB_DNS2 : NET_JOB_DNS1;
                job.gateway = job.dns1 = job.dns2 = raw;
            }
        }
        else
        {
            n = size;
            if (pid == 0x070A)
            {
                PutDword(data, gateway ? 1 : 0);
                memcpy(data + 4, &gateway, 4);
            }
            else
            {
                data[0] = q[24];
                uint32_t raw = q[24] ? dns2 : dns1;
                memcpy(data + 1, &raw, 4);
            }
        }
    }
    else if (pid == 0x0709 && set)
    {
        if (pdl != 4)
            nack = 1;
        else if (Dword(q + 24) != 1)
            nack = 6;
        else if (staged_dhcp && next_dhcp &&
                 (staged_static ? next_ip != 0 : !use_dhcp && ip_address != 0))
        {
            /* Il legacy conserva un solo selettore: abilitazione DHCP + statico
               attivo non sono rappresentabili insieme. Prima azzerare STATIC_ADDRESS. */
            nack = 0x000B; /* NR_ACTION_NOT_SUPPORTED, candidati conservati. */
        }
        else
        {
            if (staged_static)
            {
                job.fields |= NET_JOB_STATIC;
                job.ip = next_ip;
                job.mask = next_mask;
                if (next_ip)
                {
                    /* E1.37-2: l'indirizzo statico non nullo precede DHCP. */
                    job.fields |= NET_JOB_DHCP;
                    job.dhcp = 0;
                }
            }
            if (staged_dhcp)
            {
                job.fields |= NET_JOB_DHCP;
                job.dhcp = next_dhcp;
            }
            /* Candidati cancellati solo dopo il TC valido e la pubblicazione. */
        }
    }
    else if (set)
    {
        bool known = false;
        for (unsigned i = 0; i < sizeof supported / sizeof supported[0]; ++i)
            if (supported[i] == pid)
                known = true;
        nack = known ? 5 : 0;
    }
    else if (pid == 0x00E1)
    {
        if (pdl != 1)
            nack = 1;
        else if (q[24] != 1)
            nack = 6;
        else
        {
            data[0] = 1;
            PutWord(data + 1, 12);
            memcpy(data + 3, "12 relays", 9);
            n = 12;
        }
    }
    else if (pid == 0x0701 || pid == 0x0702 || pid == 0x0705)
    {
        if (pdl != 4)
            nack = 1;
        else if (Dword(q + 24) != 1)
            nack = 6;
        else
        {
            PutDword(data, 1);
            if (pid == 0x0701)
            {
                memcpy(data + 4, "Ethernet", 8);
                n = 12;
            }
            else if (pid == 0x0702)
            {
                memcpy(data + 4, gnetif.hwaddr, 6);
                n = 10;
            }
            else
            {
                memcpy(data + 4, &netif_ip4_addr(&gnetif)->addr, 4);
                uint32_t m = lwip_ntohl(netif_ip4_netmask(&gnetif)->addr);
                while (m & 0x80000000u)
                {
                    ++data[8];
                    m <<= 1;
                }
                data[9] = dhcp_supplied_address(&gnetif) ? 1 : 0;
                n = 10;
            }
        }
    }
    else if (pid == 0x0709)
        nack = 5; /* PID noto, solo SET. */
    else if (pdl)
        nack = 1;
    else if (pid == 0x0050)
    {
        for (unsigned i = 0; i < sizeof supported / sizeof supported[0]; ++i)
            PutWord(data + 2 * i, supported[i]);
        n = sizeof supported;
    }
    else if (pid == 0x0060)
    {
        PutWord(data, 0x0100);
        PutWord(data + 2, ADSW_RDM_MODEL_ID);
        PutWord(data + 4, 0x0601);
        PutDword(data + 6, (FW_VERSION_MAJOR << 16) | (FW_VERSION_MINOR << 8) | FW_VERSION_PATCH);
        PutWord(data + 10, 12);
        data[12] = 1;
        data[13] = 1;
        PutWord(data + 14, dmx_address);
        n = 19;
    }
    else if (pid == 0x0080 || pid == 0x0081 || pid == 0x00C0)
    {
        const char *text = pid == 0x0080   ? Brand_Current()->model
                           : pid == 0x0081 ? Brand_Current()->manufacturer
                                           : FW_VERSION_STR;
        n = strlen(text);
        memcpy(data, text, n);
    }
    else if (pid == 0x0700)
    {
        PutDword(data, 1);
        PutWord(data + 4, 1); /* ARP hardware type Ethernet, non ifType SNMP. */
        n = 6;
    }
    else
        nack = 0;
    if (changed)
    {
        save_pending = true;
        changed_at = HAL_GetTick();
    }
    uint32_t completion = DMX512_TxCompletionCount() + 1u;
    bool accepted = broadcast;
    if (!broadcast)
    {
        if (nack != 0xFFFF)
        {
            PutWord(data, nack);
            (void)Reply(q, data, 2, 2, end);
        }
        else accepted = Reply(q, data, n, 0, end);
    }
    if (accepted && nack == 0xFFFF && job.fields)
    {
        network_job = job;
        network_job_tc = completion;
        network_job_faults = DMX512_TxFailureCount();
        network_job_wait_tc = !broadcast;
    }
}
