#include "h_files.h"
#include <string.h>
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

#define ARTNET_HOLD_MS 3000u   // 3 s

/* ====== Local DB ====== */
static artnet_universe_t g_art_db[ARTNET_MAX_UNIVERSES];
static struct udp_pcb   *art_pcb = NULL;

uint16_t artnet_universe = 0; /* default SubUni (LSB) – aggiornabile da UI/EEPROM */

static uint8_t artnet_rx_buf[600];

/* ===== Helpers ===== */
static uint16_t rd_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint16_t rd_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void     wr_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }
static void     wr_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)(v & 0xFF); }

// ===== DIAGNOSTICA ARTNET =====
static artnet_diag_info_t g_art_diag = {0};

void ArtNet_GetDiag(artnet_diag_info_t *out)
{
    if (!out) return;
    *out = g_art_diag;
}

static int starts_with_artnet_id(const uint8_t *buf, uint16_t len)
{
    static const uint8_t kId[8] = {'A','r','t','-','N','e','t',0x00};
    if (len < 8) return 0;
    return (memcmp(buf, kId, 8) == 0);
}

uint16_t ArtNet_GetSlotsView(uint16_t universe, const uint8_t **slots_out)
{
    artnet_universe_t *u = ArtNet_FindUniverse(universe, false);
    if (!u) {
        if (slots_out) *slots_out = NULL;
        return 0;
    }

    uint32_t now = HAL_GetTick();
    if (u->slots_valid == 0 || (now - u->last_rx_ms) > ARTNET_HOLD_MS) {
        if (slots_out) *slots_out = NULL;  // “tutto zero” virtuale
        return 0;
    }

    if (slots_out) *slots_out = u->slots;
    return u->slots_valid;
}

/* ===== DB: find/alloc ===== */
artnet_universe_t* ArtNet_FindUniverse(uint16_t universe, bool create)
{
    for (size_t i = 0; i < ARTNET_MAX_UNIVERSES; ++i)
    {
        if (g_art_db[i].in_use && g_art_db[i].universe == universe)
        {
            return &g_art_db[i];
        }
    }
    if (!create) return NULL;

    for (size_t i = 0; i < ARTNET_MAX_UNIVERSES; ++i)
    {
        if (!g_art_db[i].in_use)
        {
            memset(&g_art_db[i], 0, sizeof(g_art_db[i]));
            g_art_db[i].in_use   = 1;
            g_art_db[i].universe = universe;
            g_art_db[i].priority = 100;
            g_art_db[i].slots_valid= 0;
            g_art_db[i].last_rx_ms = 0;
            return &g_art_db[i];
        }
    }
    return NULL;
}

/* ===== Callback applicativa ===== */
void ArtNet_OnDataPacket(uint16_t universe,
                         uint8_t sequence,
                         const uint8_t *levels,
                         uint16_t level_count,
                         const ip_addr_t *src_ip,
                         uint16_t src_port)
{
    (void)src_ip; (void)src_port;
    if (!levels) return;

    if (level_count > ARTNET_SLOTS_MAX) level_count = ARTNET_SLOTS_MAX;

    artnet_universe_t *u = ArtNet_FindUniverse(universe, true);
    if (!u) return;

    u->sequence    = sequence;
    u->slots_valid = level_count;

    if (level_count > 0) {
        memcpy(u->slots, levels, level_count);
    }
    if (level_count < ARTNET_SLOTS_MAX) {
        memset(&u->slots[level_count], 0, ARTNET_SLOTS_MAX - level_count);
    }

    u->last_rx_ms = HAL_GetTick();
}

