/* SDCP2 ADSW100. Driver STOP/READ e rilascio lock adattati da DRV_STG4
   0578567:i2c_bus.c. PID/schema generati dalla fondazione approvata. */
#include "h_files.h"
#include "nfc_catalog.h"
#include <string.h>
_Static_assert(NFC_CATALOG_MANUFACTURER == BRAND_ESTA_MANUFACTURER_ID, "Manufacturer catalogo");
_Static_assert(NFC_CATALOG_MODEL == ADSW_RDM_MODEL_ID, "Modello catalogo");
/* Indirizzi a7bit:02 osservato su ADSW,55 nel riferimento DRV0578567.
   57/59 sono solo alternative storiche locali, non indirizzi confermati. */
/* La build diagnostica conserva tutto il driver, ma non avvia operazioni NFC.
   Il flag è volatile solo in RAM; nessun parametro EEPROM aggiuntivo. */
#ifndef NFC_START_ENABLED
#define NFC_START_ENABLED 1
#endif
static bool nfc_enabled = NFC_START_ENABLED != 0;
static uint32_t nfc_boot_stage, nfc_boot_reset, nfc_boot_isr, nfc_boot_cr1, nfc_boot_lines;
static uint8_t s_nfc_addr = 0x02u;
static uint8_t nfc_confirmed_addr, nfc_nack_streak;
static uint16_t nfc_hal_address;
static volatile uint32_t nfc_ev_irqs, nfc_er_irqs;
static volatile uint32_t nfc_tx_callbacks, nfc_rx_callbacks, nfc_error_callbacks;

/* Stesso riconoscimento ChipHeader del riferimento DRV0578567: nessun BCC
   nella vista I2C NT3H, UID0/SAK/ATQA verificati prima di confermare l'indirizzo. */
static bool ChipHeader(const uint8_t *data)
{
    return data[0] == 4U && data[7] == 0U && data[8] == 0x44U && data[9] == 0U;
}
static uint16_t NfcHalAddress(uint8_t address7)
{
    return (uint16_t)address7 << 1;
}
static void NfcNextAddress(void)
{
    s_nfc_addr = s_nfc_addr == 2 ? 0x55 : s_nfc_addr == 0x55 ? 0x57 : s_nfc_addr == 0x57 ? 0x59 : 2;
}

typedef enum
{
    BUS_NFC = 1
} BusOwner;
typedef struct
{
    uint32_t hal_error, timeouts, nacks, memory_ops, session_ops, releases;
    uint16_t remaining;
    uint8_t hal_status, phase, reg;
    bool timeout;
    uint16_t hal_address;
    uint32_t cr1, cr2, isr, state, ev, er, enabled, pending, primask, basepri, lines;
} BusDiagnostic;
/* Stato condiviso ISR/main: visibilità anche nelle build ottimizzate. */
static volatile BusOwner active;
static volatile int result;
static uint32_t started;
static uint8_t nfc_prefix[2];
static uint8_t nfc_release[4] = {0xFE, 0x06, 0x40, 0x00};
static volatile uint8_t nfc_bus_phase;
static uint8_t *nfc_destination;
static uint16_t nfc_bus_length;
static volatile uint32_t stopped_at;
static uint8_t active_reg;
static BusDiagnostic nfc_diagnostic;
/* NXP §§9.2/9.7: circa4ms di programmazione dopo STOP;10tick danno margine anche al bordo tick. */
#define NFC_PROGRAM_TICKS 10U

/* Conserva il contesto prima che HAL o un altro proprietario riutilizzino il controller. */
static void Failure(HAL_StatusTypeDef rc, bool timeout)
{
    if (active == BUS_NFC)
    {
        /* Fotografia prima di DeInit/Init: altrimenti le prove del guasto spariscono. */
        nfc_diagnostic.cr1 = hi2c1.Instance->CR1;
        nfc_diagnostic.cr2 = hi2c1.Instance->CR2;
        nfc_diagnostic.isr = hi2c1.Instance->ISR;
        nfc_diagnostic.state = HAL_I2C_GetState(&hi2c1);
        nfc_diagnostic.hal_address = nfc_hal_address;
        nfc_diagnostic.ev = nfc_ev_irqs;
        nfc_diagnostic.er = nfc_er_irqs;
        nfc_diagnostic.enabled = NVIC_GetEnableIRQ(I2C1_EV_IRQn) | (NVIC_GetEnableIRQ(I2C1_ER_IRQn) << 1);
        nfc_diagnostic.pending = NVIC_GetPendingIRQ(I2C1_EV_IRQn) | (NVIC_GetPendingIRQ(I2C1_ER_IRQn) << 1);
        nfc_diagnostic.primask = __get_PRIMASK();
        nfc_diagnostic.basepri = __get_BASEPRI();
        nfc_diagnostic.lines = (GPIOB->IDR >> 6) & 3u;
        nfc_diagnostic.hal_error = hi2c1.ErrorCode;
        nfc_diagnostic.remaining = hi2c1.XferCount;
        nfc_diagnostic.hal_status = rc;
        nfc_diagnostic.phase = nfc_bus_phase;
        nfc_diagnostic.reg = active_reg;
        nfc_diagnostic.timeout = timeout;
        nfc_diagnostic.timeouts += timeout ? 1U : 0U;
        nfc_diagnostic.nacks += (rc == HAL_ERROR && (hi2c1.ErrorCode & HAL_I2C_ERROR_AF)) ? 1U : 0U;
    }
    result = -1;
}

/* Copia atomica rispetto al main cooperativo; i contatori non vengono azzerati dai sensori. */
static void BUS_NfcDiagnostics(BusDiagnostic *out)
{
    *out = nfc_diagnostic;
}


/* Stato del servizio a boot; non abilita IRQ né modifica la periferica. */
static void BUS_Init(void)
{
    active = 0;
    ee_nfc_bus_reserved = false;
    result = 0;
    nfc_diagnostic = (BusDiagnostic){0};
}

/* Riserva il bus; nessuna coda e nessuna attesa. */
static bool Start(BusOwner owner, uint8_t address, uint8_t reg, uint8_t *data, uint16_t length, bool read)
{
    if (active != 0 || length == 0U)
    {
        return false;
    }
    nfc_hal_address = NfcHalAddress(address);
    active = owner;
    ee_nfc_bus_reserved = true;
    result = 0;
    started = HAL_GetTick();
    active_reg = reg;
    nfc_bus_phase = 4;
    if (owner == BUS_NFC)
    {
        nfc_diagnostic.memory_ops++;
    }
    HAL_StatusTypeDef rc;
    if (read && owner == BUS_NFC)
    {
        /* NXP prescrive STOP dopo MEMA/REGA, quindi lettura atomica mantenendo ownership. */
        nfc_prefix[0] = reg;
        nfc_destination = data;
        nfc_bus_length = length;
        nfc_bus_phase = 1;
        rc = HAL_I2C_Master_Transmit_IT(&hi2c1, nfc_hal_address, nfc_prefix, 1);
    }
    else if (read)
    {
        rc = HAL_I2C_Mem_Read_IT(&hi2c1, nfc_hal_address, reg, I2C_MEMADD_SIZE_8BIT, data, length);
    }
    else
    {
        rc = HAL_I2C_Mem_Write_IT(&hi2c1, nfc_hal_address, reg, I2C_MEMADD_SIZE_8BIT, data, length);
    }
    if (rc != HAL_OK)
    {
        /* Anche un bus già bloccato deve produrre un errore consumabile,
           senza trattenere indefinitamente il caricamento iniziale. */
        Failure(rc, false);
    }
    return true;
}

