/*
 * eeprom.c
 */
#include "h_files.h"
#include "defs.h"

uint8_t g_netmask_bad = 0;   // 0 = ok, 1 = netmask sospetta/errata


// Attendi fine write cycle
static HAL_StatusTypeDef EE_WaitWrite(void)
{
    // Polling ACK sullo slave addr
    for (int i = 0; i < 50; i++)
    {
        if (HAL_I2C_IsDeviceReady(&hi2c1, (EE_I2C_ADDR_7B << 1), 1, 5) == HAL_OK)
            return HAL_OK;
        HAL_Delay(1);
    }
    return HAL_TIMEOUT;
}

uint8_t EE_ReadU8(uint16_t addr)
{
    uint8_t b = 0;
    (void)EE_Read(addr, &b, 1);
    return b;
}

void EE_WriteU8(uint16_t addr, uint8_t v)
{
    (void)EE_Write(addr, &v, 1);
}

uint16_t EE_ReadU16(uint16_t addr)
{
    uint8_t b[2] = {0};
    if (EE_Read(addr, b, 2) != HAL_OK) return 0;
    return (uint16_t)((b[0] << 8) | b[1]);
}

void EE_WriteU16(uint16_t addr, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)(v & 0xFF) };
    (void)EE_Write(addr, b, 2);
}

uint32_t EE_ReadU32(uint16_t addr)
{
    uint8_t b[4] = {0};
    if (EE_Read(addr, b, 4) != HAL_OK) return 0;
    return (uint32_t)((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]);
}

