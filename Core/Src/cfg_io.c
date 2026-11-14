#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include "lwip/ip4_addr.h"

#include "h_files.h"   // include comune del tuo progetto (porta dentro defs.h, ecc.)
#include "defs.h"      // dove sono dichiarate le tue globali

/* Stringhe statiche condivise: i protocolli non possiedono copie dei nomi. */
static const char model_nodim[] = "NODIM01";
static const brand_info_t brand_table[BRAND_COUNT] =
{
    [BRAND_AXN] =
    {
        "AXN System", model_nodim, 0
    },
    [BRAND_VIDELIO] =
    {
        "Videlio HMS", "ADSW100", 1
    },
    [BRAND_GENERIC] =
    {
        "Generic", model_nodim, 0
    }
};

uint8_t brand = BRAND_AXN;
uint8_t menu_code_required = 0;

const brand_info_t *Brand_GetInfo(uint8_t id)
{
    return id < BRAND_COUNT ? &brand_table[id] : NULL;
}

bool Brand_IsIdentityValid(uint8_t id, uint8_t require_code)
{
    return Brand_GetInfo(id) && require_code <= 1 &&
           (id != BRAND_GENERIC || require_code == 0);
}

const brand_info_t *Brand_Current(void)
{
    const brand_info_t *info = Brand_GetInfo(brand);
    return info ? info : &brand_table[BRAND_AXN];
}

/* ===========================================================
 * Helpers JSON molto semplici (no alloc, no dipendenze)
 * =========================================================== */

static const char* skip_ws(const char *p) { while (*p && (unsigned char)*p <= ' ') ++p; return p; }

/* Accetta lo schema legacy esatto, senza chiavi duplicate, escape o code.
   Evita che la ricerca compatta dei valori accetti frammenti JSON malformati. */
static bool cfg_json_object(const char **cursor, unsigned depth, uint32_t *seen)
{
    static const char *keys[] = {"magic", "ver", "data", "dmx_mode", "universe", "net", "subuni", "prio", "dhcp", "ip", "mask", "gw"};
    const char *p = skip_ws(*cursor);
    if (*p++ != '{' || depth > 1) return false;
    for (;;)
    {
        p = skip_ws(p);
        if (*p++ != '"') return false;
        const char *key = p;
        while (*p && *p != '"' && *p != '\\' && (unsigned char)*p >= 32) ++p;
        if (*p != '"') return false;
        unsigned index;
        for (index = 0; index < sizeof keys / sizeof keys[0]; ++index)
            if (strlen(keys[index]) == (size_t)(p - key) && memcmp(keys[index], key, p - key) == 0) break;
        if (index == sizeof keys / sizeof keys[0] || (*seen & (1u << index)) ||
            (depth == 0 ? index > 2 : index < 3)) return false;
        *seen |= 1u << index;
        p = skip_ws(p + 1);
        if (*p++ != ':') return false;
        p = skip_ws(p);
        if (index == 2)
        {
            if (!cfg_json_object(&p, depth + 1, seen)) return false;
        }
        else if (index == 0 || index >= 9)
        {
            if (*p++ != '"') return false;
            while (*p && *p != '"' && *p != '\\' && (unsigned char)*p >= 32) ++p;
            if (*p++ != '"') return false;
        }
        else if (index == 8)
        {
            if (strncmp(p, "true", 4) == 0) p += 4;
            else if (strncmp(p, "false", 5) == 0) p += 5;
            else return false;
        }
        else
        {
            if (!isdigit((unsigned char)*p)) return false;
            if (*p == '0' && isdigit((unsigned char)p[1])) return false;
            while (isdigit((unsigned char)*p)) ++p;
        }
        p = skip_ws(p);
        if (*p == '}') { *cursor = p + 1; return true; }
        if (*p++ != ',') return false;
    }
}

static bool cfg_json_valid(const char *json)
{
    uint32_t seen = 0;
    return cfg_json_object(&json, 0, &seen) && seen == 0xFFFu && *skip_ws(json) == 0;
}

static bool json_find_string(const char *json, const char *key, char *dst, size_t dsz) {
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *k = strstr(json, pat); if (!k) return false;
    const char *p = strchr(k + strlen(pat), ':'); if (!p) return false;
    p = skip_ws(p+1);
    if (*p != '"') return false;
    ++p;
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < dsz) dst[n++] = *p++;
    dst[n] = 0;
    return (*p == '"');
}

