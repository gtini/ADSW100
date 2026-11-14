#include "h_files.h"
#include "nfc_i2c.h"
#include <string.h>

/* ================== Costanti di base / Mappa ================== */

#define NFC_PAGE_SIZE_4B          4u     /* pagina vista lato NFC */
#define NFC_BLOCK_SIZE_16B        16u    /* blocco I2C = 4 pagine */
#define NFC_FIRST_USER_PAGE       0x04u  /* 0x04.. user area T2T */
#define NFC_FIRST_USER_BLOCK      (NFC_FIRST_USER_PAGE >> 2)

/* MIME della tua app */
#define NFC_MIME_STR              "application/vnd.FGM.adsw100.cfg"
#define NFC_MIME_LEN              ((uint8_t)(sizeof(NFC_MIME_STR)-1))

/* Page (4B) -> Block (16B) mapping */
#define NFC_PAGE_TO_BLOCK(p)     ((uint8_t)((p) >> 2))          /* 4 pagine per blocco */
#define NFC_PAGE_OFFSET(p)       ((uint8_t)(((p) & 0x03) * 4))  /* offset 0,4,8,12 */

/* Handle I2C */
extern I2C_HandleTypeDef hi2c1;

/* ================== Stato runtime ================== */

static uint8_t s_nfc_addr = NFC_I2C_ADDR_DEFAULT_A;
static volatile bool g_nfc_busy = false;

static inline uint8_t  NFC_I2C_GetAddr7(void)     { return s_nfc_addr; }
static inline bool     NFC_IsBusy(void)           { return g_nfc_busy; }
void NFC_I2C_SetRuntimeAddress(uint8_t a)         { s_nfc_addr = a; }
uint8_t NFC_I2C_GetRuntimeAddress(void)           { return s_nfc_addr; }

/* ================== Helper I2C semplici ================== */

static bool read_block16(I2C_HandleTypeDef *hi2c, uint8_t block, uint8_t out16[16])
{
    return HAL_I2C_Mem_Read(hi2c, (uint16_t)(NFC_I2C_GetAddr7()<<1),
                            block, I2C_MEMADD_SIZE_8BIT,
                            out16, 16, NFC_I2C_TIMEOUT_MS) == HAL_OK;
}

/* write di UNA pagina (4B) alla volta: più compatibile sui tag “capricciosi” */
static bool ntag_write_page4(I2C_HandleTypeDef *hi2c, uint8_t page, const uint8_t data[4])
{
    uint8_t rd[4];
    if (HAL_I2C_Mem_Write(hi2c, (uint16_t)(NFC_I2C_GetAddr7()<<1),
                          page, I2C_MEMADD_SIZE_8BIT,
                          (uint8_t*)data, 4, NFC_I2C_TIMEOUT_MS) != HAL_OK)
        return false;

    HAL_Delay(5);
    if (HAL_I2C_Mem_Read(hi2c, (uint16_t)(NFC_I2C_GetAddr7()<<1),
                         page, I2C_MEMADD_SIZE_8BIT,
                         rd, 4, NFC_I2C_TIMEOUT_MS) != HAL_OK)
        return false;

    return (memcmp(rd, data, 4) == 0);
}

/* Session-regs (best-effort, non tutti i tag li espongono) */
static void debug_read_session_regs(I2C_HandleTypeDef *hi2c)
{
    uint8_t sreg[16] = {0}; // block FEh mirroring session regs (prime 8 byte significativi)
    if (HAL_I2C_Mem_Read(hi2c,
                         (uint16_t)(NFC_I2C_GetAddr7()<<1),
                         0xFE, I2C_MEMADD_SIZE_8BIT,
                         sreg, sizeof sreg, NFC_I2C_TIMEOUT_MS) == HAL_OK)
    {
        // Stampo i primi 8 byte (quelli documentati come session bytes)
        USB_Printf("NFC: SREG FEh[0..7] =");
        for (int i=0;i<8;i++) USB_Printf(" %02X", sreg[i]);
        USB_Printf("\r\n");
    }
    else
    {
        USB_Printf("NFC: session FEh read fail\r\n");
    }
}

