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

/* Due sorgenti Art-Net HTP per il solo universo utilizzato dal modulo.
   I record di merge scadono dopo 10 s; la disponibilità applicativa resta 3 s. */
typedef struct
{
    bool used;
    ip_addr_t ip;
    uint8_t physical, sequence;
    uint16_t count;
    uint32_t stamp;
    uint8_t slots[512];
} art_source_t;
static art_source_t sources[2];
static uint8_t packet_physical;

static void ArtNet_ResetData(void)
{
    memset(g_art_db, 0, sizeof g_art_db);
    memset(sources, 0, sizeof sources);
}

artnet_universe_t *ArtNet_FindUniverse(uint16_t universe, bool create)
{
    if (universe != artnet_universe || universe > 32767) return NULL;
    artnet_universe_t *u = &g_art_db[0];
    if (u->in_use && u->universe != universe) ArtNet_ResetData();
    if (!u->in_use && create)
    {
        u->in_use = 1;
        u->universe = universe;
    }
    return u->in_use ? u : NULL;
}

uint16_t ArtNet_GetSlotsView(uint16_t universe, const uint8_t **out)
{
    if (out) *out = NULL;
    artnet_universe_t *u = ArtNet_FindUniverse(universe, false);
    if (!u) return 0;
    uint32_t now = HAL_GetTick(), latest_age = UINT32_MAX;
    memset(u->slots, 0, sizeof u->slots);
    u->slots_valid = 0;
    for (unsigned i = 0; i < 2; ++i)
    {
        art_source_t *s = &sources[i];
        uint32_t age = now - s->stamp;
        if (s->used && age >= 10000u) s->used = false;
        if (!s->used) continue;
        if (age < latest_age) latest_age = age;
        if (s->count > u->slots_valid) u->slots_valid = s->count;
        for (unsigned j = 0; j < s->count; ++j)
        {
            if (s->slots[j] > u->slots[j]) u->slots[j] = s->slots[j];
        }
    }
    if (latest_age >= ARTNET_HOLD_MS || !u->slots_valid) return 0;
    if (out) *out = u->slots;
    return u->slots_valid;
}

void ArtNet_OnDataPacket(uint16_t universe, uint8_t sequence, const uint8_t *levels,
                        uint16_t count, const ip_addr_t *ip, uint16_t port)
{
    (void)port;
    if (!levels || !ip || count < 2 || count > 512 || (count & 1)) return;
    artnet_universe_t *u = ArtNet_FindUniverse(universe, true);
    if (!u) return;
    uint32_t now = HAL_GetTick();
    art_source_t *slot = NULL;
    for (unsigned i = 0; i < 2; ++i)
    {
        if (sources[i].used && now - sources[i].stamp >= 10000u) sources[i].used = false;
        if (sources[i].used && ip_addr_cmp(&sources[i].ip, ip) && sources[i].physical == packet_physical)
        {
            slot = &sources[i];
            break;
        }
    }
    if (!slot)
    {
        for (unsigned i = 0; i < 2; ++i) if (!sources[i].used) { slot = &sources[i]; break; }
        if (!slot) return;
        memset(slot, 0, sizeof *slot);
    }
    if (slot->used && sequence && slot->sequence)
    {
        uint8_t delta = (uint8_t)(sequence - slot->sequence);
        if (!delta || delta >= 128) return;
    }
    slot->used = true;
    slot->ip = *ip;
    slot->physical = packet_physical;
    slot->sequence = sequence;
    slot->count = count;
    slot->stamp = now;
    memcpy(slot->slots, levels, count);
    u->sequence = sequence;
    u->last_rx_ms = now;
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

    /* Porta little-endian = 0x1936. */
    wr_le16(&r[14], 0x1936);

    /* VersInfoH/L (fw/proto) */
    r[16] = FW_VERSION_MAJOR; r[17] = FW_VERSION_MINOR;

    /* NetSwitch/SubSwitch */
    r[18] = (uint8_t)(artnet_universe >> 8); r[19] = (uint8_t)((artnet_universe >> 4) & 15);

    /* Oem (BE) – 0xFFFF non registrato */
    wr_be16(&r[20], BRAND_ARTNET_OEM_UNASSIGNED);

    /* UbeaVersion */
    r[22] = 0;

    /* Status1: avvio da flash, nessuna capability RDM dichiarata. */
    r[23] = 0;

    /* ESTA */
    wr_le16(&r[24], BRAND_ESTA_MANUFACTURER_ID);

    /* ShortName[18] */
    const brand_info_t *info = Brand_Current();
    strncpy((char*)&r[26], info->model, 17);

    /* LongName[64] */
    snprintf((char *)&r[44], 64, "%s %s DMX/ArtNet/sACN Power Switches", info->manufacturer, info->model);

    /* NodeReport[64] */
    static uint16_t replies;
    replies = (uint16_t)((replies + 1u) % 10000u);
    snprintf((char *)&r[108], 64, "#0001 [%04u] %s", (unsigned)replies, info->model);

    /* NumPortsHi/Lo – 1 DMX OUT */
    r[172] = 0;
    r[173] = 1;

    /* PortTypes[4] – port 0 = DMX512 OUTPUT (bit7=1) */
    r[174] = 0x80; /* DMX out */

    /* GoodInput/GoodOutput = 0 (default) */

    /* SwIn[4] / SwOut[4] – mappa SubUni (LSB) */
    r[186] = 0; /* Nessun ingresso Art-Net pubblicato. */
    r[190] = (uint8_t)(artnet_universe & 0x0F); /* SwOut[0] */

    /* Style: 0x00 = StNode */
    r[200] = 0x00;

    /* MAC[6] */
    if (n && n->hwaddr_len == 6) {
        memcpy(&r[201], n->hwaddr, 6);
    }

    /* BindIp = stesso IP */
    r[207] = r[10]; r[208] = r[11]; r[209] = r[12]; r[210] = r[13];

    /* BindIndex */
    r[211] = 1;

    /* Status2: DHCP capace/attivo e indirizzamento a 15 bit. */
    r[212] = (1 << 2) | (1 << 3) | (use_dhcp ? (1 << 1) : 0);

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

        if (dlen < 2 || dlen > ARTNET_SLOTS_MAX || (dlen & 1)) break;
        if (buf[15] & 0x80) break;
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

        packet_physical = buf[13];
        ArtNet_OnDataPacket(universe, seq, levels, level_count, addr, port);
    } break;

            case 0x2000: /* OpPoll */
            {
                /* Reply unicast al mittente su 6454 */
                if (len >= 14 && rd_be16(buf + 10) >= 14) send_artpoll_reply(addr, ARTNET_UDP_PORT);
            } break;

            default:
                /* ignora altri opcode */
                break;
        }
}

/* ===== Public API ===== */
bool ArtNet_StartListening(void)
{
    if (art_pcb) return true;

    art_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!art_pcb) {
        return false;
    }

    if (udp_bind(art_pcb, IP_ANY_TYPE, ARTNET_UDP_PORT) != ERR_OK) {
        udp_remove(art_pcb);
        art_pcb = NULL;
        return false;
    }

    udp_recv(art_pcb, artnet_udp_recv, NULL);
    return true;
}

void ArtNet_StopListening(void)
{
    ArtNet_ResetData();
    memset(&g_art_diag, 0, sizeof g_art_diag);
    if (!art_pcb) return;
    udp_recv(art_pcb, NULL, NULL);
    udp_remove(art_pcb);
    art_pcb = NULL;
}
