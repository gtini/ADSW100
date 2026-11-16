/*
 * eeprom.c
 */
#include "h_files.h"
#include "defs.h"

volatile bool ee_nfc_bus_reserved;
static bool nfc_transaction, nfc_local_live;
static uint32_t nfc_local_at;
/* Ultimo esito pubblico: sola RAM, nessuna lettura bus dalla CDC. */
static HAL_StatusTypeDef ee_last_write_status;
static uint16_t ee_last_write_address, ee_last_write_length;
static uint8_t ee_last_meta_state;
static int ee_last_meta_result;
static uint32_t ee_write_attempts;


uint8_t dmx_port_mode = DMX_PORT_IN;
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

HAL_StatusTypeDef EE_WriteU8(uint16_t addr, uint8_t v)
{
    return EE_Write(addr, &v, 1);
}

uint16_t EE_ReadU16(uint16_t addr)
{
    uint8_t b[2] = {0};
    if (EE_Read(addr, b, 2) != HAL_OK) return 0;
    return (uint16_t)((b[0] << 8) | b[1]);
}

HAL_StatusTypeDef EE_WriteU16(uint16_t addr, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)(v & 0xFF) };
    return EE_Write(addr, b, 2);
}

uint32_t EE_ReadU32(uint16_t addr)
{
    uint8_t b[4] = {0};
    if (EE_Read(addr, b, 4) != HAL_OK) return 0;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

HAL_StatusTypeDef EE_WriteU32(uint16_t addr, uint32_t v)
{
    uint8_t b[4] = {
        (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v
    };
    return EE_Write(addr, b, 4);
}

HAL_StatusTypeDef EE_Read(uint16_t memAddr, uint8_t* buf, uint16_t len)
{
    if (!buf) return HAL_ERROR;
    if (ee_nfc_bus_reserved) return HAL_BUSY;
    if (memAddr > EE_SIZE || len > EE_SIZE - memAddr) return HAL_ERROR;
    return HAL_I2C_Mem_Read(&hi2c1, (EE_I2C_ADDR_7B << 1), memAddr,
                            I2C_MEMADD_SIZE_8BIT, buf, len, 100);
}

static HAL_StatusTypeDef EE_WriteImpl(uint16_t memAddr, const uint8_t* buf, uint16_t len)
{
    if (!buf) return HAL_ERROR;
    if (ee_nfc_bus_reserved) return HAL_BUSY;
    if (memAddr > 128u || len > 128u - memAddr) return HAL_ERROR;

    /* Prima di qualunque scrittura locale, invalida la base NFC persistente.
       Copre anche i setter EEPROM diretti del pannello, senza modificarli. */
    if (memAddr < 0x28u && len && !nfc_transaction &&
        !(memAddr >= EE_OFFS_BRAND && memAddr + len <= EE_OFFS_DMX_PORT_MODE) &&
        !(memAddr == EE_OFFS_VERSION && len == 1))
    {
        ee_nfc_meta_t meta;
        int valid = EE_NfcRead(&meta);
        ee_last_meta_result = valid;
        if (valid < 0) return HAL_ERROR;
        if (valid > 0)
        {
            ee_last_meta_state = meta.state;
            if (meta.state == EE_NFC_REQUEST || meta.state == EE_NFC_INIT_PREPARE) return HAL_BUSY;
            bool resumed_local = meta.state == EE_NFC_LOCAL && !nfc_local_live;
            if (resumed_local)
            {
                /* Nuova azione locale esplicita: non riprende una richiesta NFC.
                   Verifica l'intera configurazione prima di aprire una nuova revisione. */
                uint8_t saved[40];
                if (EE_ReadConfigSnapshot(saved) != HAL_OK) return HAL_ERROR;
            }
            uint8_t old[40];
            uint16_t count = len < 0x28u - memAddr ? len : 0x28u - memAddr;
            if (EE_Read(memAddr, old, count) != HAL_OK) return HAL_ERROR;
            if (resumed_local || (memcmp(old, buf, count) &&
                (meta.state == EE_NFC_COMMITTED || meta.state == EE_NFC_INIT_TAG)))
            {
                meta.revision++;
                if (!meta.revision) meta.revision = 1;
                /* Un edit locale annulla il consenso Init tag pendente: mai pubblicare
                   in seguito default precedenti sopra la nuova configurazione. */
                meta.state = EE_NFC_LOCAL;
                if (EE_NfcStore(&meta) != HAL_OK) return HAL_ERROR;
                nfc_local_live = true;
            }
        }
    }

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
        uint8_t verify[8];
        st = EE_Read((uint16_t)(memAddr + off), verify, chunk);
        if (st != HAL_OK) return st;
        if (memcmp(verify, buf + off, chunk) != 0) return HAL_ERROR;

        off = (uint16_t)(off + chunk);
    }
    if (memAddr < 0x28u && nfc_local_live && !nfc_transaction) nfc_local_at = HAL_GetTick();
    return HAL_OK;
}

HAL_StatusTypeDef EE_Write(uint16_t memAddr, const uint8_t* buf, uint16_t len)
{
    HAL_StatusTypeDef status = EE_WriteImpl(memAddr, buf, len);
    ee_last_write_address = memAddr;
    ee_last_write_length = len;
    ee_last_write_status = status;
    ++ee_write_attempts;
    return status;
}

void EE_FormatDiagnostics(char *out, size_t capacity)
{
    snprintf(out, capacity, "ee writes=%lu addr=%02X len=%u rc=%u reserved=%u transaction=%u local=%u meta_rc=%d meta=%02X\r\n",
             (unsigned long)ee_write_attempts, ee_last_write_address, ee_last_write_length,
             ee_last_write_status, ee_nfc_bus_reserved, nfc_transaction, nfc_local_live,
             ee_last_meta_result, ee_last_meta_state);
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

/* Decodifica legacy, senza dipendere da allineamento o layout di struct C. */
static uint16_t EE_U16(const uint8_t *b)
{
    return (uint16_t)((uint16_t)b[0] << 8 | b[1]);
}

static uint32_t EE_U32(const uint8_t *b)
{
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}

static void EE_Put16(uint8_t *b, uint16_t v)
{
    b[0] = v >> 8;
    b[1] = v;
}

static void EE_Put32(uint8_t *b, uint32_t v)
{
    b[0] = v >> 24;
    b[1] = v >> 16;
    b[2] = v >> 8;
    b[3] = v;
}

/* Scrive soltanto i tre metadati, senza toccare magic o payload legacy.
   Versione invalidata prima e commessa per ultima; ogni scrittura ha read-back.
   Un'interruzione non modifica i parametri installati. */
static HAL_StatusTypeDef EE_StoreIdentity(uint8_t new_brand, uint8_t require_code)
{
    if (!Brand_IsIdentityValid(new_brand, require_code)) return HAL_ERROR;
    HAL_StatusTypeDef st = EE_WriteU8(EE_OFFS_VERSION, EE_VERSION_PENDING);
    if (st != HAL_OK) return st;
    /* Verso Generic scrive prima OFF: nessuna coppia pendente Generic/ON.
       Dagli altri brand scrive prima l'ID, così anche il percorso inverso resta valido. */
    const uint8_t offsets[2] = {EE_OFFS_BRAND, EE_OFFS_MENU_CODE};
    const uint8_t values[2] = {(uint8_t)(EE_BRAND_TAG | new_brand),
                               (uint8_t)(EE_MENU_CODE_TAG | require_code)};
    unsigned first = new_brand == BRAND_GENERIC ? 1u : 0u;
    for (unsigned step = 0; step < 2; ++step)
    {
        unsigned index = step ^ first;
        st = EE_WriteU8(offsets[index], values[index]);
        if (st != HAL_OK) return st;
    }
    return EE_WriteU8(EE_OFFS_VERSION, EE_CFG_VERSION);
}

/* Classifica la tupla, mai il solo byte 0x02. I valori marcati incoerenti
   sono rifiutati, non corretti sostituendo la configurazione con default. */
static HAL_StatusTypeDef EE_LoadIdentity(uint8_t *b, uint8_t *id, uint8_t *code)
{
    bool brand_tag = (b[EE_OFFS_BRAND] & 0xFCu) == EE_BRAND_TAG;
    bool code_tag = (b[EE_OFFS_MENU_CODE] & 0xFEu) == EE_MENU_CODE_TAG;
    if (brand_tag && code_tag)
    {
        *id = b[EE_OFFS_BRAND] & 3u;
        *code = b[EE_OFFS_MENU_CODE] & 1u;
        if (!Brand_IsIdentityValid(*id, *code)) return HAL_ERROR;
        if (b[EE_OFFS_VERSION] == EE_CFG_VERSION) return HAL_OK;
        uint8_t version = b[EE_OFFS_VERSION];
        if (version == EE_IDENTITY_VERSION || version == 0)
        {
            /* La v2 non definiva 0x27: IN esplicito, mai interpretare spazzatura come OUT. */
            HAL_StatusTypeDef st = EE_WriteU8(EE_OFFS_DMX_PORT_MODE, EE_DMX_PORT_TAG | DMX_PORT_IN);
            if (st != HAL_OK) return st;
            return EE_WriteU8(EE_OFFS_VERSION, EE_CFG_VERSION);
        }
        if (version != EE_VERSION_PENDING) return HAL_ERROR;
        return EE_WriteU8(EE_OFFS_VERSION, EE_CFG_VERSION);
    }
    if ((b[EE_OFFS_VERSION] == EE_CFG_VERSION || b[EE_OFFS_VERSION] == EE_IDENTITY_VERSION) && (brand_tag || code_tag)) return HAL_ERROR;
    /* Layout/magic legacy validati: Videlio con codice, come i dispositivi installati. */
    *id = BRAND_VIDELIO;
    *code = Brand_GetInfo(BRAND_VIDELIO)->menu_code_default;
    HAL_StatusTypeDef st = EE_WriteU8(EE_OFFS_DMX_PORT_MODE, EE_DMX_PORT_TAG | DMX_PORT_IN);
    if (st != HAL_OK) return st;
    return EE_StoreIdentity(*id, *code);
}

/* Stessi range legacy della baseline; nessuna normalizzazione persistente. */
static bool EE_LegacyValid(const uint8_t *b)
{
    return !(EE_U16(b) != EE_INIT_VALUE || b[EE_OFFS_USE_DHCP] > 1 ||
        EE_U16(b + EE_OFFS_DMX_ADDR) < 1 || EE_U16(b + EE_OFFS_DMX_ADDR) > 512 ||
        b[EE_OFFS_DMX_MODE] > 3 || EE_U16(b + EE_OFFS_ARTNET_UNI) > 32767 ||
        EE_U16(b + EE_OFFS_SACN_UNI) < 1 || EE_U16(b + EE_OFFS_SACN_UNI) > 63999 ||
        b[EE_OFFS_SACN_TRANSPORT] > 1 || b[EE_OFFS_SACN_MIN_PRIO] > 200);
}

/* Lettura verificata e migrazione dei soli metadati; pubblicazione RAM dopo successo. */
HAL_StatusTypeDef EE_LoadConfig(void)
{
    uint8_t b[EE_OFFS_MENU_CODE + 1];
    HAL_StatusTypeDef st = EE_Read(0, b, sizeof b);
    if (st != HAL_OK) return st;
    if (!EE_LegacyValid(b)) return HAL_ERROR;
    uint8_t loaded_brand, loaded_code;
    st = EE_LoadIdentity(b, &loaded_brand, &loaded_code);
    if (st != HAL_OK) return st;
    uint8_t port;
    st = EE_Read(EE_OFFS_DMX_PORT_MODE, &port, 1);
    if (st != HAL_OK) return st;
    if (port < EE_DMX_PORT_TAG || port > (EE_DMX_PORT_TAG | DMX_PORT_SACN_NODE)) return HAL_ERROR;
    dmx_port_mode = port & 3u;
    brand = loaded_brand;
    menu_code_required = loaded_code;
    use_dhcp = b[EE_OFFS_USE_DHCP];
    ip_address = EE_U32(b + EE_OFFS_IP);
    net_mask = EE_U32(b + EE_OFFS_NETMASK);
    gateway = EE_U32(b + EE_OFFS_GATEWAY);
    dns1 = EE_U32(b + EE_OFFS_DNS1);
    dns2 = EE_U32(b + EE_OFFS_DNS2);
    dmx_address = EE_U16(b + EE_OFFS_DMX_ADDR);
    dmx_mode = b[EE_OFFS_DMX_MODE];
    artnet_universe = EE_U16(b + EE_OFFS_ARTNET_UNI);
    sacn_universe = EE_U16(b + EE_OFFS_SACN_UNI);
    sacn_transport = b[EE_OFFS_SACN_TRANSPORT];
    sacn_min_prio = b[EE_OFFS_SACN_MIN_PRIO];
    lcd_backlight = b[EE_OFFS_LCD_BACKLIGHT];
    /* Estensioni storiche assenti: fallback SOLO RAM, nessuna riscrittura. */
    off_timeout_s = b[EE_OFFS_OFF_TIMEOUT_S] <= 30 ? b[EE_OFFS_OFF_TIMEOUT_S] : CFG_DEFAULT_OFF_TIMEOUT_S;
    proto_priority = b[EE_OFFS_PROTO_PRIORITY] <= 5 ? b[EE_OFFS_PROTO_PRIORITY] : CFG_DEFAULT_PROTO_PRIORITY;
    active_proto = dmx_mode <= 2 ? dmx_mode : 0;
    g_netmask_bad = !use_dhcp && net_mask == 0;
    return HAL_OK;
}

/* Mantiene il gap legacy e i metadati correnti; salva i parametri esistenti.
   Il formato non è atomico a power-cut: nessun nuovo slot autorizzato. */
static HAL_StatusTypeDef EE_StoreConfig(bool defaults, bool preserve_identity)
{
    uint8_t b[EE_OFFS_DMX_PORT_MODE + 1], verify[sizeof b];
    HAL_StatusTypeDef st = EE_Read(0, b, sizeof b);
    if (st != HAL_OK) return st;
    if (!defaults)
    {
        uint8_t identity[2];
        if (EE_U16(b) != EE_INIT_VALUE || b[EE_OFFS_VERSION] != EE_CFG_VERSION) return HAL_ERROR;
        st = EE_Read(EE_OFFS_BRAND, identity, sizeof identity);
        if (st != HAL_OK) return st;
        if (identity[0] != (EE_BRAND_TAG | brand) || identity[1] != (EE_MENU_CODE_TAG | menu_code_required)) return HAL_ERROR;
    }
    else
    {
        if (preserve_identity)
        {
            uint8_t stored_brand, stored_code;
            st = EE_ReadIdentity(&stored_brand, &stored_code);
            if (st != HAL_OK) return st;
        }
        /* Reset esplicito: invalida magic prima di cambiare i default. */
        st = EE_WriteU16(EE_OFFS_INIT, 0);
        if (st != HAL_OK) return st;
    }
    EE_Put16(b, EE_INIT_VALUE);
    b[EE_OFFS_USE_DHCP] = defaults ? CFG_DEFAULT_USE_DHCP : use_dhcp;
    EE_Put32(b + EE_OFFS_IP, defaults ? CFG_DEFAULT_IP : ip_address);
    EE_Put32(b + EE_OFFS_NETMASK, defaults ? CFG_DEFAULT_NETMASK : net_mask);
    EE_Put32(b + EE_OFFS_GATEWAY, defaults ? CFG_DEFAULT_GATEWAY : gateway);
    EE_Put32(b + EE_OFFS_DNS1, defaults ? CFG_DEFAULT_DNS1 : dns1);
    EE_Put32(b + EE_OFFS_DNS2, defaults ? CFG_DEFAULT_DNS2 : dns2);
    EE_Put16(b + EE_OFFS_DMX_ADDR, defaults ? CFG_DEFAULT_DMX_ADDR : dmx_address);
    b[EE_OFFS_DMX_MODE] = defaults ? CFG_DEFAULT_DMX_MODE : dmx_mode;
    EE_Put16(b + EE_OFFS_ARTNET_UNI, defaults ? CFG_DEFAULT_ARTNET_UNI : artnet_universe);
    EE_Put16(b + EE_OFFS_SACN_UNI, defaults ? CFG_DEFAULT_SACN_UNI : sacn_universe);
    b[EE_OFFS_SACN_TRANSPORT] = defaults ? CFG_DEFAULT_SACN_TRANSPORT : sacn_transport;
    b[EE_OFFS_SACN_MIN_PRIO] = defaults ? CFG_DEFAULT_SACN_MIN_PRIO : sacn_min_prio;
    b[EE_OFFS_LCD_BACKLIGHT] = defaults ? CFG_DEFAULT_LCD_BL : lcd_backlight;
    b[EE_OFFS_OFF_TIMEOUT_S] = defaults ? CFG_DEFAULT_OFF_TIMEOUT_S : off_timeout_s;
    b[EE_OFFS_PROTO_PRIORITY] = defaults ? CFG_DEFAULT_PROTO_PRIORITY : proto_priority;
    b[EE_OFFS_DMX_PORT_MODE] = EE_DMX_PORT_TAG | (defaults ? DMX_PORT_IN : dmx_port_mode);
    /* Non riscrive versione, gap o identità di fabbrica nel save ordinario. */
    for (unsigned i = 0; i < sizeof b; ++i)
    {
        if (i == EE_OFFS_VERSION || i == 0x1B || i == EE_OFFS_BRAND || i == EE_OFFS_MENU_CODE || (defaults && i < 2)) continue;
        uint8_t old;
        st = EE_Read(i, &old, 1);
        if (st != HAL_OK) return st;
        if (old == b[i]) continue;
        st = EE_Write(i, b + i, 1);
        if (st != HAL_OK) return st;
    }
    if (defaults)
    {
        if (!preserve_identity)
        {
            st = EE_StoreIdentity(BRAND_AXN, Brand_GetInfo(BRAND_AXN)->menu_code_default);
            if (st != HAL_OK) return st;
            b[EE_OFFS_VERSION] = EE_CFG_VERSION;
            b[EE_OFFS_BRAND] = EE_BRAND_TAG | BRAND_AXN;
            b[EE_OFFS_MENU_CODE] = EE_MENU_CODE_TAG | Brand_GetInfo(BRAND_AXN)->menu_code_default;
        }
        st = EE_WriteU16(EE_OFFS_INIT, EE_INIT_VALUE);
        if (st != HAL_OK) return st;
    }
    st = EE_Read(0, verify, sizeof verify);
    if (st != HAL_OK) return st;
    if (memcmp(b, verify, sizeof b)) return HAL_ERROR;
    if (defaults && nfc_local_live && !nfc_transaction)
    {
        /* Reset esplicito verificato: commette la revisione prima del reboot. */
        ee_nfc_meta_t meta;
        if (EE_NfcRead(&meta) != 1 || meta.state != EE_NFC_LOCAL) return HAL_ERROR;
        meta.state = EE_NFC_COMMITTED;
        if (EE_NfcStore(&meta) != HAL_OK) return HAL_ERROR;
    }
    return HAL_OK;
}

/* Solo factory reset esplicito: nessun richiamo automatico dal load. */
HAL_StatusTypeDef EE_InitMemory(void)
{
    return EE_StoreConfig(true, false);
}

HAL_StatusTypeDef EE_ResetUserMemory(void)
{
    return EE_StoreConfig(true, true);
}

HAL_StatusTypeDef EE_ReadIdentity(uint8_t *stored_brand, uint8_t *require_code)
{
    if (!stored_brand || !require_code) return HAL_ERROR;
    uint8_t b[EE_OFFS_MENU_CODE + 1];
    HAL_StatusTypeDef st = EE_Read(0, b, sizeof b);
    if (st != HAL_OK) return st;
    if (!EE_LegacyValid(b) || b[EE_OFFS_VERSION] != EE_CFG_VERSION ||
        (b[EE_OFFS_BRAND] & 0xFCu) != EE_BRAND_TAG ||
        (b[EE_OFFS_MENU_CODE] & 0xFEu) != EE_MENU_CODE_TAG ||
        !Brand_IsIdentityValid(b[EE_OFFS_BRAND] & 3u, b[EE_OFFS_MENU_CODE] & 1u)) return HAL_ERROR;
    *stored_brand = b[EE_OFFS_BRAND] & 3u;
    *require_code = b[EE_OFFS_MENU_CODE] & 1u;
    return HAL_OK;
}

/* Valida configurazione e metadati presenti prima di consentire modifiche.
   Un save identico non consuma cicli EEPROM. */
HAL_StatusTypeDef EE_SaveIdentity(uint8_t new_brand, uint8_t require_code)
{
    if (!Brand_IsIdentityValid(new_brand, require_code)) return HAL_ERROR;
    uint8_t b[EE_OFFS_MENU_CODE + 1];
    uint8_t stored_brand, stored_code;
    HAL_StatusTypeDef st = EE_Read(0, b, sizeof b);
    if (st != HAL_OK) return st;
    if (!EE_LegacyValid(b)) return HAL_ERROR;
    st = EE_LoadIdentity(b, &stored_brand, &stored_code);
    if (st != HAL_OK) return st;
    if (stored_brand != new_brand || stored_code != require_code)
    {
        st = EE_StoreIdentity(new_brand, require_code);
        if (st != HAL_OK) return st;
    }
    /* Come RSH7: i descrittori attivi cambiano soltanto al riavvio. */
    return HAL_OK;
}

HAL_StatusTypeDef EE_SaveConfig(void)
{
    if (dmx_port_mode > DMX_PORT_SACN_NODE || !Brand_IsIdentityValid(brand, menu_code_required) || use_dhcp > 1 || dmx_address < 1 || dmx_address > 512 || dmx_mode > 3 ||
        artnet_universe > 32767 || sacn_universe < 1 || sacn_universe > 63999 ||
        sacn_transport > 1 || sacn_min_prio > 200 || off_timeout_s > 30 || proto_priority > 5) return HAL_ERROR;
    return EE_StoreConfig(false, false);
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

/* Metadati NFC indipendenti dalla versione/mappa legacy. Nessuna scrittura su lettura. */
int EE_NfcRead(ee_nfc_meta_t *meta)
{
    uint8_t b[16];
    if (!meta || EE_Read(0x28, b, sizeof b) != HAL_OK) return -1;
    if (b[0] != 'N' || b[1] != 'F') return 0;
    if (b[2] != 1 || (b[3] != EE_NFC_COMMITTED && b[3] != EE_NFC_REQUEST && b[3] != EE_NFC_LOCAL && b[3] != EE_NFC_INIT_PREPARE && b[3] != EE_NFC_INIT_TAG)) return -1;
    meta->state = b[3];
    meta->revision = EE_U32(b + 4);
    meta->request = EE_U32(b + 8);
    meta->crc = EE_U32(b + 12);
    return meta->revision ? 1 : -1;
}

/* Invalida prima, scrive corpo/readback, sigilla per ultimo. Un taglio ambiguo
   lascia NF riconoscibile ma non valido: nessuna inizializzazione automatica. */
HAL_StatusTypeDef EE_NfcStore(const ee_nfc_meta_t *meta)
{
    uint8_t b[16] = {'N', 'F', 1, 0};
    if (!meta || !meta->revision || (meta->state != EE_NFC_COMMITTED &&
        meta->state != EE_NFC_REQUEST && meta->state != EE_NFC_LOCAL &&
        meta->state != EE_NFC_INIT_PREPARE && meta->state != EE_NFC_INIT_TAG)) return HAL_ERROR;
    EE_Put32(b + 4, meta->revision);
    EE_Put32(b + 8, meta->request);
    EE_Put32(b + 12, meta->crc);
    if (EE_WriteU8(0x2B, 0) != HAL_OK || EE_Write(0x28, b, sizeof b) != HAL_OK ||
        EE_WriteU8(0x2B, meta->state) != HAL_OK) return HAL_ERROR;
    ee_nfc_meta_t verify;
    if (EE_NfcRead(&verify) != 1 || verify.state != meta->state || verify.revision != meta->revision ||
        verify.request != meta->request || verify.crc != meta->crc) return HAL_ERROR;
    if (meta->state == EE_NFC_COMMITTED) nfc_local_live = false;
    return HAL_OK;
}

void EE_NfcTransaction(bool active)
{
    nfc_transaction = active;
}

bool EE_NfcLocalLive(void)
{
    return nfc_local_live;
}

/* Legge soltanto una configurazione già commessa: mai migrazione o pubblicazione RAM. */
HAL_StatusTypeDef EE_ReadConfigSnapshot(uint8_t b[40])
{
    uint8_t stored_brand, code;
    if (!b || EE_Read(0, b, 40) != HAL_OK || !EE_LegacyValid(b) || b[2] != EE_CFG_VERSION ||
        (b[EE_OFFS_DMX_PORT_MODE] & 0xFCu) != EE_DMX_PORT_TAG || (b[EE_OFFS_DMX_PORT_MODE] & 3u) > 2 ||
        EE_ReadIdentity(&stored_brand, &code) != HAL_OK) return HAL_ERROR;
    return HAL_OK;
}

uint32_t EE_NfcLocalAt(void)
{
    return nfc_local_at;
}

/* Una transizione di solo stato non riscrive corpo, revisione o ID richiesta. */
HAL_StatusTypeDef EE_NfcSeal(const ee_nfc_meta_t *expected, uint8_t state)
{
    ee_nfc_meta_t current;
    if (!expected || (state != EE_NFC_COMMITTED && state != EE_NFC_INIT_TAG) ||
        EE_NfcRead(&current) != 1 || current.state != expected->state ||
        current.revision != expected->revision || current.request != expected->request ||
        current.crc != expected->crc) return HAL_ERROR;
    if (EE_WriteU8(0x2B, state) != HAL_OK || EE_NfcRead(&current) != 1 || current.state != state) return HAL_ERROR;
    return HAL_OK;
}

/* Solo Init esplicito: prepara prima dei default, abilita il tag solo dopo
   readback della configurazione. Un PREPARE interrotto non autorizza il boot. */
HAL_StatusTypeDef EE_InitWithNfc(bool preserve_identity)
{
    ee_nfc_meta_t meta = {0};
    int valid = EE_NfcRead(&meta);
    if (valid < 0) return HAL_ERROR;
    meta.revision++;
    if (!meta.revision) meta.revision = 1;
    meta.state = EE_NFC_INIT_PREPARE;
    if (EE_NfcStore(&meta) != HAL_OK) return HAL_ERROR;
    nfc_local_live = false;
    EE_NfcTransaction(true);
    HAL_StatusTypeDef st = preserve_identity ? EE_ResetUserMemory() : EE_InitMemory();
    EE_NfcTransaction(false);
    if (st != HAL_OK) return st;
    return EE_NfcSeal(&meta, EE_NFC_INIT_TAG);
}