/* Legge 4B di una NFC page usando Read(16B)+slice */
static bool ntag_read_page4(I2C_HandleTypeDef *hi2c, uint8_t page, uint8_t out4[4])
{
    uint8_t blk = NFC_PAGE_TO_BLOCK(page);
    uint8_t off = NFC_PAGE_OFFSET(page);
    uint8_t buf[16];

    if (HAL_I2C_Mem_Read(hi2c, (uint16_t)(NFC_I2C_GetAddr7()<<1),
                         blk, I2C_MEMADD_SIZE_8BIT, buf, sizeof buf, NFC_I2C_TIMEOUT_MS) != HAL_OK)
        return false;

    memcpy(out4, &buf[off], 4);
    return true;
}

/* ---- DISCOVERY ROBUSTO -------------------------------- */

/* Verifica minima ma robusta: UID NXP (0x04) + BCC0 corretto.
   Accetta il tag anche se la CC in page 0x03 NON è ancora standardizzata. */
static bool ntag_probe_by_uid(I2C_HandleTypeDef *hi2c, uint8_t a7)
{
    uint8_t b0[16];
    if (HAL_I2C_Mem_Read(hi2c, (uint16_t)(a7<<1),
                         0x00, I2C_MEMADD_SIZE_8BIT,
                         b0, sizeof b0, NFC_I2C_TIMEOUT_MS) != HAL_OK)
        return false;

    /* UID parte 1: b0[0..2], BCC0=b0[3], UID parte 2: b0[4..7]
       Manufacturer NXP tipico: UID0==0x04. */
    if (b0[0] != 0x04) return false;

    const uint8_t bcc0_calc = (uint8_t)~(0x88 ^ b0[0] ^ b0[1] ^ b0[2]);
    if (bcc0_calc != b0[3]) return false;

    return true;
}

bool NFC_I2C_InitQuickReaddress(I2C_HandleTypeDef *hi2c)
{
    /* 1) Prova indirizzi tipici NT3H2x11:
     * - 0x02: indirizzo raccomandato dal datasheet NXP (write = 0x04)
     * - 0x55: default NT3H2211
     * - 0x57: altra variante
     * - 0x59: eventuale indirizzo custom
     */
    const uint8_t preferiti[] = {
    #ifdef NFC_I2C_ADDR_PREFERRED
        NFC_I2C_ADDR_PREFERRED,   // es. 0x59 se definito
    #endif
        0x02,
        0x55,
        0x57,
        0x59
    };
    const unsigned n_pref = sizeof(preferiti) / sizeof(preferiti[0]);

    for (unsigned i = 0; i < n_pref; ++i) {
        uint8_t a = preferiti[i];
        if (a == 0x53) continue; /* mai l'EEPROM */

        if (HAL_I2C_IsDeviceReady(hi2c, (uint16_t)(a << 1), 1, NFC_I2C_TIMEOUT_MS) != HAL_OK)
            continue;

        if (ntag_probe_by_uid(hi2c, a)) {
            NFC_I2C_SetRuntimeAddress(a);
            USB_Printf("NFC: NTAG at 0x%02X (preferred scan)\r\n", a);
            return true;
        }
    }

    /* 2) Scan ampio: 0x02..0x77, escluso 0x53 (EEPROM) */
    for (uint8_t a = 0x02; a <= 0x77; ++a) {
        if (a == 0x53) continue; /* escludi EEPROM */

        if (HAL_I2C_IsDeviceReady(hi2c, (uint16_t)(a << 1), 1, NFC_I2C_TIMEOUT_MS) != HAL_OK)
            continue;

        if (ntag_probe_by_uid(hi2c, a)) {
            NFC_I2C_SetRuntimeAddress(a);
            USB_Printf("NFC: NTAG at 0x%02X (broad scan)\r\n", a);
            return true;
        }
    }

    USB_Printf("NFC: NTAG not detected (UID/BCC0 filter)\r\n");
    return false;
}

/* ================== API di lettura semplici (usate da main) ================== */

bool NFC_I2C_ReadPage16(I2C_HandleTypeDef *hi2c, uint8_t start_block, uint8_t out16[16])
{
    return read_block16(hi2c, start_block, out16);
}

bool NFC_I2C_ReadBlock4(I2C_HandleTypeDef *hi2c, uint8_t page, uint8_t out4[4])
{
    return ntag_read_page4(hi2c, page, out4);
}