/* Avvia una lettura con buffer del chiamante. */
static bool BUS_Read(BusOwner owner, uint8_t address, uint8_t reg, uint8_t *data, uint16_t length)
{
    return Start(owner, address, reg, data, length, true);
}

/* Avvia una scrittura con buffer del chiamante. */
static bool BUS_Write(BusOwner owner, uint8_t address, uint8_t reg, uint8_t *data, uint16_t length)
{
    return Start(owner, address, reg, data, length, false);
}

/* Rilascia la proprietà solo dopo il consumo del risultato. */
static int BUS_Result(BusOwner owner)
{
    if (active != owner || result == 0 || (owner == BUS_NFC && result > 0 && nfc_bus_phase != 5U))
    {
        return 0;
    }
    int value = result;
    active = 0;
    ee_nfc_bus_reserved = false;
    nfc_bus_phase = 0;
    return value;
}

/* Ripristina il controller dopo timeout; non invia clock di recupero sui pin. */
static void BUS_Task(void)
{
    if (active == BUS_NFC && result == 0 && nfc_bus_phase == 6U &&
        (uint32_t)(HAL_GetTick() - stopped_at) >= NFC_PROGRAM_TICKS)
    {
        /* Nessun comando al tag, incluso FE064000, prima della fine di questa finestra. */
        nfc_bus_phase = 4;
        result = 1;
    }
    if (active == BUS_NFC && result > 0 && nfc_bus_phase != 5U)
    {
        /* Non inviare comandi di rilascio a un device non ancora riconosciuto. */
        if (nfc_bus_phase == 3U && active_reg == 0 && nfc_bus_length == 16U &&
            !nfc_confirmed_addr && !ChipHeader(nfc_destination))
        {
            nfc_bus_phase = 5;
            return;
        }
        /* STOP non libera l'arbiter NT3H: NS_REG, MASK40, DATA00 modifica solo I2C_LOCKED. */
        result = 0;
        active_reg = 6;
        nfc_bus_phase = 5;
        started = HAL_GetTick();
        HAL_StatusTypeDef rc = HAL_I2C_Master_Transmit_IT(&hi2c1, nfc_hal_address, nfc_release, sizeof(nfc_release));
        if (rc != HAL_OK)
        {
            Failure(rc, false);
        }
    }
    if (active == BUS_NFC && result == 0 && nfc_bus_phase == 2U &&
        (uint32_t)(HAL_GetTick() - stopped_at) >= 2U)
    {
        /* Due tick garantiscono almeno un millisecondo e coprono i50us richiesti anche con clock stretching disabilitato. */
        nfc_bus_phase = 3;
        /* H7: il timeout appartiene al comando RX, non al tempo del main dopo STOP. */
        started = HAL_GetTick();
        HAL_StatusTypeDef rc = HAL_I2C_Master_Receive_IT(&hi2c1, nfc_hal_address, nfc_destination, nfc_bus_length);
        if (rc != HAL_OK)
        {
            Failure(rc, false);
        }
    }
    if (active != 0 && result == 0 && nfc_bus_phase != 2U && nfc_bus_phase != 6U &&
        (uint32_t)(HAL_GetTick() - started) >= 30U)
    {
        Failure(HAL_TIMEOUT, true);
        HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
        HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
        HAL_I2C_DeInit(&hi2c1);
        HAL_I2C_Init(&hi2c1);
        HAL_NVIC_ClearPendingIRQ(I2C1_EV_IRQn);
        HAL_NVIC_ClearPendingIRQ(I2C1_ER_IRQn);
        HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
        HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
    }
}

/* Le callback segnalano il completamento, senza avviare altre operazioni. */
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *h)
{
    if (h == &hi2c1 && active != 0 && result == 0 && nfc_bus_phase == 4U)
    {
        result = 1;
    }
}

/* Segnala la fine della scrittura. */
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *h)
{
    if (h == &hi2c1 && active == BUS_NFC && result == 0 && nfc_bus_phase == 4U)
    {
        /* La callback segue STOP: la HAL ha finito il TX, non la programmazione EEPROM del tag. */
        stopped_at = HAL_GetTick();
        nfc_bus_phase = 6;
    }
    else
    {
        HAL_I2C_MemRxCpltCallback(h);
    }
}

/* Segnala NACK, errore di linea o arbitraggio. */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h)
{
    if (h == &hi2c1 && active != 0 && result == 0)
    {
        ++nfc_error_callbacks;
        Failure(HAL_ERROR, false);
    }
}


/* Sessione NT3H: MEMA=FE, REGA=0..7; nessun altro servizio può inserirsi prima del READ. */
static bool BUS_NfcSession(uint8_t reg, uint8_t *data)
{
    if (active != 0 || reg > 7U)
    {
        return false;
    }
    nfc_hal_address = NfcHalAddress(s_nfc_addr);
    active = BUS_NFC;
    ee_nfc_bus_reserved = true;
    nfc_diagnostic.session_ops++;
    result = 0;
    started = HAL_GetTick();
    active_reg = reg;
    nfc_prefix[0] = 0xFE;
    nfc_prefix[1] = reg;
    nfc_destination = data;
    nfc_bus_length = 1;
    nfc_bus_phase = 1;
    HAL_StatusTypeDef rc = HAL_I2C_Master_Transmit_IT(&hi2c1, nfc_hal_address, nfc_prefix, 2);
    if (rc != HAL_OK)
    {
        Failure(rc, false);
    }
    return true;
}

/* Il completamento TX registra lo STOP; BUS_Task avvia RX dopo la pausa senza cedere ownership. */
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *h)
{
    if (h == &hi2c1 && active == BUS_NFC && result == 0 && nfc_bus_phase == 1U)
    {
        ++nfc_tx_callbacks;
        nfc_bus_phase = 2;
        stopped_at = HAL_GetTick();
    }
    else if (h == &hi2c1 && active == BUS_NFC && result == 0 && nfc_bus_phase == 5U)
    {
        ++nfc_tx_callbacks;
        nfc_diagnostic.releases++;
        result = 1;
    }
}

/* Conclude una lettura NT3H integrale, lasciando il risultato al proprietario main. */
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *h)
{
    if (h == &hi2c1 && active == BUS_NFC && result == 0 && nfc_bus_phase == 3U)
    {
        ++nfc_rx_callbacks;
        result = 1;
    }
}

void NFC_I2C_EventIRQ(void)
{
    ++nfc_ev_irqs;
    HAL_I2C_EV_IRQHandler(&hi2c1);
}
void NFC_I2C_ErrorIRQ(void)
{
    ++nfc_er_irqs;
    HAL_I2C_ER_IRQHandler(&hi2c1);
}