void EE_WriteU32(uint16_t addr, uint32_t v)
{
    uint8_t b[4] = {
        (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v
    };
    (void)EE_Write(addr, b, 4);
}

HAL_StatusTypeDef EE_Read(uint16_t memAddr, uint8_t* buf, uint16_t len)
{
    if (!buf) return HAL_ERROR;
    if ((memAddr + len) > EE_SIZE) return HAL_ERROR;
    return HAL_I2C_Mem_Read(&hi2c1, (EE_I2C_ADDR_7B << 1), memAddr,
                            I2C_MEMADD_SIZE_8BIT, buf, len, 100);
}

HAL_StatusTypeDef EE_Write(uint16_t memAddr, const uint8_t* buf, uint16_t len)
{
    if (!buf) return HAL_ERROR;
    if ((memAddr + len) > EE_SIZE) return HAL_ERROR;

    // 24AA02E48: page size = 8 byte
    const uint8_t page = 8;
    uint16_t off = 0;

    while (off < len)
    {
        uint16_t chunk = (uint16_t)(page - ((memAddr + off) % page));
        if (chunk > (len - off)) chunk = (uint16_t)(len - off);

        HAL_StatusTypeDef st = HAL_I2C_Mem_Write(&hi2c1, (EE_I2C_ADDR_7B << 1),
                                (uint16_t)(memAddr + off),
                                I2C_MEMADD_SIZE_8BIT,
                                (uint8_t*)&buf[off], chunk, 100);
        if (st != HAL_OK) return st;

        st = EE_WaitWrite();
        if (st != HAL_OK) return st;

        off = (uint16_t)(off + chunk);
    }
    return HAL_OK;
}

// Legge MAC-48 dalla 24AA02E48 (indirizzi 0xFA..0xFF)
// Restituisce HAL_OK solo se il MAC è sensato e UNICAST (bit0==0)
static HAL_StatusTypeDef EE_ReadEui48(uint8_t mac[6])
{
    if (!mac) return HAL_ERROR;

    HAL_StatusTypeDef st = EE_Read(0xFA, mac, 6);
    if (st != HAL_OK) return st;

    // Sanity: non tutti 0x00 / non tutti 0xFF
    uint8_t acc_or = 0, acc_and = 0xFF;
    for (int i = 0; i < 6; i++) { acc_or |= mac[i]; acc_and &= mac[i]; }
    if (acc_or == 0x00 || acc_and == 0xFF) return HAL_ERROR;

    // MUST be unicast (bit0 == 0). Se è 1 (multicast), rifiuta.
    if (mac[0] & 0x01) return HAL_ERROR;

    // (Opzionale) Se vuoi essere ancora più stretto, accetta solo OUI Microchip:
    // if (mac[0]!=0x00 || mac[1]!=0x04 || mac[2]!=0xA3) return HAL_ERROR;

    return HAL_OK;
}

// Per la E64
HAL_StatusTypeDef EE_ReadEui64(uint8_t eui64[8])
{
    if (!eui64) return HAL_ERROR;
    return EE_Read(0xF8, eui64, 8);
}

// API usata nel codice: restituisce SEMPRE un MAC valido
HAL_StatusTypeDef EE_GetLocalMAC48(uint8_t mac[6])
{
    if (!mac) return HAL_ERROR;

    // 1) Produzione: prova E48
    if (EE_ReadEui48(mac) == HAL_OK) {
        return HAL_OK;
    }

    // 2) Fallback per PCB con E64: prova a derivare un MAC da EUI-64
    uint8_t eui64[8];
    if (EE_ReadEui64(eui64) == HAL_OK) {
        // Se è "canonical" (… FF:FE …), ricava il MAC standard
        if (eui64[3] == 0xFF && eui64[4] == 0xFE) {
            mac[0] = eui64[0];
            mac[1] = eui64[1];
            mac[2] = eui64[2];
            mac[3] = eui64[5];
            mac[4] = eui64[6];
            mac[5] = eui64[7];
        } else {
            // Non canonical (come il tuo 00-04-A3-0B-01-15-0D-88):
            // Prendi i primi 3 (OUI) + ultimi 3
            mac[0] = eui64[0];
            mac[1] = eui64[1];
            mac[2] = eui64[2];
            mac[3] = eui64[5];
            mac[4] = eui64[6];
            mac[5] = eui64[7];
        }
        // UNICAST (bit0=0)
        mac[0] &= (uint8_t)~0x01;
        return HAL_OK;
    }

    // 3) In caso di emergenza: genera un LAA dal UID MCU (bit1=1, bit0=0)
    uint32_t uid0 = *(uint32_t*)UID_BASE;
    uint32_t uid1 = *(uint32_t*)(UID_BASE + 4);
    uint32_t mix  = uid0 ^ (uid1 << 1) ^ (uid1 >> 1);

    mac[0] = (uint8_t)((mix >> 24) & 0xFC); // azzera bit0,1
    mac[0] |= 0x02;                         // LAA=1, I/G=0  → unicast locale
    mac[1] = (mix >> 16) & 0xFF;
    mac[2] = (mix >>  8) & 0xFF;
    mac[3] = (mix      ) & 0xFF;
    mac[4] = (uid0 >>  8) & 0xFF;
    mac[5] = (uid0      ) & 0xFF;
    return HAL_OK;
}

// ---------- Carica la configurazione ----------

HAL_StatusTypeDef EE_LoadConfig(void)
{
    if (EE_ReadU16(EE_OFFS_INIT) != EE_INIT_VALUE)
        return HAL_ERROR;   // EEPROM non inizializzata

    use_dhcp       = EE_ReadU8 (EE_OFFS_USE_DHCP);
    ip_address     = EE_ReadU32(EE_OFFS_IP);
    net_mask       = EE_ReadU32(EE_OFFS_NETMASK);
    gateway        = EE_ReadU32(EE_OFFS_GATEWAY);
    dns1           = EE_ReadU32(EE_OFFS_DNS1);
    dns2           = EE_ReadU32(EE_OFFS_DNS2);
    dmx_address    = EE_ReadU16(EE_OFFS_DMX_ADDR);
    dmx_mode       = EE_ReadU8 (EE_OFFS_DMX_MODE);
    artnet_universe= EE_ReadU16(EE_OFFS_ARTNET_UNI);
    sacn_universe  = EE_ReadU16(EE_OFFS_SACN_UNI);
    sacn_transport = EE_ReadU8 (EE_OFFS_SACN_TRANSPORT);
    sacn_min_prio  = EE_ReadU8 (EE_OFFS_SACN_MIN_PRIO);
    lcd_backlight  = EE_ReadU8 (EE_OFFS_LCD_BACKLIGHT);

    // System (v1.3.1+)
    off_timeout_s  = EE_ReadU8 (EE_OFFS_OFF_TIMEOUT_S);
    proto_priority = EE_ReadU8 (EE_OFFS_PROTO_PRIORITY);

    if (dmx_address < 1 || dmx_address > 512) dmx_address = 1;
    if (dmx_mode > 3) dmx_mode = CFG_DEFAULT_DMX_MODE;

    // Protocollo attivo runtime: all'avvio, se Auto parte da DMX (0)
    active_proto = (dmx_mode <= 2) ? dmx_mode : 0;
    if (sacn_min_prio > 200) sacn_min_prio = 200;

    // Compatibilità aggiornamenti: se in EEPROM c'è spazzatura (device vecchi)
    // imposta default e riscrive.
    if (off_timeout_s > 30) {
        off_timeout_s = CFG_DEFAULT_OFF_TIMEOUT_S;
        EE_WriteU8(EE_OFFS_OFF_TIMEOUT_S, off_timeout_s);
    }
    if (proto_priority > 5) {
        proto_priority = CFG_DEFAULT_PROTO_PRIORITY;
        EE_WriteU8(EE_OFFS_PROTO_PRIORITY, proto_priority);
    }

    // Controlla netmask
    if (!use_dhcp)
    {
        // in STATICO: 0.0.0.0 --> sicuramente sbagliata
        if (net_mask == 0)
        {
            g_netmask_bad = 1;
            USB_Printf("WARNING: NETMASK is 0.0.0.0 (invalid in static mode)\r\n");
        }
        else
        {
            g_netmask_bad = 0;
        }
    }
    else
    {
        // in DHCP non controllare netmask
        g_netmask_bad = 0;
    }

    return HAL_OK;
}

// ---------- Inizializza EEPROM ---------------

HAL_StatusTypeDef EE_InitMemory(void)
{
    // Cancella il valore di init
    EE_WriteU16(EE_OFFS_INIT, 0x0000);

    // Network
    EE_Write(EE_OFFS_USE_DHCP, (uint8_t[]){1}, 1);
    EE_WriteU32(EE_OFFS_IP, 0);
    EE_WriteU32(EE_OFFS_NETMASK, 0);
    EE_WriteU32(EE_OFFS_GATEWAY, 0);
    EE_WriteU32(EE_OFFS_DNS1, 0);
    EE_WriteU32(EE_OFFS_DNS2, 0);

    // DMX
    EE_WriteU16(EE_OFFS_DMX_ADDR, 1);
    EE_Write(EE_OFFS_DMX_MODE, (uint8_t[]){2}, 1); // 0=DMX, 1=ArtNet, 2=sACN, 3=Auto

    // Art-Net
    EE_WriteU16(EE_OFFS_ARTNET_UNI, 0);

    // sACN
    EE_WriteU16(EE_OFFS_SACN_UNI, 1);
    EE_Write(EE_OFFS_SACN_TRANSPORT, (uint8_t[]){0}, 1); // 0=multicast
    EE_Write(EE_OFFS_SACN_MIN_PRIO,  (uint8_t[]){0}, 1);

    // System (v1.3.1+)
    EE_WriteU8(EE_OFFS_LCD_BACKLIGHT,   CFG_DEFAULT_LCD_BL);
    EE_WriteU8(EE_OFFS_OFF_TIMEOUT_S,   CFG_DEFAULT_OFF_TIMEOUT_S);
    EE_WriteU8(EE_OFFS_PROTO_PRIORITY,  CFG_DEFAULT_PROTO_PRIORITY);

    // Init value
    EE_WriteU16(EE_OFFS_INIT, EE_INIT_VALUE);

    return HAL_OK;
}


// Salva la configurazione corrente (globali --> EEPROM)

HAL_StatusTypeDef EE_SaveConfig(void)
{
    EE_WriteU8 (EE_OFFS_USE_DHCP, use_dhcp);
    EE_WriteU32(EE_OFFS_IP,       ip_address);
    EE_WriteU32(EE_OFFS_NETMASK,  net_mask);
    EE_WriteU32(EE_OFFS_GATEWAY,  gateway);
    EE_WriteU32(EE_OFFS_DNS1,     dns1);
    EE_WriteU32(EE_OFFS_DNS2,     dns2);

    EE_WriteU16(EE_OFFS_DMX_ADDR, dmx_address);
    EE_WriteU8 (EE_OFFS_DMX_MODE, dmx_mode);

    EE_WriteU16(EE_OFFS_ARTNET_UNI, artnet_universe);
    EE_WriteU16(EE_OFFS_SACN_UNI,   sacn_universe);
    EE_WriteU8 (EE_OFFS_SACN_TRANSPORT, sacn_transport);
    EE_WriteU8 (EE_OFFS_SACN_MIN_PRIO,  sacn_min_prio);

    EE_WriteU8 (EE_OFFS_LCD_BACKLIGHT, lcd_backlight);

    // System (v1.3.1+)
    EE_WriteU8 (EE_OFFS_OFF_TIMEOUT_S,  off_timeout_s);
    EE_WriteU8 (EE_OFFS_PROTO_PRIORITY, proto_priority);

    return HAL_OK;
}

void EE_BusScan(void)
{
    // 1) mini delay post-power-up per dare tempo ai device lenti
    HAL_Delay(5);

    USB_Printf("=== I2C BUS SCAN ===\r\n");

    // 2) Prima prova gli indirizzi tipici NTAG (incluso 0x02 raccomandato da NXP)
    const uint8_t ntag_addrs[] = { 0x02, 0x55, 0x57, 0x59 };
    const unsigned ntag_count = sizeof(ntag_addrs) / sizeof(ntag_addrs[0]);

    for (unsigned i = 0; i < ntag_count; ++i) {
        uint8_t a = ntag_addrs[i];
        HAL_StatusTypeDef st = HAL_ERROR;
        for (int r = 0; r < 4 && st != HAL_OK; ++r) {
            st = HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(a << 1), 1, 25);
            if (st != HAL_OK) HAL_Delay(3);
        }
        if (st == HAL_OK) {
            USB_Printf("I2C dev @ 0x%02X (NTAG cand)\r\n", a);
        }
    }

    // 3) Scan ampio su TUTTA la gamma valida 7-bit: 0x01..0x7E
    //    (0x00 e 0x7F sono riservati come indirizzi speciali)
    for (uint8_t a = 0x01; a <= 0x7E; a++) {

        HAL_StatusTypeDef st = HAL_ERROR;
        for (int r = 0; r < 3 && st != HAL_OK; ++r) {
            st = HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(a << 1), 1, 20);
            if (st != HAL_OK) HAL_Delay(2);
        }

        if (st == HAL_OK) {
            // etichette “speciali” per indirizzi noti
            if (a == 0x53) {
                USB_Printf("I2C dev @ 0x%02X (EEPROM)\r\n", a);
            } else if (a == 0x6F) {
                USB_Printf("I2C dev @ 0x%02X (RTC?)\r\n", a);
            } else {
                USB_Printf("I2C dev @ 0x%02X\r\n", a);
            }
        }
    }

    USB_Printf("=== I2C BUS SCAN END ===\r\n");
}

void EE_DumpTail(void)
{
    uint8_t tail[16] = {0};
    HAL_StatusTypeDef st = EE_Read(0xF0, tail, 16);
    USB_Printf("EE 0xF0..0xFF (st=%d):", st);
    for (int i=0;i<16;i++) USB_Printf(" %02X", tail[i]);
    USB_Printf("\n");
    uint8_t mac[6]={0}; st = EE_Read(0xFA, mac, 6);
    USB_Printf("E48 @0xFA..0xFF (st=%d): %02X:%02X:%02X:%02X:%02X:%02X\n",
               st, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}