bool NFC_I2C_ReadUID(I2C_HandleTypeDef *hi2c, uint8_t uid7[7])
{
    if (!uid7) return false;
    uint8_t p0[16];
    if (!read_block16(hi2c, 0x00, p0)) return false;

    uid7[0]=p0[0]; uid7[1]=p0[1]; uid7[2]=p0[2];
    uid7[3]=p0[4]; uid7[4]=p0[5]; uid7[5]=p0[6]; uid7[6]=p0[7];

    const uint8_t bcc0_calc = (uint8_t)~(0x88 ^ uid7[0] ^ uid7[1] ^ uid7[2]);
    if (bcc0_calc != p0[3]) {
        USB_Printf("NFC: BCC0 mismatch (calc=%02X read=%02X)\r\n", bcc0_calc, p0[3]);
    }
    return true;
}

bool NFC_I2C_DumpFirstBlocks(I2C_HandleTypeDef *hi2c)
{
    uint8_t b[4];
    for (uint8_t page = 0; page < 4; page++) {
        if (!ntag_read_page4(hi2c, page, b)) {
            USB_Printf("NFC: read fail @page 0x%02X\r\n", page);
            return false;
        }
        USB_Printf("PAGE %02u: %02X %02X %02X %02X\r\n", page, b[0], b[1], b[2], b[3]);
    }
    return true;
}

/* ================== Lettura NDEF (estrazione JSON) ================== */

bool ndef_get_cfg_json(I2C_HandleTypeDef *hi2c, char *out_json, uint16_t out_sz)
{
    if (!out_json || out_sz<16) return false;
    memset(out_json, 0, out_sz);

    /* Leggi 1 KB di user area, 16B per volta, da block=1..64 */
    uint8_t buf[1024];
    uint32_t pos = 0;
    for (uint8_t blk = NFC_FIRST_USER_BLOCK; blk < NFC_FIRST_USER_BLOCK+64 && pos+16 <= sizeof(buf); ++blk) {
        if (!read_block16(hi2c, blk, &buf[pos])) break;
        pos += 16;
    }

    uint8_t *p = buf, *end = buf+pos;
    while (p < end) {
        if (*p == 0xFE) break;                 /* terminatore TLV */
        if (*p != 0x03) { ++p; continue; }     /* non-NDEF */
        ++p;

        uint32_t ndef_len = 0;
        if (p>=end) return false;
        if (*p == 0xFF) { if (p+2>=end) return false; ndef_len = ((uint32_t)p[1]<<8)|p[2]; p+=3; }
        else             { ndef_len = *p++; }

        if (p+ndef_len > end) return false;

        uint8_t *q = p, *qend = p+ndef_len;
        if (q>=qend) { p=qend; continue; }

        const uint8_t hdr = *q++;
        const uint8_t tnf = (hdr & 0x07);
        const bool sr = (hdr & 0x10)!=0, il = (hdr & 0x08)!=0;
        if (tnf != 0x02) { p=qend; continue; }  /* non MIME */

        if (q>=qend) { p=qend; continue; }
        uint8_t type_len = *q++;

        uint32_t payload_len = 0;
        if (sr) { if (q>=qend){p=qend;continue;} payload_len = *q++; }
        else    { if (q+3>=qend){p=qend;continue;} payload_len = ((uint32_t)q[0]<<24)|((uint32_t)q[1]<<16)|((uint32_t)q[2]<<8)|q[3]; q+=4; }

        if (il) { if (q>=qend){p=qend;continue;} uint8_t idlen=*q++; if (q+idlen>qend){p=qend;continue;} q+=idlen; }
        if (q+type_len > qend) { p=qend; continue; }

        USB_Printf("NFC: MIME raw=\"%.*s\" (len=%u)\r\n", (int)type_len, (const char*)q, type_len);

        bool ours = false;
        if (type_len == NFC_MIME_LEN && memcmp(q, NFC_MIME_STR, NFC_MIME_LEN)==0) {
            ours = true;
        } else {
            /* tollera “application” puro o “application\0vnd.FGM...” (alcune app scrivono così) */
            static const char APP[] = "application";
            size_t appn = sizeof(APP)-1;
            if (type_len >= appn && memcmp(q, APP, appn)==0) {
                if (type_len == appn) {
                    ours = true;
                } else if (q[appn]==0x00) {
                    const char *sub = "vnd.FGM.adsw100.cfg";
                    size_t subn = strlen(sub);
                    if (type_len == appn+1+subn && memcmp(&q[appn+1], sub, subn)==0) ours = true;
                }
            }
        }

        if (!ours) { p = qend; continue; }

        q += type_len;
        if (q+payload_len > qend) return false;

        const uint32_t copy = (payload_len < (out_sz-1)) ? payload_len : (out_sz-1);
        memcpy(out_json, q, copy);
        out_json[copy] = '\0';
        USB_Printf("NFC: JSON extracted (%lu bytes)\r\n", (unsigned long)copy);
        return true;
    }
    return false;
}