#define NFC_USER_BYTES 888u
#define NFC_SCAN_BYTES 896u
#define NFC_IMAGE_BYTES 240u
/* Il tag può contenere uno SDCP più lungo per sezioni opzionali opache. */
static uint8_t nfc_memory[NFC_SCAN_BYTES], nfc_previous[NFC_SCAN_BYTES];
static uint8_t nfc_image[NFC_SCAN_BYTES], nfc_identity[16];
static uint8_t nfc_header[16], nfc_access[16], nfc_control[16], nfc_config[16];
static uint8_t nfc_mode, nfc_rf, nfc_eui[6];
static uint8_t nfc_cc_write[16], nfc_cc_read[16];
static bool nfc_cc_provision;
static unsigned nfc_factory_used;
static uint8_t nfc_extensions[NFC_USER_BYTES];
static unsigned nfc_extension_length, nfc_extension_cursor;
static uint8_t nfc_blocks;
static uint32_t nfc_reads, nfc_applied, nfc_replays, nfc_errors;
static const char *nfc_status = "NOT_STARTED";
static uint32_t nfc_at, nfc_io_at;
static uint16_t nfc_capacity, nfc_length;
static uint8_t nfc_block, nfc_verify_block;
static bool nfc_pending, nfc_second, nfc_identity_ok, nfc_writing;
static ee_nfc_meta_t nfc_meta;

typedef enum
{
    N_HEADER, N_ACCESS, N_CONTROL, N_CONFIG, N_MODE, N_RF, N_SCAN,
    N_PROCESS, N_WRITE_RF, N_INVALIDATE, N_BODY, N_FINISH, N_VERIFY, N_WAIT,
    N_CAPACITY, N_CC_GUARD, N_CC_CHECK, N_CC_RF, N_CC_WRITE, N_CC_READBACK, N_CC_VERIFY
} nfc_phase_t;
static nfc_phase_t nfc_phase, nfc_after;
static const uint8_t nfc_invalid[16] = {3, 0, 0xFE};

