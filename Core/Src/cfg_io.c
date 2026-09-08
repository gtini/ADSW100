#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

#include "h_files.h"   // include comune del tuo progetto (porta dentro defs.h, ecc.)
#include "defs.h"      // dove sono dichiarate le tue globali

/* ===========================================================
 * Helpers JSON molto semplici (no alloc, no dipendenze)
 * =========================================================== */

static const char* skip_ws(const char *p) { while (*p && (unsigned char)*p <= ' ') ++p; return p; }

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
    while (isdigit((unsigned char)*p)) { v = v*10u + (unsigned)(*p - '0'); ++p; }
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

static void ip32be_to_str(uint32_t be, char out[16]) {
    // be è in ordine di rete (big-endian). Ricomponi a . dotted
    uint8_t o0 = (uint8_t)((be >> 24) & 0xFF);
    uint8_t o1 = (uint8_t)((be >> 16) & 0xFF);
    uint8_t o2 = (uint8_t)((be >>  8) & 0xFF);
    uint8_t o3 = (uint8_t)((be      ) & 0xFF);
    snprintf(out, 16, "%u.%u.%u.%u", o0, o1, o2, o3);
}

static bool str_to_ip32be(const char *s, uint32_t *out_be) {
    unsigned o0, o1, o2, o3;
    if (!s || sscanf(s, "%u.%u.%u.%u", &o0, &o1, &o2, &o3) != 4) return false;
    if (o0>255 || o1>255 || o2>255 || o3>255) return false;
    *out_be = ((uint32_t)o0 << 24) | ((uint32_t)o1 << 16) | ((uint32_t)o2 << 8) | (uint32_t)o3;
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
    if (!json ||
        !dmx_mode_out || !universe_out || !net_out || !subuni_out || !prio_out ||
        !dhcp_out || !ip || !mask || !gw) {
        return false;
    }

    if (magic_out && magic_sz) {
        if (!json_find_string(json, "magic", magic_out, magic_sz))
            return false;
    }

    unsigned u = 0;

    if (!json_find_uint(json, "dmx_mode", &u))
        return false;
    *dmx_mode_out = (uint8_t)u;

    if (!json_find_uint(json, "universe", &u))
        return false;
    *universe_out = (uint16_t)u;

    if (!json_find_uint(json, "net", &u))
        return false;
    *net_out = (uint8_t)u;

    if (!json_find_uint(json, "subuni", &u))
        return false;
    *subuni_out = (uint8_t)u;

    if (!json_find_uint(json, "prio", &u))
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

    return true;
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
void apply_config_vars(uint8_t dmx_mode_in, uint16_t universe_in,
                       uint8_t net, uint8_t subuni, uint16_t prio_in,
                       bool dhcp_in, const char *ip_str, const char *mask_str, const char *gw_str)
{
    (void)net; (void)subuni; // al momento non usati

    dmx_mode      = dmx_mode_in;
    sacn_min_prio = prio_in;
    use_dhcp      = dhcp_in ? 1 : 0;

    // Universe indirizzato al blocco giusto
    if (dmx_mode == 1)      artnet_universe = universe_in;
    else if (dmx_mode == 2) sacn_universe   = universe_in;
    else                    dmx_address     = universe_in;  // se vuoi usarlo come DMX start addr

    // String -> uint32 big-endian
    uint32_t tmp;
    if (ip_str   && str_to_ip32be(ip_str,   &tmp)) ip_address = tmp;
    if (mask_str && str_to_ip32be(mask_str, &tmp)) net_mask   = tmp;
    if (gw_str   && str_to_ip32be(gw_str,   &tmp)) gateway    = tmp;
}