/* ================== Scrittura NDEF (MIME+JSON) ================== */
/* Sicura: NON tocca la CC; scrive il TLV a 4B/pagina da page 0x10. */

bool ndef_write_mime_json(I2C_HandleTypeDef *hi2c, const char *json)
{
    if (!json) return false;

    const uint8_t tlen = NFC_MIME_LEN;
    const uint32_t plen = (uint32_t)strlen(json);
    const bool sr = (plen <= 255);

    USB_Printf("NFC: WRITING MIME=\"%s\" (len=%u)\r\n", NFC_MIME_STR, (unsigned)tlen);

    /* Record NDEF */
    uint8_t rec[512], *p = rec;
    uint8_t hdr = 0xD2;                /* MB|ME|SR|TNF=2 (MIME) */
    if (!sr) hdr &= (uint8_t)~0x10;    /* togli SR se payload >255 */

    *p++ = hdr;
    *p++ = tlen;
    if (sr) *p++ = (uint8_t)plen;
    else {
        *p++ = (uint8_t)(plen>>24);
        *p++ = (uint8_t)(plen>>16);
        *p++ = (uint8_t)(plen>>8);
        *p++ = (uint8_t)(plen);
    }
    memcpy(p, NFC_MIME_STR, tlen); p += tlen;
    memcpy(p, json,        plen); p += plen;

    const uint32_t ndef_len = (uint32_t)(p - rec);

    /* TLV: 0x03, len, payload..., 0xFE */
    uint8_t tlv[520]; uint32_t pos=0;
    tlv[pos++] = 0x03;
    if (ndef_len < 0xFF) {
        tlv[pos++] = (uint8_t)ndef_len;
    } else {
        tlv[pos++] = 0xFF;
        tlv[pos++] = (uint8_t)(ndef_len>>8);
        tlv[pos++] = (uint8_t)(ndef_len);
    }
    memcpy(&tlv[pos], rec, ndef_len); pos += ndef_len;
    tlv[pos++] = 0xFE;

    g_nfc_busy = true;

    /* Erase “soft” delle prime 8 pagine user (0x04..0x0B) per evitare residui */
    const uint8_t zeros[4] = {0,0,0,0};
    for (uint8_t page=0x04; page<=0x0B; ++page) (void)ntag_write_page4(hi2c, page, zeros);

    /* Scrivi TLV a partire da page 0x10 (tipicamente “libera” dalle app demo) */
    uint8_t page = 0x10;
    uint32_t w = 0;
    while (w < pos) {
        uint8_t chunk[4] = {0,0,0,0};
        uint8_t c = 0;
        while (c<4 && w<pos) chunk[c++] = tlv[w++];
        if (!ntag_write_page4(hi2c, page, chunk)) {
            USB_Printf("NFC: write/verify FAIL @page 0x%02X\r\n", page);
            g_nfc_busy = false;
            return false;
        }
        page++;
    }

    g_nfc_busy = false;
    USB_Printf("NFC: NDEF write OK\r\n");
    return true;
}

/* ================== Inizializzazione “soft” (niente CC) ================== */

bool NFC_FormatIfVirgin(I2C_HandleTypeDef *hi2c)
{
    /* Se la CC è “sensata”, non facciamo nulla. */
    uint8_t p0[16];
    if (!read_block16(hi2c, 0x00, p0)) return false;

    if (p0[12] == 0xE1) {
        USB_Printf("NFC: CC already valid -> skip format\r\n");
        return true;
    }

    /* Non tocchiamo la CC per evitare lock: ci limitiamo a pulire testa user. */
    USB_Printf("NFC: CC invalid -> soft head erase only\r\n");
    const uint8_t zeros[4] = {0,0,0,0};
    for (uint8_t page=0x04; page<=0x1F; ++page) (void)ntag_write_page4(hi2c, page, zeros);
    return true;
}

/* ================== Routines chiamate dal main ================== */