static uint16_t NfcU16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}
static uint32_t NfcU32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static void NfcPut16(uint8_t *p, uint16_t value)
{
    p[0] = value >> 8;
    p[1] = value;
}
static void NfcPut32(uint8_t *p, uint32_t value)
{
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}
static uint32_t NfcNext(uint32_t value)
{
    return value == UINT32_MAX ? 1u : value + 1u;
}
static uint32_t NfcCrc(const uint8_t *p, unsigned length)
{
    uint32_t crc = UINT32_MAX;
    for (unsigned i = 0; i < length; ++i)
    {
        crc ^= p[i];
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return crc ^ UINT32_MAX;
}

/* Legge i byte legacy soltanto se già commessi. IPv4 resta raw lwIP in RAM. */
static bool NfcSaved(uint32_t values[CFG_PARAMETER_COUNT])
{
    uint8_t b[40];
    if (EE_ReadConfigSnapshot(b) != HAL_OK) return false;
    values[CFG_DMX_ADDRESS] = NfcU16(b + 24);
    values[CFG_DMX_PORT_MODE] = b[39] & 3u;
    values[CFG_SOURCE_MODE] = b[26];
    values[CFG_ARTNET_UNIVERSE] = NfcU16(b + 28);
    values[CFG_SACN_UNIVERSE] = NfcU16(b + 30);
    values[CFG_SACN_TRANSPORT] = b[32];
    values[CFG_SACN_PRIORITY] = b[33];
    values[CFG_DISPLAY_AUTO_OFF] = b[34] == 0;
    values[CFG_OFF_TIME] = b[35];
    values[CFG_AUTO_PRIORITY] = b[36];
    values[CFG_USE_DHCP] = b[3];
    values[CFG_IP] = NfcU32(b + 4);
    values[CFG_MASK] = NfcU32(b + 8);
    values[CFG_GATEWAY] = NfcU32(b + 12);
    values[CFG_DNS1] = NfcU32(b + 16);
    values[CFG_DNS2] = NfcU32(b + 20);
    for (unsigned i = 0; i < CFG_PARAMETER_COUNT; ++i)
    {
        if (!CFG_ValidateValue((cfg_parameter_t)i, values[i])) return false;
    }
    return true;
}
static bool NfcRuntimeEqual(const uint32_t values[CFG_PARAMETER_COUNT])
{
    uint8_t stored_brand, stored_code;
    if (EE_ReadIdentity(&stored_brand, &stored_code) != HAL_OK ||
        stored_brand != brand || stored_code != menu_code_required) return false;
    for (unsigned i = 0; i < CFG_PARAMETER_COUNT; ++i)
    {
        uint32_t actual;
        if (!CFG_GetValue((cfg_parameter_t)i, &actual) || values[i] != actual) return false;
    }
    return true;
}

/* PID e selettori sono quelli del catalogo; nessuna seconda configurazione. */
static int NfcParameter(const nfc_field_t *f)
{
    switch (f->pid)
    {
        case 0x00F0: return CFG_DMX_ADDRESS;
        case 0x0703: return CFG_USE_DHCP;
        case 0x0706: return CFG_IP;
        case 0x070A: return CFG_GATEWAY;
        case 0x070B: return f->selector ? CFG_DNS2 : CFG_DNS1;
        case PID_DMX_PORT_MODE: return CFG_DMX_PORT_MODE;
        case PID_SOURCE_MODE: return CFG_SOURCE_MODE;
        case PID_ARTNET_UNIVERSE: return CFG_ARTNET_UNIVERSE;
        case PID_SACN_UNIVERSE: return CFG_SACN_UNIVERSE;
        case PID_SACN_TRANSPORT: return CFG_SACN_TRANSPORT;
        case PID_SACN_PRIORITY: return CFG_SACN_PRIORITY;
        case PID_AUTO_PRIORITY: return CFG_AUTO_PRIORITY;
        case PID_DISPLAY_AUTO_OFF: return CFG_DISPLAY_AUTO_OFF;
        case PID_RELAY_OFF_TIME: return CFG_OFF_TIME;
        default: return -1;
    }
}
static bool NfcValue(const nfc_field_t *f, uint8_t *out, uint32_t *values, bool set)
{
    int parameter = NfcParameter(f);
    if (parameter < 0) return false;
    if (f->pid == 0x0706 || f->pid == 0x070A || f->pid == 0x070B)
    {
        unsigned offset = f->pid == 0x070A ? 4u : 0u;
        if (f->pid == 0x070A)
        {
            if (set && NfcU32(out) != 0) return false;
            if (!set) memset(out, 0, 4);
        }
        if (set) memcpy(&values[parameter], out + offset, 4);
        else memcpy(out + offset, &values[parameter], 4);
        if (f->pid == 0x0706)
        {
            if (set)
            {
                if (out[4] > 32) return false;
                uint32_t mask = out[4] ? UINT32_MAX << (32u - out[4]) : 0;
                values[CFG_MASK] = lwip_htonl(mask);
            }
            else
            {
                uint32_t mask = lwip_ntohl(values[CFG_MASK]);
                out[4] = 0;
                while (mask & 0x80000000u)
                {
                    out[4]++;
                    mask <<= 1;
                }
                if (mask) return false;
            }
        }
    }
    else
    {
        if (set) values[parameter] = f->value_size == 2 ? NfcU16(out) : out[0];
        else if (f->value_size == 2) NfcPut16(out, values[parameter]);
        else out[0] = values[parameter];
    }
    return !set || CFG_ValidateValue((cfg_parameter_t)parameter, values[parameter]);
}

/* Conserva le sezioni opache non critiche nell'ordine originale. */
static bool NfcAppendExtensions(uint8_t *d, unsigned *used, unsigned before)
{
    while (nfc_extension_cursor < nfc_extension_length && nfc_extensions[nfc_extension_cursor] < before)
    {
        unsigned size = 4u + NfcU16(nfc_extensions + nfc_extension_cursor + 2);
        if (*used + size > 816u) return false;
        memcpy(d + *used, nfc_extensions + nfc_extension_cursor, size);
        *used += size;
        nfc_extension_cursor += size;
    }
    return true;
}

/* SDCP snapshot base 197 byte; NDEF SR233, padding a240 solo I2C.
   Le sezioni opache possono richiedere NDEF/TLV lunghi.
   I dati provengono dallo snapshot EEPROM verificato, mai da editing RAM. */
static bool NfcBuild(uint32_t values[CFG_PARAMETER_COUNT], const ee_nfc_meta_t *meta)
{
    if (nfc_extension_length > 816u - 197u) return false;
    memset(nfc_image, 0, sizeof nfc_image);
    uint8_t *d = nfc_image + 40;
    nfc_extension_cursor = 0;
    memcpy(d, "SD\x20\0", 4);
    d[6] = 1; d[7] = 1; NfcPut16(d + 8, 16);
    memcpy(d + 10, nfc_identity, 16);
    unsigned used = 26;
    d[used] = 2; d[used + 1] = 1;
    unsigned value_header = used;
    used += 4;
    for (unsigned i = 0; i < NFC_FIELD_COUNT; ++i)
    {
        const nfc_field_t *f = &nfc_fields[i];
        NfcPut16(d + used, f->pid);
        d[used + 2] = f->selector_size;
        used += 3;
        if (f->selector_size == 4) NfcPut32(d + used, f->selector);
        else if (f->selector_size == 1) d[used] = f->selector;
        used += f->selector_size;
        d[used++] = f->value_size;
        if (!NfcValue(f, d + used, values, false)) return false;
        used += f->value_size;
    }
    NfcPut16(d + value_header + 2, used - value_header - 4);
    if (!NfcAppendExtensions(d, &used, 7)) return false;
    d[used] = 7; d[used + 1] = 1; NfcPut16(d + used + 2, 36);
    NfcPut32(d + used + 4, NFC_SCHEMA_REVISION);
    memcpy(d + used + 8, nfc_schema_hash, 32); used += 40;
    if (!NfcAppendExtensions(d, &used, 0x80)) return false;
    d[used] = 0x80; d[used + 1] = 1; NfcPut16(d + used + 2, 14);
    d[used + 4] = 1; d[used + 5] = 0;
    NfcPut32(d + used + 6, meta->revision);
    NfcPut32(d + used + 10, meta->request);
    NfcPut32(d + used + 14, meta->request); used += 18;
    if (!NfcAppendExtensions(d, &used, 256) || used + 4u > 816u) return false;
    NfcPut16(d + 4, used + 4);
    NfcPut32(d + used, NfcCrc(d, used)); used += 4;
    unsigned header = used <= 255u ? 3u : 6u;
    unsigned record = header + 30u + used;
    unsigned tlv = record < 255u ? 2u : 4u;
    unsigned prefix = tlv + header + 30u;
    nfc_length = prefix + used + 1u;
    nfc_blocks = (nfc_length + 15u) / 16u;
    /* Il blocco56 contiene anche registri fuori User: mai scriverlo integralmente. */
    if (nfc_blocks > 55u || nfc_blocks * 16u > nfc_capacity) return false;
    memmove(nfc_image + prefix, d, used);
    memset(nfc_image, 0, prefix);
    nfc_image[0] = 3;
    if (tlv == 2) nfc_image[1] = record;
    else { nfc_image[1] = 255; NfcPut16(nfc_image + 2, record); }
    nfc_image[tlv] = header == 3 ? 0xD2 : 0xC2;
    nfc_image[tlv + 1] = 30;
    if (header == 3) nfc_image[tlv + 2] = used;
    else NfcPut32(nfc_image + tlv + 2, used);
    memcpy(nfc_image + tlv + header, "application/vnd.studiodue.sdcp", 30);
    nfc_image[prefix + used] = 0xFE;
    memset(nfc_image + nfc_length, 0, sizeof nfc_image - nfc_length);
    return true;
}

typedef struct
{
    uint32_t values[CFG_PARAMETER_COUNT];
    uint32_t revision, request, applied, crc, software;
    uint8_t kind;
    const uint8_t *extensions[16];
    uint16_t extension_sizes[16];
    unsigned extension_count;
} nfc_document_t;

/* Validazione completa prima di modificare RAM o EEPROM; SR/non-SR, TLV corto/lungo.
   Le sezioni opache non critiche vengono conservate nel successivo ACK. */
static bool NfcParse(const uint8_t *memory, unsigned capacity, nfc_document_t *doc)
{
    if (capacity < 4 || memory[0] != 3) return false;
    unsigned at = 2, n = memory[1];
    if (n == 255)
    {
        at = 4; n = NfcU16(memory + 2);
    }
    if (n < 33 || at + n >= capacity || memory[at + n] != 0xFE) return false;
    for (unsigned i = at + n + 1; i < capacity; ++i)
    {
        if (memory[i] != 0 && memory[i] != 0xFF) return false;
    }
    const uint8_t *record = memory + at;
    if ((record[0] != 0xD2 && record[0] != 0xC2) || record[1] != 30) return false;
    unsigned h = record[0] == 0xD2 ? 3u : 6u;
    unsigned length = h == 3 ? record[2] : NfcU32(record + 2);
    if (length < 10 || length > 4096 || h + 30u + length != n ||
        memcmp(record + h, "application/vnd.studiodue.sdcp", 30)) return false;
    const uint8_t *d = record + h + 30;
    if (memcmp(d, "SD\x20\0", 4) || NfcU16(d + 4) != length ||
        NfcU32(d + length - 4) != NfcCrc(d, length - 4)) return false;
    memset(doc, 0, sizeof *doc);
    unsigned seen = 0, previous = 0, sections = 0;
    for (at = 6; at < length - 4;)
    {
        if (length - 4 - at < 4 || ++sections > 16) return false;
        unsigned id = d[at], flags = d[at + 1], size = NfcU16(d + at + 2);
        if (id <= previous || flags > 1 || size > length - 8 - at) return false;
        const uint8_t *p = d + at + 4;
        if (id == 1)
        {
            if (flags != 1 || size != 16 || memcmp(p, nfc_identity, 6) || memcmp(p + 10, nfc_identity + 10, 6)) return false;
            doc->software = NfcU32(p + 6);
            seen |= 1;
        }
        else if (id == 2)
        {
            unsigned v = 0;
            for (unsigned i = 0; i < NFC_FIELD_COUNT; ++i)
            {
                const nfc_field_t *f = &nfc_fields[i];
                if (size - v < 4u + f->selector_size + f->value_size ||
                    NfcU16(p + v) != f->pid || p[v + 2] != f->selector_size) return false;
                v += 3;
                uint32_t selector = f->selector_size == 4 ? NfcU32(p + v) : f->selector_size ? p[v] : 0;
                if (selector != f->selector) return false;
                v += f->selector_size;
                if (p[v++] != f->value_size || !NfcValue(f, (uint8_t *)p + v, doc->values, true)) return false;
                v += f->value_size;
            }
            if (v != size || flags != 1) return false;
            seen |= 2;
        }
        else if (id == 7)
        {
            if (flags != 1 || size != 36 || NfcU32(p) != NFC_SCHEMA_REVISION || memcmp(p + 4, nfc_schema_hash, 32)) return false;
            seen |= 4;
        }
        else if (id == 0x80)
        {
            if (flags != 1 || size != 14 || p[0] != 1 || p[1] > 1) return false;
            doc->kind = p[1]; doc->revision = NfcU32(p + 2);
            doc->request = NfcU32(p + 6); doc->applied = NfcU32(p + 10);
            if (!doc->revision || doc->request != (doc->kind ? NfcNext(doc->applied) : doc->applied)) return false;
            seen |= 8;
        }
        else
        {
            if (flags) return false;
            doc->extensions[doc->extension_count] = d + at;
            doc->extension_sizes[doc->extension_count++] = size + 4;
        }
        previous = id; at += size + 4;
    }
    for (unsigned i = 0; i < CFG_PARAMETER_COUNT; ++i)
    {
        if (!CFG_ValidateValue((cfg_parameter_t)i, doc->values[i])) return false;
    }
    doc->crc = NfcU32(d + length - 4);
    return seen == 15;
}

static void NfcWait(const char *status)
{
    nfc_status = status;
    nfc_phase = N_WAIT;
    nfc_at = HAL_GetTick();
    nfc_writing = false;
}
static bool NfcBlank(void)
{
    unsigned start = nfc_memory[0] == 3 && nfc_memory[1] == 0 && nfc_memory[2] == 0xFE ? 3 : 0;
    for (unsigned i = start; i < NFC_USER_BYTES; ++i)
    {
        if (nfc_memory[i] != 0 && nfc_memory[i] != 0xFF) return false;
    }
    return true;
}
/* Init esplicito: riconosce solo NDEF del dispositivo, senza importare i dati.
   Il legacy poteva iniziare dopo NULL TLV; il normale parser SDCP2 resta rigoroso. */
static bool NfcOwnedContent(unsigned *used)
{
    const uint8_t *m = nfc_memory;
    unsigned start = 0;
    while (start < nfc_capacity && m[start] == 0) ++start;
    if (start + 4u >= nfc_capacity || m[start] != 3) return false;
    unsigned pos = start + 2, size = m[start + 1];
    if (size == 255) { size = NfcU16(m + start + 2); pos += 2; }
    if (size < 3 || pos + size >= nfc_capacity || m[pos + size] != 0xFE) return false;
    *used = pos + size + 1;
    for (unsigned i = *used; i < NFC_USER_BYTES; ++i)
    {
        if (m[i] != 0 && m[i] != 0xFF) return false;
    }
    const uint8_t *r = m + pos;
    if (r[0] != 0xD2 && r[0] != 0xC2) return false;
    unsigned h = r[0] == 0xD2 ? 3 : 6;
    if (size < h) return false;
    unsigned length = h == 3 ? r[2] : NfcU32(r + 2);
    if (length > size || h + r[1] + length != size) return false;
    const uint8_t *data = r + h + r[1];
    const char legacy[] = "application/vnd.FGM.adsw100.cfg";
    const char sdcp[] = "application/vnd.studiodue.sdcp";
    if (r[1] == sizeof legacy - 1 && !memcmp(r + h, legacy, sizeof legacy - 1))
    {
        char json[768], actual[64], expected[64], ip[16], mask[16], gw[16];
        uint8_t mode, net, sub;
        uint16_t universe, priority;
        bool dhcp;
        if (length >= sizeof json || memchr(data, 0, length)) return false;
        memcpy(json, data, length); json[length] = 0;
        build_magic_from_eui(expected, sizeof expected, nfc_eui);
        return json_to_cfg_vars(json, actual, sizeof actual, &mode, &universe, &net, &sub,
                                &priority, &dhcp, ip, mask, gw) && !strcmp(actual, expected);
    }
    if (r[1] != sizeof sdcp - 1 || memcmp(r + h, sdcp, sizeof sdcp - 1) || length < 30 ||
        memcmp(data, "SD\x20\0", 4) || NfcU16(data + 4) != length ||
        NfcU32(data + length - 4) != NfcCrc(data, length - 4)) return false;
    /* Identità standard all'inizio del documento; niente applicazione di VALUES. */
    return data[6] == 1 && data[7] == 1 && NfcU16(data + 8) == 16 &&
           !memcmp(data + 10, nfc_identity, 6) && !memcmp(data + 20, nfc_identity + 10, 6);
}

static bool NfcPublish(uint32_t *saved)
{
    if (!NfcBuild(saved, &nfc_meta)) return false;
    if (nfc_meta.state == EE_NFC_INIT_TAG)
    {
        unsigned clear = (nfc_factory_used + 15u) / 16u;
        if (clear > nfc_blocks) nfc_blocks = clear;
        if (nfc_blocks > 55 || nfc_blocks * 16u > nfc_capacity) return false;
    }
    nfc_writing = true;
    nfc_block = 0;
    nfc_phase = N_WRITE_RF;
    nfc_status = "PUBLISHING";
    return true;
}
static bool NfcSameValues(const uint32_t *a, const uint32_t *b)
{
    return memcmp(a, b, CFG_PARAMETER_COUNT * sizeof *a) == 0;
}
static bool NfcApply(const uint32_t *values)
{
    for (unsigned i = 0; i < CFG_PARAMETER_COUNT; ++i)
    {
        uint32_t current;
        if (!CFG_GetValue((cfg_parameter_t)i, &current) ||
            (values[i] != current && !CFG_SetValue((cfg_parameter_t)i, values[i]))) return false;
    }
    return true;
}

/* Main: EEPROM autorevole. REQUEST solo su base verificata, stato preparato
   persistente prima dei parametri, ACK soltanto dopo readback integrale. */
static void NfcProcess(void)
{
    if (DMX512_BusBusy() || RDM_SavePending())
    {
        NfcWait("WAIT_RDM");
        return;
    }
    uint32_t saved[CFG_PARAMETER_COUNT];
    nfc_document_t doc;
    bool valid = NfcParse(nfc_memory, nfc_capacity, &doc);
    bool blank = NfcBlank();
    int metadata = EE_NfcRead(&nfc_meta);
    if (metadata < 0 || !NfcSaved(saved))
    {
        NfcWait("EEPROM_ERROR");
        return;
    }
    if (nfc_meta.state == EE_NFC_INIT_PREPARE)
    {
        NfcWait("INIT_INTERRUPTED");
        return;
    }
    if (metadata == 1 && nfc_meta.state == EE_NFC_INIT_TAG)
    {
        nfc_factory_used = 0;
        if ((!blank && !NfcOwnedContent(&nfc_factory_used)) || !NfcRuntimeEqual(saved))
        {
            NfcWait("INIT_FOREIGN_OR_EDIT");
            return;
        }
        if (valid && !doc.kind && doc.revision == nfc_meta.revision && doc.applied == nfc_meta.request &&
            NfcSameValues(saved, doc.values) && doc.software == NfcU32(nfc_identity + 6))
        {
            if (EE_NfcSeal(&nfc_meta, EE_NFC_COMMITTED) == HAL_OK)
            {
                nfc_meta.state = EE_NFC_COMMITTED;
                NfcWait("SYNCED");
            }
            else NfcWait("METADATA_ERROR");
            return;
        }
        nfc_extension_length = 0;
        if (nfc_cc_provision)
        {
            nfc_phase = N_CC_GUARD;
            return;
        }
        if (!NfcPublish(saved)) NfcWait("CAPACITY_ERROR");
        return;
    }
    if (!valid && !blank)
    {
        NfcWait("FOREIGN_OR_INVALID");
        return;
    }
    if (!metadata)
    {
        /* Non assegnare una nuova base a una vecchia REQUEST/JSON/SNAPSHOT. */
        if (!blank || !NfcRuntimeEqual(saved))
        {
            NfcWait("PROVISION_REQUIRED");
            return;
        }
        nfc_meta = (ee_nfc_meta_t){.revision = 1, .state = EE_NFC_COMMITTED};
        if (EE_NfcStore(&nfc_meta) != HAL_OK)
        {
            NfcWait("METADATA_ERROR");
            return;
        }
    }
    nfc_extension_length = 0;
    if (valid)
    {
        for (unsigned i = 0; i < doc.extension_count; ++i)
        {
            unsigned size = doc.extension_sizes[i];
            if (size > sizeof nfc_extensions - nfc_extension_length)
            {
                NfcWait("CAPACITY_ERROR");
                return;
            }
            memcpy(nfc_extensions + nfc_extension_length, doc.extensions[i], size);
            nfc_extension_length += size;
        }
        /* Preflight ACK prima di qualsiasi scrittura applicativa. */
        if (!NfcBuild(doc.values, &nfc_meta))
        {
            NfcWait("CAPACITY_ERROR");
            return;
        }
    }
    if (nfc_meta.state == EE_NFC_LOCAL)
    {
        if (!EE_NfcLocalLive() || !NfcRuntimeEqual(saved) || RDM_SavePending() || HAL_GetTick() - EE_NfcLocalAt() < 250u)
        {
            NfcWait("LOCAL_RECOVERY_OR_EDIT");
            return;
        }
        nfc_meta.state = EE_NFC_COMMITTED;
        if (EE_NfcStore(&nfc_meta) != HAL_OK)
        {
            NfcWait("METADATA_ERROR");
            return;
        }
    }
    if (nfc_meta.state == EE_NFC_REQUEST)
    {
        /* Reset dopo preparazione: non ripetere scritture di configurazione.
           Completa soltanto se tutta la configurazione è già quella richiesta. */
        if (!valid || !doc.kind || doc.request != nfc_meta.request || doc.crc != nfc_meta.crc ||
            NfcNext(doc.revision) != nfc_meta.revision || !NfcSameValues(saved, doc.values) ||
            !NfcRuntimeEqual(saved))
        {
            NfcWait("REQUEST_RECOVERY_REQUIRED");
            return;
        }
        nfc_meta.state = EE_NFC_COMMITTED;
        if (EE_NfcStore(&nfc_meta) != HAL_OK)
        {
            NfcWait("METADATA_ERROR");
            return;
        }
    }
    if (valid && doc.kind)
    {
        if (doc.request == nfc_meta.request)
        {
            if (doc.crc != nfc_meta.crc)
            {
                NfcWait("DUPLICATE_ID");
                return;
            }
            ++nfc_replays;
        }
        else
        {
            if (doc.revision != nfc_meta.revision || doc.applied != nfc_meta.request)
            {
                NfcWait("TRANSACTION_BASE");
                return;
            }
            if (!NfcRuntimeEqual(saved) || RDM_SavePending() || DMX512_BusBusy())
            {
                NfcWait("WAIT_LOCAL_OR_RDM");
                return;
            }
            nfc_meta.revision = NfcNext(nfc_meta.revision);
            nfc_meta.request = doc.request;
            nfc_meta.crc = doc.crc;
            nfc_meta.state = EE_NFC_REQUEST;
            if (EE_NfcStore(&nfc_meta) != HAL_OK)
            {
                NfcWait("METADATA_ERROR");
                return;
            }
            EE_NfcTransaction(true);
            bool ok = NfcApply(doc.values) && EE_SaveConfig() == HAL_OK;
            EE_NfcTransaction(false);
            uint32_t verify[CFG_PARAMETER_COUNT];
            if (!ok || !NfcSaved(verify) || !NfcSameValues(verify, doc.values))
            {
                (void)NfcApply(saved);
                NfcWait("REQUEST_RECOVERY_REQUIRED");
                return;
            }
            memcpy(saved, verify, sizeof saved);
            nfc_meta.state = EE_NFC_COMMITTED;
            if (EE_NfcStore(&nfc_meta) != HAL_OK)
            {
                NfcWait("METADATA_ERROR");
                return;
            }
            ++nfc_applied;
        }
    }
    if (!NfcRuntimeEqual(saved))
    {
        NfcWait("UNSAVED_LOCAL");
        return;
    }
    if (valid && !doc.kind)
    {
        if (doc.revision == nfc_meta.revision && doc.applied == nfc_meta.request && NfcSameValues(saved, doc.values))
        {
            if (doc.software != NfcU32(nfc_identity + 6))
            {
                if (!NfcPublish(saved)) NfcWait("CAPACITY_ERROR");
            }
            else NfcWait("SYNCED");
            return;
        }
        if (doc.applied != nfc_meta.request || (int32_t)(nfc_meta.revision - doc.revision) <= 0)
        {
            NfcWait("SNAPSHOT_CONFLICT");
            return;
        }
    }
    if (!NfcPublish(saved)) NfcWait("CAPACITY_ERROR");
}

static bool NfcRead(uint8_t reg, uint8_t *out, unsigned length, nfc_phase_t next)
{
    if (!BUS_Read(BUS_NFC, s_nfc_addr, reg, out, length)) return false;
    nfc_pending = true;
    nfc_after = next;
    return true;
}
static bool NfcWrite(uint8_t reg, const uint8_t *data, nfc_phase_t next)
{
    /* Ricontrolla subito prima del TX: il pannello può aver salvato durante
       la precedente lettura RF, dopo il controllo in N_WRITE_RF. */
    ee_nfc_meta_t current;
    if (EE_NfcRead(&current) != 1 || current.state != nfc_meta.state ||
        (current.state != EE_NFC_COMMITTED && current.state != EE_NFC_INIT_TAG) ||
        current.revision != nfc_meta.revision || current.request != nfc_meta.request || current.crc != nfc_meta.crc)
    {
        NfcWait("LOCAL_CHANGED_DURING_PUBLISH");
        return false;
    }
    if (!BUS_Write(BUS_NFC, s_nfc_addr, reg, (uint8_t *)data, 16)) return false;
    nfc_pending = true;
    nfc_after = next;
    return true;
}
static bool NfcSession(uint8_t reg, uint8_t *out, nfc_phase_t next)
{
    if (!BUS_NfcSession(reg, out)) return false;
    nfc_pending = true;
    nfc_after = next;
    return true;
}

void NFC_Init(void)
{
    BUS_Init();
    memset(&nfc_meta, 0, sizeof nfc_meta);
    (void)EE_NfcRead(&nfc_meta);
    nfc_extension_length = 0;
    nfc_cc_provision = false;
    nfc_phase = N_HEADER;
    nfc_pending = nfc_second = nfc_writing = false;
    nfc_io_at = HAL_GetTick();
    nfc_identity_ok = false;
    uint8_t eui[6];
    if (RDM_Enabled() && EE_Read(0xFA, eui, 6) == HAL_OK)
    {
        NfcPut16(nfc_identity, BRAND_ESTA_MANUFACTURER_ID);
        NfcPut16(nfc_identity + 2, ADSW_RDM_MODEL_ID);
        NfcPut16(nfc_identity + 4, NFC_CATALOG_CATEGORY);
        NfcPut32(nfc_identity + 6, (uint32_t)FW_VERSION_MAJOR << 16 | FW_VERSION_MINOR << 8 | FW_VERSION_PATCH);
        NfcPut16(nfc_identity + 10, BRAND_ESTA_MANUFACTURER_ID);
        memcpy(nfc_identity + 12, eui + 2, 4);
        memcpy(nfc_eui, eui, 6);
        nfc_identity_ok = true;
    }
    s_nfc_addr = 0x02;
    nfc_confirmed_addr = nfc_nack_streak = 0;
    nfc_capacity = 0;
    nfc_ev_irqs = nfc_er_irqs = 0;
    nfc_tx_callbacks = nfc_rx_callbacks = nfc_error_callbacks = 0;
    nfc_status = nfc_identity_ok ? "PROBING" : "IDENTITY_ERROR";
}

/* Tutti gli scambi NTAG sono IT e cedono il main. Le API EEPROM legacy
   sincrone/readback sono riutilizzate soltanto nel commit, mai in ISR. */
void NFC_Service(void)
{
    /* Disabilitazione cooperativa: termina lo scambio e l'eventuale pubblicazione
       già iniziati; non forza il rilascio di lock posseduti da HAL/NTAG. */
    if (!nfc_enabled && !nfc_pending && !nfc_writing)
    {
        nfc_status = "DISABLED";
        return;
    }
    if (!nfc_identity_ok) return;
    BUS_Task();
    if (nfc_pending)
    {
        int result = BUS_Result(BUS_NFC);
        if (!result) return;
        nfc_pending = false;
        nfc_io_at = HAL_GetTick();
        if (result < 0)
        {
            ++nfc_errors;
            BusDiagnostic bus;
            BUS_NfcDiagnostics(&bus);
            /* DRV usa3NACK consecutivi per CHIP_ABSENT. Su H7 XferCount è già
               decrementato dal preload TX: non usare remaining==1 come prova ACK. */
            bool address_nack = bus.phase == 1 && bus.reg == 0 && bus.hal_status == HAL_ERROR && !bus.timeout &&
                (bus.hal_error & HAL_I2C_ERROR_AF) && nfc_after == N_HEADER;
            if (address_nack)
            {
                if (!nfc_confirmed_addr || ++nfc_nack_streak >= 3)
                {
                    nfc_confirmed_addr = 0;
                    nfc_nack_streak = 0;
                    NfcNextAddress();
                }
            }
            else nfc_nack_streak = 0;
            NfcWait("I2C_ERROR");
            return;
        }
        nfc_phase = nfc_after;
        return;
    }
    /* Finisce l'operazione pendente ma non ne avvia altre mentre il pannello
       configura: evita BUSY sul salvataggio, senza interferire con la navigazione. */
    if (UI_ConfigActive()) return;
    /* Lascia intervalli liberi a EEPROM/RTC fra le operazioni NFC. */
    if (HAL_GetTick() - nfc_io_at < 2u) return;
    switch (nfc_phase)
    {
        case N_WAIT:
            if (HAL_GetTick() - nfc_at >= 500u)
            {
                nfc_second = false;
                nfc_phase = N_HEADER;
            }
            break;
        case N_HEADER:
            if (!nfc_second)
            {
                nfc_second = true;
                (void)NfcRead(0, nfc_header, 16, N_HEADER);
                break;
            }
            nfc_second = false;
            if (!ChipHeader(nfc_header))
            {
                nfc_confirmed_addr = 0;
                NfcNextAddress();
                NfcWait("UNSUPPORTED_CHIP");
                break;
            }
            nfc_confirmed_addr = s_nfc_addr;
            nfc_nack_streak = 0;
            ee_nfc_meta_t init;
            bool cc_valid = nfc_header[12] == 0xE1 && nfc_header[13] == 0x10 && nfc_header[15] == 0 && nfc_header[14];
            nfc_cc_provision = !cc_valid && EE_NfcRead(&init) == 1 && init.state == EE_NFC_INIT_TAG &&
                (nfc_header[12] == 0 || nfc_header[12] == 0xE1) &&
                (nfc_header[13] == 0 || nfc_header[13] == 0x10) &&
                (nfc_header[14] == 0 || nfc_header[14] == 0x66) && nfc_header[15] == 0;
            if (!cc_valid && !nfc_cc_provision)
            {
                NfcWait("CC_PROVISION_REQUIRED");
                break;
            }
            nfc_capacity = nfc_cc_provision ? 816u : (uint16_t)nfc_header[14] * 8u;
            if (nfc_capacity > NFC_USER_BYTES) nfc_capacity = NFC_USER_BYTES;
            if (nfc_capacity < NFC_IMAGE_BYTES)
            {
                NfcWait("CAPACITY_ERROR");
                break;
            }
            if (nfc_cc_provision)
            {
                /* Probe2k del riferimento DRV: la versione1k non espone questo blocco. */
                (void)NfcRead(0x40, nfc_cc_read, 16, N_CAPACITY);
                break;
            }
            (void)NfcRead(0x38, nfc_access, 16, N_ACCESS);
            break;
        case N_CAPACITY:
            (void)NfcRead(0x38, nfc_access, 16, N_ACCESS);
            break;
        case N_ACCESS:
            (void)NfcRead(0x39, nfc_control, 16, N_CONTROL);
            break;
        case N_CONTROL:
            (void)NfcRead(0x3A, nfc_config, 16, N_CONFIG);
            break;
        case N_CONFIG:
            (void)NfcSession(0, &nfc_mode, N_MODE);
            break;
        case N_MODE:
            if (nfc_header[10] || nfc_header[11] || nfc_access[8] || nfc_access[9] || nfc_access[10] ||
                nfc_access[15] <= 223u || (nfc_mode & 0x42u))
            {
                NfcWait("PROTECTED_TAG");
                break;
            }
            nfc_block = 1;
            nfc_phase = N_RF;
            break;
        case N_RF:
            (void)NfcSession(6, &nfc_rf, N_SCAN);
            break;
        case N_SCAN:
            if (nfc_rf & 0x21u)
            {
                NfcWait("RF_BUSY");
                break;
            }
            if (nfc_block <= 56)
            {
                if (NfcRead(nfc_block, nfc_memory + (nfc_block - 1u) * 16u, 16, N_RF)) ++nfc_block;
                break;
            }
            ++nfc_reads;
            if (!nfc_second)
            {
                memcpy(nfc_previous, nfc_memory, NFC_USER_BYTES);
                nfc_second = true;
                nfc_block = 1;
                nfc_phase = N_RF;
            }
            else if (memcmp(nfc_previous, nfc_memory, NFC_USER_BYTES)) NfcWait("TAG_CHANGED");
            else nfc_phase = N_PROCESS;
            break;
        case N_PROCESS:
            NfcProcess();
            break;
        case N_WRITE_RF:
        {
            ee_nfc_meta_t current;
            if (EE_NfcRead(&current) != 1 || current.state != nfc_meta.state ||
                (current.state != EE_NFC_COMMITTED && current.state != EE_NFC_INIT_TAG) ||
                current.revision != nfc_meta.revision || current.request != nfc_meta.request || current.crc != nfc_meta.crc)
            {
                NfcWait("LOCAL_CHANGED_DURING_PUBLISH");
                break;
            }
            (void)NfcSession(6, &nfc_rf, nfc_block == 0 ? N_INVALIDATE : nfc_block > nfc_blocks ? N_FINISH : N_BODY);
            break;
        }
        case N_INVALIDATE:
            if (nfc_rf & 0x21u) { NfcWait("RF_BUSY"); break; }
            if (NfcWrite(1, nfc_invalid, N_WRITE_RF)) nfc_block = 2;
            break;
        case N_BODY:
            if (nfc_rf & 0x21u) { NfcWait("RF_BUSY"); break; }
            if (NfcWrite(nfc_block, nfc_image + (nfc_block - 1u) * 16u, N_WRITE_RF)) ++nfc_block;
            break;
        case N_FINISH:
            if (nfc_rf & 0x21u) { NfcWait("RF_BUSY"); break; }
            if (NfcWrite(1, nfc_image, N_VERIFY)) nfc_verify_block = 1;
            break;
        case N_VERIFY:
            if (nfc_verify_block <= nfc_blocks)
            {
                if (NfcRead(nfc_verify_block, nfc_memory + (nfc_verify_block - 1u) * 16u, 16, N_VERIFY)) ++nfc_verify_block;
            }
            else if (memcmp(nfc_memory, nfc_image, nfc_blocks * 16u)) NfcWait("VERIFY_ERROR");
            else if (nfc_meta.state == EE_NFC_INIT_TAG && EE_NfcSeal(&nfc_meta, EE_NFC_COMMITTED) != HAL_OK)
                NfcWait("METADATA_ERROR");
            else
            {
                nfc_meta.state = EE_NFC_COMMITTED;
                NfcWait("SYNCED");
            }
            break;
        case N_CC_GUARD:
            (void)NfcRead(0, nfc_cc_read, 16, N_CC_CHECK);
            break;
        case N_CC_CHECK:
            if (memcmp(nfc_cc_read, nfc_header, 16) || (nfc_config[6] & 2u))
            {
                NfcWait("CC_GUARD_ERROR");
                break;
            }
            memcpy(nfc_cc_write, nfc_header, 16);
            /* UID0 letto04, ma il byte0 scritto mantiene l'indirizzo I2C effettivo. */
            nfc_cc_write[0] = s_nfc_addr << 1;
            nfc_cc_write[12] = 0xE1; nfc_cc_write[13] = 0x10;
            nfc_cc_write[14] = 0x66; nfc_cc_write[15] = 0;
            nfc_phase = N_CC_RF;
            break;
        case N_CC_RF:
            (void)NfcSession(6, &nfc_rf, N_CC_WRITE);
            break;
        case N_CC_WRITE:
            if (nfc_rf & 0x21u) { NfcWait("RF_BUSY"); break; }
            (void)NfcWrite(0, nfc_cc_write, N_CC_READBACK);
            break;
        case N_CC_READBACK:
            (void)NfcRead(0, nfc_cc_read, 16, N_CC_VERIFY);
            break;
        case N_CC_VERIFY:
            if (nfc_cc_read[0] != 4 || memcmp(nfc_cc_read + 1, nfc_cc_write + 1, 15)) NfcWait("CC_VERIFY_ERROR");
            else NfcWait("CC_VERIFIED");
            break;
    }
}

void NFC_FormatDiagnostics(char *out, size_t capacity)
{
    BusDiagnostic bus;
    BUS_NfcDiagnostics(&bus);
    char address[3] = "--";
    if (nfc_confirmed_addr) snprintf(address, sizeof address, "%02X", nfc_confirmed_addr);
    snprintf(out, capacity, "nfc=%s addr=%s probe=%02X hal=%02X capacity=%u rev=%lu request=%lu state=%02X scans=%lu applied=%lu replay=%lu errors=%lu timeout=%lu nack=%lu\r\n",
             nfc_status, address, s_nfc_addr, NfcHalAddress(s_nfc_addr), nfc_capacity,
             (unsigned long)nfc_meta.revision, (unsigned long)nfc_meta.request, nfc_meta.state,
             (unsigned long)nfc_reads, (unsigned long)nfc_applied, (unsigned long)nfc_replays,
             (unsigned long)nfc_errors, (unsigned long)bus.timeouts, (unsigned long)bus.nacks);
}

/* Fotografia dell'ultimo errore prima del reset HAL; nessun I/O o stampa in ISR. */
void NFC_FormatBusDiagnostics(char *out, size_t capacity)
{
    BusDiagnostic b;
    BUS_NfcDiagnostics(&b);
    snprintf(out, capacity, "i2c last_hal=%02X phase=%u reg=%02X rc=%u err=%lX left=%u state=%lX CR1=%08lX CR2=%08lX ISR=%08lX irq=%lu/%lu en=%lu pend=%lu mask=%lu/%lu lines=%lu cb=%lu/%lu/%lu liveirq=%lu/%lu\r\n",
             b.hal_address, b.phase, b.reg, b.hal_status, (unsigned long)b.hal_error, b.remaining,
             (unsigned long)b.state, (unsigned long)b.cr1, (unsigned long)b.cr2, (unsigned long)b.isr,
             (unsigned long)b.ev, (unsigned long)b.er, (unsigned long)b.enabled, (unsigned long)b.pending,
             (unsigned long)b.primask, (unsigned long)b.basepri, (unsigned long)b.lines,
             (unsigned long)nfc_tx_callbacks, (unsigned long)nfc_rx_callbacks, (unsigned long)nfc_error_callbacks,
             (unsigned long)nfc_ev_irqs, (unsigned long)nfc_er_irqs);
}

/* Comando main CDC, non persistente: la quiescenza è osservabile con i2c?. */
void NFC_SetEnabled(bool enabled)
{
    nfc_enabled = enabled;
}

/* Stage1 prima di RTC/EEPROM, poi2 dopo RTC,3 dopo hardware_check,4 boot pronto.
   Conserva i registri prima del servizio NFC e prima della pulizia flag reset. */
void NFC_RecordBoot(uint32_t stage, uint32_t reset_flags)
{
    nfc_boot_stage = stage;
    if (stage == 1)
    {
        nfc_boot_reset = reset_flags;
        nfc_boot_isr = hi2c1.Instance->ISR;
        nfc_boot_cr1 = hi2c1.Instance->CR1;
        nfc_boot_lines = (GPIOB->IDR >> 6) & 3u;
    }
}

void NFC_FormatLiveDiagnostics(char *out, size_t capacity)
{
    snprintf(out, capacity, "i2c enabled=%u owner=%u phase=%u result=%d reserved=%u pending=%u writing=%u HAL=%lX lock=%u err=%lX ISR=%08lX CR1=%08lX lines=%lu boot=%lu reset=%08lX bootISR=%08lX bootCR1=%08lX bootlines=%lu\r\n",
             nfc_enabled, (unsigned)active, nfc_bus_phase, result, ee_nfc_bus_reserved,
             nfc_pending, nfc_writing, (unsigned long)HAL_I2C_GetState(&hi2c1), (unsigned)hi2c1.Lock,
             (unsigned long)hi2c1.ErrorCode, (unsigned long)hi2c1.Instance->ISR,
             (unsigned long)hi2c1.Instance->CR1, (unsigned long)((GPIOB->IDR >> 6) & 3u),
             (unsigned long)nfc_boot_stage, (unsigned long)nfc_boot_reset,
             (unsigned long)nfc_boot_isr, (unsigned long)nfc_boot_cr1, (unsigned long)nfc_boot_lines);
}