/* ===== ArtPollReply builder ===== */
static void send_artpoll_reply(const ip_addr_t *dst_ip, u16_t dst_port)
{
    struct netif *n = netif_default;

    uint8_t r[239];					/* Build reply (239 bytes) */
    memset(r, 0, sizeof(r));
    memcpy(r, "Art-Net\0", 8);		/* ID */
    wr_le16(&r[8], 0x2100);			/* OpCode (LE) */

    /* IP */
#if LWIP_IPV4
    if (n)
    {
        const ip4_addr_t *ip4 = netif_ip4_addr(n);
        r[10] = ip4_addr1(ip4);
        r[11] = ip4_addr2(ip4);
        r[12] = ip4_addr3(ip4);
        r[13] = ip4_addr4(ip4);
    }
#endif

    /* Port (BE) = 0x1936 */
    wr_be16(&r[14], 0x1936);

    /* VersInfoH/L (fw/proto) */
    r[16] = 1; r[17] = 0;

    /* NetSwitch/SubSwitch */
    r[18] = 0; r[19] = 0;

    /* Oem (BE) – 0xFFFF non registrato */
    wr_be16(&r[20], 0xFFFF);

    /* UbeaVersion */
    r[22] = 0;

    /* Status1: 15-bit addressing capable (bit2=1) */
    r[23] = (1 << 2);

    /* ESTA */
    wr_be16(&r[24], 0x0000);

    /* ShortName[18] */
    strncpy((char*)&r[26], ARTNET_SHORT_NAME, 18);

    /* LongName[64] */
    strncpy((char*)&r[44], ARTNET_LONG_NAME, 64);

    /* NodeReport[64] */
    strncpy((char*)&r[108], "#0001 [OK] ADSW100", 64);

    /* NumPortsHi/Lo – 1 DMX OUT */
    r[172] = 0;
    r[173] = 1;

    /* PortTypes[4] – port 0 = DMX512 OUTPUT (bit7=1) */
    r[174] = 0x80; /* DMX out */

    /* GoodInput/GoodOutput = 0 (default) */

    /* SwIn[4] / SwOut[4] – mappa SubUni (LSB) */
    r[190] = (uint8_t)(artnet_universe & 0x0F); /* SwIn[0] */
    r[194] = (uint8_t)(artnet_universe & 0x0F); /* SwOut[0] */

    /* Style: 0x00 = StNode */
    r[203] = 0x00;

    /* MAC[6] */
    if (n && n->hwaddr_len == 6) {
        memcpy(&r[201], n->hwaddr, 6);
    }

    /* BindIp = stesso IP */
    r[207] = r[10]; r[208] = r[11]; r[209] = r[12]; r[210] = r[13];

    /* BindIndex */
    r[211] = 1;

    /* Status2: DHCP capable (bit1) */
    r[212] = (1 << 1);

    /* invia */
    struct pbuf *pb = pbuf_alloc(PBUF_TRANSPORT, sizeof(r), PBUF_RAM);
    if (!pb) return;
    memcpy(pb->payload, r, sizeof(r));
    udp_sendto(art_pcb, pb, dst_ip, dst_port);
    pbuf_free(pb);
}

/* ===== Receive callback ===== */
static void artnet_udp_recv(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                            const ip_addr_t *addr, u16_t port)
{
    if (!p) return;

    u16_t len = p->tot_len;
    if (len > sizeof artnet_rx_buf) len = sizeof artnet_rx_buf;

    if (pbuf_copy_partial(p, artnet_rx_buf, len, 0) != len)
    {
        pbuf_free(p);
        return;
    }

    pbuf_free(p);

    const uint8_t *buf = artnet_rx_buf;

    if (len < 10) return;

    if (!starts_with_artnet_id(buf, len)) return;

    uint16_t op = rd_le16(&buf[8]);

    switch (op)
    {
    case 0x5000: /* OpDmx */
    {
        if (len < 18) break;

        if (len >= 12) {
            uint16_t protver = rd_be16(&buf[10]);
            if (protver < 14) break;
        }

        uint8_t  seq    = buf[12];
        uint8_t  subuni = buf[14];
        uint8_t  net    = (uint8_t)(buf[15] & 0x7F);
        uint16_t dlen   = rd_be16(&buf[16]);

        if (dlen == 0) break;
        if (dlen > ARTNET_SLOTS_MAX) dlen = ARTNET_SLOTS_MAX;
        if ((uint16_t)(18 + dlen) > len) break;

        const uint8_t *levels = &buf[18];
        uint16_t level_count  = dlen;

        uint16_t universe = ((uint16_t)net << 8) | subuni;

        // ===== DIAGNOSTICA: qualsiasi OpDmx valido =====
        uint32_t now = HAL_GetTick();
        g_art_diag.last_any_ms = now;
        g_art_diag.any_count++;

        // ===== DIAGNOSTICA: pacchetti per l'universo configurato =====
        if (universe == artnet_universe)
        {
            g_art_diag.last_cfg_ms = now;
            g_art_diag.cfg_count++;
        }

        ArtNet_OnDataPacket(universe, seq, levels, level_count, addr, port);
    } break;

            case 0x2000: /* OpPoll */
            {
                /* Reply unicast al mittente su 6454 */
                send_artpoll_reply(addr, ARTNET_UDP_PORT);
            } break;

            default:
                /* ignora altri opcode */
                break;
        }
}

/* ===== Public API ===== */
void ArtNet_StartListening(void)
{
    if (art_pcb) return;

    art_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!art_pcb) {
        return;
    }

    if (udp_bind(art_pcb, IP_ANY_TYPE, ARTNET_UDP_PORT) != ERR_OK) {
        udp_remove(art_pcb);
        art_pcb = NULL;
        return;
    }

    udp_recv(art_pcb, artnet_udp_recv, NULL);
}

void ArtNet_StopListening(void)
{
    if (!art_pcb) return;
    udp_recv(art_pcb, NULL, NULL);
    udp_remove(art_pcb);
    art_pcb = NULL;
}