static bool json_find_uint(const char *json, const char *key, unsigned *out) {
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *k = strstr(json, pat); if (!k) return false;
    const char *p = strchr(k + strlen(pat), ':'); if (!p) return false;
    p = skip_ws(p+1);
    if (!isdigit((unsigned char)*p)) return false;
    unsigned v = 0;
    while (isdigit((unsigned char)*p))
    {
        unsigned digit = (unsigned)(*p - '0');
        if (v > (UINT_MAX - digit) / 10u) return false;
        v = v * 10u + digit;
        ++p;
    }
    if (*skip_ws(p) != ',' && *skip_ws(p) != '}') return false;
    *out = v;
    return true;
}

static bool json_find_bool(const char *json, const char *key, bool *out) {
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *k = strstr(json, pat); if (!k) return false;
    const char *p = strchr(k + strlen(pat), ':'); if (!p) return false;
    p = skip_ws(p+1);
    if (strncmp(p, "true", 4)  == 0) { *out = true;  return true; }
    if (strncmp(p, "false", 5) == 0) { *out = false; return true; }
    return false;
}

/* ===========================================================
 * IPv4 helpers (uint32 big-endian ⇄ stringa)
 * ip_address/net_mask/gateway sono uint32 in ordine di rete (come lwIP)
 * =========================================================== */

/* Le globali mantengono il valore raw lwIP: nessuna conversione EEPROM. */
static void ip32be_to_str(uint32_t raw, char out[16])
{
    ip4_addr_t a;
    a.addr = raw;
    ip4addr_ntoa_r(&a, out, 16);
}

/* IPv4 decimale a quattro ottetti, senza abbreviazioni o suffissi. */
static bool str_to_ip32be(const char *s, uint32_t *raw)
{
    uint8_t bytes[4];
    if (!s || !raw) return false;
    for (unsigned i = 0; i < 4; ++i)
    {
        unsigned v = 0, digits = 0;
        while (*s >= '0' && *s <= '9')
        {
            if (++digits > 3) return false;
            v = v * 10u + (unsigned)(*s++ - '0');
        }
        if (!digits || v > 255) return false;
        bytes[i] = v;
        if (i < 3 && *s++ != '.') return false;
    }
    if (*s) return false;
    ip4_addr_t a;
    IP4_ADDR(&a, bytes[0], bytes[1], bytes[2], bytes[3]);
    *raw = a.addr;
    return true;
}

/* Validazione comune prima di modificare una qualunque globale. */
static bool cfg_valid(uint8_t mode, uint16_t universe, uint8_t net, uint8_t subuni,
                      uint16_t priority, bool dhcp, const char *ip, const char *mask,
                      const char *gw, uint32_t *a, uint32_t *m, uint32_t *g)
{
    if (mode > 3 || net > 127 || priority > 200 ||
        (mode == 1 ? universe > 32767 : mode == 2 ? (universe < 1 || universe > 63999) :
         (universe < 1 || universe > 512))) return false;
    (void)subuni;
    if (!str_to_ip32be(ip, a) || !str_to_ip32be(mask, m) || !str_to_ip32be(gw, g)) return false;
    if (!dhcp)
    {
        uint32_t hostmask = lwip_ntohl(*m);
        uint32_t inverse = ~hostmask;
        uint32_t hostip = lwip_ntohl(*a);
        if (!hostmask || hostmask == UINT32_MAX || (inverse & (inverse + 1u)) ||
            !hostip || (hostip >> 24) >= 224 || (hostip >> 24) == 127 ||
            (hostip & inverse) == 0 || (hostip & inverse) == inverse) return false;
        if (*g && ((*g & *m) != (*a & *m))) return false;
    }
    return true;
}

/* ===========================================================
 * MAGIC string
 * =========================================================== */

void build_magic_from_eui(char *magic, size_t msz, const uint8_t eui[6]) {
    if (!magic || !eui || msz < 16) return;
    snprintf(magic, msz, "ADSW100-%02X%02X%02X%02X%02X%02X",
             eui[0], eui[1], eui[2], eui[3], eui[4], eui[5]);
}

/* ===========================================================
 * JSON deterministico (no CRC)
 * =========================================================== */

bool cfg_to_json_vars(uint8_t dmx_mode_val, uint16_t universe_val,
                      uint8_t net, uint8_t subuni, uint16_t prio_val,
                      bool dhcp, const char *ip, const char *mask, const char *gw,
                      const char *magic,
                      char *json, size_t jsz)
{
    if (!magic || !ip || !mask || !gw || !json || jsz < 128) return false;

    int n = snprintf(json, jsz,
        "{"
          "\"magic\":\"%s\","
          "\"ver\":1,"
          "\"data\":{"
            "\"dmx_mode\":%u,"
            "\"universe\":%u,"
            "\"net\":%u,"
            "\"subuni\":%u,"
            "\"prio\":%u,"
            "\"dhcp\":%s,"
            "\"ip\":\"%s\","
            "\"mask\":\"%s\","
            "\"gw\":\"%s\""
          "}"
        "}",
        magic,
        (unsigned)dmx_mode_val, (unsigned)universe_val,
        (unsigned)net, (unsigned)subuni, (unsigned)prio_val,
        dhcp ? "true":"false",
        ip, mask, gw
    );
    return (n > 0 && (size_t)n < jsz);
}