void NFC_Test(void)
{
    I2C_HandleTypeDef *hi2c = &hi2c1;

    if (!NFC_I2C_InitQuickReaddress(hi2c)) {
        USB_Printf("NFC: no device, skip test\r\n");
        return;
    }
    USB_Printf("NFC: active addr 0x%02X\r\n", NFC_I2C_GetRuntimeAddress());
    debug_read_session_regs(hi2c);

    uint8_t p0[16];
    if (NFC_I2C_ReadPage16(hi2c, 0x00u, p0)) {
        USB_Printf("PAGE00..0F: ");
        for (int i=0;i<16;i++) USB_Printf("%02X ", p0[i]);
        USB_Printf("\r\n");
    }
    (void)NFC_I2C_DumpFirstBlocks(hi2c);

    uint8_t uid[7];
    if (NFC_I2C_ReadUID(hi2c, uid)) {
        USB_Printf("NFC UID: %02X-%02X-%02X-%02X-%02X-%02X-%02X\r\n",
                   uid[0],uid[1],uid[2],uid[3],uid[4],uid[5],uid[6]);
    }
}

void NFC_BootApplyIfValid(void)
{
    I2C_HandleTypeDef *hi2c = &hi2c1;

    if (EE_LoadConfig() != HAL_OK) {
        USB_Printf("EEPROM: load failed, NFC apply skipped\r\n");
        return;
    }

    if (!NFC_I2C_InitQuickReaddress(hi2c)) {
        USB_Printf("NFC: no device on I2C -> using EEPROM\r\n");
        return;
    }

    (void)NFC_FormatIfVirgin(hi2c);

    char json[768];
    if (!ndef_get_cfg_json(hi2c, json, sizeof json)) {
        USB_Printf("NFC: no valid NDEF -> using EEPROM\r\n");
        return;
    }

    uint8_t mac[6]={0}; (void)EE_GetLocalMAC48(mac);
    char magic_expected[64]; build_magic_from_eui(magic_expected, sizeof magic_expected, mac);

    char     magic_in[64];
    uint8_t  n_dmx_mode, n_net, n_subuni;
    uint16_t n_universe, n_prio;
    bool     n_dhcp;
    char     n_ip[16], n_mask[16], n_gw[16];

    if (!json_to_cfg_vars(json, magic_in, sizeof(magic_in),
                          &n_dmx_mode, &n_universe, &n_net, &n_subuni, &n_prio,
                          &n_dhcp, n_ip, n_mask, n_gw)) {
        USB_Printf("NFC: JSON parse fail -> using EEPROM\r\n");
        return;
    }

    if (strcmp(magic_in, magic_expected) != 0) {
        USB_Printf("NFC: MAGIC mismatch -> ignoring NFC\r\n");
        return;
    }

    uint8_t  g_dmx_mode, g_net, g_subuni; uint16_t g_universe, g_prio;
    bool     g_dhcp; char g_ip[16], g_mask[16], g_gw[16];
    get_runtime_vars(&g_dmx_mode, &g_universe, &g_net, &g_subuni, &g_prio,
                     &g_dhcp, g_ip, g_mask, g_gw);

    bool same =
      (n_dmx_mode==g_dmx_mode) && (n_universe==g_universe) &&
      (n_net==g_net) && (n_subuni==g_subuni) && (n_prio==g_prio) &&
      (n_dhcp==g_dhcp) &&
      (strcmp(n_ip,g_ip)==0) && (strcmp(n_mask,g_mask)==0) && (strcmp(n_gw,g_gw)==0);

    if (!same) {
        if (!apply_config_vars(n_dmx_mode, n_universe, n_net, n_subuni, n_prio,
                               n_dhcp, n_ip, n_mask, n_gw)) return;
        if (EE_SaveConfig() != HAL_OK)
        {
            USB_Printf("NFC: EEPROM save FAILED\r\n");
            return;
        }
        USB_Printf("NFC: applied & EEPROM updated from NDEF\r\n");
    } else {
        USB_Printf("NFC: NDEF == EEPROM -> no changes\r\n");
    }
}