bool json_to_cfg_vars(const char *json,
                      char *magic_out, size_t magic_sz,
                      uint8_t *dmx_mode_out, uint16_t *universe_out,
                      uint8_t *net_out, uint8_t *subuni_out, uint16_t *prio_out,
                      bool *dhcp_out, char ip[16], char mask[16], char gw[16])
{
    if (!json || !cfg_json_valid(json) ||
        !dmx_mode_out || !universe_out || !net_out || !subuni_out || !prio_out ||
        !dhcp_out || !ip || !mask || !gw) {
        return false;
    }

    if (magic_out && magic_sz) {
        if (!json_find_string(json, "magic", magic_out, magic_sz))
            return false;
    }

    unsigned u = 0;
    if (!json_find_uint(json, "ver", &u) || u != 1) return false;

    if (!json_find_uint(json, "dmx_mode", &u) || u > 3u)
        return false;
    *dmx_mode_out = (uint8_t)u;

    if (!json_find_uint(json, "universe", &u) || u > 63999u)
        return false;
    *universe_out = (uint16_t)u;

    if (!json_find_uint(json, "net", &u) || u > 127u)
        return false;
    *net_out = (uint8_t)u;

    if (!json_find_uint(json, "subuni", &u) || u > 255u)
        return false;
    *subuni_out = (uint8_t)u;

    if (!json_find_uint(json, "prio", &u) || u > 200u)
        return false;
    *prio_out = (uint16_t)u;

    if (!json_find_bool(json, "dhcp", dhcp_out))
        return false;

    if (!json_find_string(json, "ip", ip, 16))
        return false;
    if (!json_find_string(json, "mask", mask, 16))
        return false;
    if (!json_find_string(json, "gw", gw, 16))
        return false;

    uint32_t a, m, g;
    return cfg_valid(*dmx_mode_out, *universe_out, *net_out, *subuni_out, *prio_out,
                     *dhcp_out, ip, mask, gw, &a, &m, &g);
}

/* ===========================================================
 * Bridge con le variabili globali
 * =========================================================== */

/* Prende i valori runtime (globali) e li esporta in formato generico */
void get_runtime_vars(uint8_t *dmx_mode_out, uint16_t *universe_out,
                      uint8_t *net_out, uint8_t *subuni_out, uint16_t *prio_out,
                      bool *dhcp_out, char ip[16], char mask[16], char gw[16])
{
    if (dmx_mode_out)  *dmx_mode_out  = dmx_mode;               // 0=DMX, 1=ArtNet, 2=sACN
    if (prio_out)      *prio_out      = sacn_min_prio;          // usi la prio sACN

    // Universe in base alla modalità
    if (universe_out) {
        if (dmx_mode == 1)      *universe_out = artnet_universe;
        else if (dmx_mode == 2) *universe_out = sacn_universe;
        else                    *universe_out = dmx_address;     // fallback/placeholder
    }

    if (net_out)    *net_out    = 0; // non usati per ora
    if (subuni_out) *subuni_out = 0;

    if (dhcp_out)   *dhcp_out   = (use_dhcp != 0);

    // IP/mask/gw da uint32 big-endian -> stringhe "a.b.c.d"
    ip32be_to_str(ip_address, ip);
    ip32be_to_str(net_mask,    mask);
    ip32be_to_str(gateway,     gw);
}

/* Applica valori generici alle tue globali reali */
bool apply_config_vars(uint8_t dmx_mode_in, uint16_t universe_in,
                       uint8_t net, uint8_t subuni, uint16_t prio_in,
                       bool dhcp_in, const char *ip_str, const char *mask_str, const char *gw_str)
{
    uint32_t a, m, g;
    if (!cfg_valid(dmx_mode_in, universe_in, net, subuni, prio_in, dhcp_in,
                   ip_str, mask_str, gw_str, &a, &m, &g)) return false;
    dmx_mode = dmx_mode_in;
    sacn_min_prio = (uint8_t)prio_in;
    use_dhcp = dhcp_in ? 1 : 0;
    if (dmx_mode == 1) artnet_universe = universe_in;
    else if (dmx_mode == 2) sacn_universe = universe_in;
    else dmx_address = universe_in;
    ip_address = a;
    net_mask = m;
    gateway = g;
    NET_ScheduleApply();
    return true;
}