void NFC_Service(void)
{
    if (NFC_IsBusy()) return;

    static bool     applied_once = false;
    static uint32_t last_ms = 0;
    static char     last_json[768];
    static bool     have_cache = false;

    if (!NFC_I2C_InitQuickReaddress(&hi2c1)) return;
    (void)NFC_FormatIfVirgin(&hi2c1);

    /* Unico percorso boot: magic, versione, range e stato EEPROM verificati. */
    if (!applied_once)
    {
        NFC_BootApplyIfValid();
        applied_once = true;
        return;
    }

    uint32_t now = HAL_GetTick();
    if (now - last_ms < 1000u) return;
    last_ms = now;

    uint8_t dm, net, su; uint16_t uni, pr; bool dh; char ip[16], mk[16], gw[16];
    get_runtime_vars(&dm,&uni,&net,&su,&pr,&dh, ip,mk,gw);

    uint8_t mac[6]; EE_GetLocalMAC48(mac);
    char magic[64]; build_magic_from_eui(magic, sizeof magic, mac);

    char out_json[768];
    if (!cfg_to_json_vars(dm,uni,net,su,pr,dh, ip,mk,gw, magic,
                          out_json, sizeof out_json))
        return;

    if (have_cache && strcmp(out_json, last_json)==0) return;

    if (ndef_write_mime_json(&hi2c1, out_json)) {
        strncpy(last_json, out_json, sizeof(last_json)-1);
        last_json[sizeof(last_json)-1] = '\0';
        have_cache = true;
    }
}

/* =====================================================================
 * INIT MEMORY (compatibile con chiamate esistenti)
 * - tenta di impostare la CC (E1 10 EF 00) in page 0x03 se non è già valida
 * - effettua un “head erase” dell’area utente 0x04..0x1F (4B/pagina)
 * - best effort: se qualche write fallisce, continua e ritorna true
 * ===================================================================== */
bool NFC_InitMemory(I2C_HandleTypeDef *hi2c)
{
    USB_Printf("NFC: full factory init -> CC (page 0x03) + head erase\r\n");

    /* ---- Leggi block 0 (16B) per verificare la CC a offset 12..15 ---- */
    const uint8_t cc_page03[4] = { 0xE1, 0x10, 0x6D, 0x00 }; // NT3H2111/2211
    uint8_t b0[16];

    if (HAL_I2C_Mem_Read(hi2c,
                         (uint16_t)(NFC_I2C_GetAddr7()<<1),
                         0x00, I2C_MEMADD_SIZE_8BIT,
                         b0, 16, NFC_I2C_TIMEOUT_MS) != HAL_OK)
    {
        USB_Printf("NFC: cannot read block0 (continue with head erase)\r\n");
    }
    else
    {
        const bool need_cc = (b0[12] != cc_page03[0] ||
                              b0[13] != cc_page03[1] ||
                              b0[14] != cc_page03[2] ||
                              b0[15] != cc_page03[3]);
        if (need_cc)
        {
            /* Leggi session bytes da block 0xFE, byte0 bit0 = RF_FIELD_PRESENT */
            uint8_t sreg[16] = {0};
            bool rf_present = false;
            if (HAL_I2C_Mem_Read(hi2c,
                                  (uint16_t)(NFC_I2C_GetAddr7()<<1),
                                  0xFE, I2C_MEMADD_SIZE_8BIT,
                                  sreg, sizeof sreg, 50) == HAL_OK)
            {
                rf_present = (sreg[0] & 0x01) != 0u;
            }

            if (rf_present)
            {
                USB_Printf("NFC: RF present -> skip CC write\r\n");
            }
            else
            {
                if (!ntag_write_page4(hi2c, 0x03, cc_page03))
                {
                    USB_Printf("NFC: CC write FAIL (continuo)\r\n");
                }
                else
                {
                    uint8_t vr[4] = {0};
                    if (ntag_read_page4(hi2c, 0x03, vr) &&
                        memcmp(vr, cc_page03, 4) == 0)
                    {
                        USB_Printf("NFC: CC set @page 0x03\r\n");
                    }
                    else
                    {
                        USB_Printf("NFC: CC verify FAIL (continuo)\r\n");
                    }
                }
            }
        }
        else
        {
            USB_Printf("NFC: CC already valid\r\n");
        }
    }

    /* ---- Head erase: pagine NFC 0x04..0x1F (4B/page) via ntag_write_page4 ---- */
    const uint8_t zeros[4] = {0,0,0,0};
    for (uint8_t page = 0x04; page <= 0x1F; ++page)
    {
        (void)ntag_write_page4(hi2c, page, zeros); /* best-effort */
        HAL_Delay(1);
    }

    USB_Printf("NFC: factory memory initialized\r\n");
    return true;
}
