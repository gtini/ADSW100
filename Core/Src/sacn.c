#include "h_files.h"
#include "sacn.h"
#include <string.h>
#include "lwip/udp.h"
#include "lwip/igmp.h"
#include "lwip/pbuf.h"
#include "main.h"
#include "ethernetif.h"

#define SACN_LOSS_MS 2500u  // timeout raccomandato da E1.31 (~2.5 s)

extern struct netif gnetif;

static uint8_t sacn_rx_buf[1536];

/* === DB universi (lazy) === */
static sacn_universe_t g_sacn_db[SACN_MAX_UNIVERSES] = {0};

/* === UDP PCB === */
static struct udp_pcb *sacn_pcb = NULL;

/* === Diagnostica sACN ===*/
static sacn_diag_info_t g_sacn_diag = {0};

uint16_t sacn_universe;
uint8_t  sacn_transport, sacn_min_prio;

void sACN_GetDiag(sacn_diag_info_t *out)
{
    if (!out) return;
    *out = g_sacn_diag;
}

/* Tracking limitato a quattro CID per l'universo configurato; nessuna
   allocazione permanente per universi estranei. Merge HTP a pari priorità. */
typedef struct
{
    bool used;
    uint8_t cid[16], sequence, priority;
    uint16_t count;
    uint32_t stamp;
    uint8_t slots[512];
} sacn_source_t;
static sacn_source_t sources[4];

void sACN_ResetData(void)
{
    memset(g_sacn_db, 0, sizeof g_sacn_db);
    memset(sources, 0, sizeof sources);
}

sacn_universe_t *sACN_FindUniverse(uint16_t universe, bool create)
{
    if (universe != sacn_universe || universe < 1 || universe > 63999) return NULL;
    sacn_universe_t *u = &g_sacn_db[0];
    if (u->in_use && u->universe != universe) sACN_ResetData();
    if (!u->in_use && create)
    {
        u->in_use = 1;
        u->universe = universe;
    }
    return u->in_use ? u : NULL;
}

uint16_t sACN_GetSlotsView(uint16_t universe, const uint8_t **out)
{
    if (out) *out = NULL;
    sacn_universe_t *u = sACN_FindUniverse(universe, false);
    if (!u) return 0;
    uint32_t now = HAL_GetTick();
    unsigned priority = 0;
    bool any = false;
    for (unsigned i = 0; i < 4; ++i)
    {
        if (sources[i].used && now - sources[i].stamp >= SACN_LOSS_MS) sources[i].used = false;
        if (sources[i].used && sources[i].priority >= sacn_min_prio && sources[i].count)
        {
            if (!any || sources[i].priority > priority) priority = sources[i].priority;
            any = true;
        }
    }
    memset(u->slots, 0, sizeof u->slots);
    u->slots_valid = 0;
    if (!any) return 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        sacn_source_t *source = &sources[i];
        if (!source->used || source->priority != priority) continue;
        if (source->count > u->slots_valid) u->slots_valid = source->count;
        for (unsigned j = 0; j < source->count; ++j)
        {
            if (source->slots[j] > u->slots[j]) u->slots[j] = source->slots[j];
        }
    }
    u->priority = priority;
    if (out) *out = u->slots;
    return u->slots_valid;
}

bool sACN_HasUniverse(uint16_t universe)
{
    return sACN_GetSlotsView(universe, NULL) != 0;
}

uint8_t sACN_GetLevel(uint16_t universe, uint16_t ch)
{
    const uint8_t *data;
    uint16_t count = sACN_GetSlotsView(universe, &data);
    return ch < count ? data[ch] : 0;
}

/* CID e sequence sono per sorgente; pacchetti vecchi non rinnovano il timeout. */
void sACN_OnDataPacket(uint16_t universe, uint8_t priority, uint8_t sequence,
                       uint8_t start_code, const uint8_t *levels, uint16_t count,
                       const ip_addr_t *ip, uint16_t port, const uint8_t cid[16], uint8_t options)
{
    if (!cid || !levels || count > 512 || priority > 200 || start_code || (options & 0x80)) return;
    uint8_t cid_bits = 0;
    for (unsigned i = 0; i < 16; ++i) cid_bits |= cid[i];
    if (!cid_bits || (options & 0x1F)) return;
    sacn_universe_t *u = sACN_FindUniverse(universe, true);
    if (!u) return;
    uint32_t now = HAL_GetTick();
    sacn_source_t *slot = NULL;
    for (unsigned i = 0; i < 4; ++i)
    {
        if (sources[i].used && now - sources[i].stamp >= SACN_LOSS_MS) sources[i].used = false;
        if (sources[i].used && memcmp(sources[i].cid, cid, 16) == 0) slot = &sources[i];
    }
    if (slot)
    {
        int delta = (int)sequence - slot->sequence;
        /* E1.31: duplicato e finestra dei 19 pacchetti precedenti. */
        if (delta <= 0 && delta > -20) return;
        if (options & 0x40)
        {
            slot->used = false;
            return;
        }
    }
    else
    {
        if (options & 0x40) return;
        for (unsigned i = 0; i < 4; ++i) if (!sources[i].used) { slot = &sources[i]; break; }
        if (!slot)
        {
            slot = &sources[0];
            for (unsigned i = 1; i < 4; ++i) if (sources[i].priority < slot->priority) slot = &sources[i];
            if (priority <= slot->priority) return;
        }
    }
    slot->used = true;
    memcpy(slot->cid, cid, 16);
    slot->sequence = sequence;
    slot->priority = priority;
    slot->count = count;
    slot->stamp = now;
    memcpy(slot->slots, levels, count);
    u->sequence = sequence;
    u->last_update_ms = now;
    u->last_src_port = port;
    if (ip) u->last_src_ip = *ip;
}

/* Cache join IGMP per evitare chiamate/log ripetuti */
static uint16_t g_igmp_joined_uni[SACN_MAX_UNIVERSES] = {0};
static uint8_t  g_igmp_joined_used[SACN_MAX_UNIVERSES] = {0};

/* ==================== IGMP join cache ====================
   Evita join ripetuti e permette reset pulito su LINK DOWN. */

static int sACN_FindJoinedSlot(uint16_t universe)
{
    for (int i = 0; i < (int)SACN_MAX_UNIVERSES; i++)
        if (g_igmp_joined_used[i] && g_igmp_joined_uni[i] == universe)
            return i;
    return -1;
}

static int sACN_AllocJoinedSlot(uint16_t universe)
{
    for (int i = 0; i < (int)SACN_MAX_UNIVERSES; i++)
    {
        if (!g_igmp_joined_used[i])
        {
            g_igmp_joined_used[i] = 1;
            g_igmp_joined_uni[i]  = universe;
            return i;
        }
    }
    return -1;
}

static void sACN_FreeJoinedSlot(uint16_t universe)
{
    int idx = sACN_FindJoinedSlot(universe);
    if (idx >= 0)
    {
        g_igmp_joined_used[idx] = 0;
        g_igmp_joined_uni[idx]  = 0;
    }
}

void sACN_ResetJoinState(void)
{
    for (int i = 0; i < (int)SACN_MAX_UNIVERSES; i++)
    {
        if (g_igmp_joined_used[i])
        {
            ip4_addr_t group;
            IP4_ADDR(&group, 239, 255, g_igmp_joined_uni[i] >> 8, g_igmp_joined_uni[i] & 255);
            (void)igmp_leavegroup_netif(&gnetif, &group);
        }
        g_igmp_joined_used[i] = 0;
        g_igmp_joined_uni[i] = 0;
    }
    USB_Printf("[sACN] Join state reset\n");
}

/* ==================== IGMP join/leave ==================== */

bool sACN_JoinUniverse(uint16_t universe)
{
    if (universe < 1 || universe > 63999) return false;
    /* idempotente: se già joinato, evita chiamate/log ripetuti */
    if (sACN_FindJoinedSlot(universe) >= 0)
        return true;

    ip4_addr_t grp;
    IP4_ADDR(&grp, 239, 255, (uint8_t)(universe >> 8), (uint8_t)(universe & 0xFF));

    err_t e = igmp_joingroup_netif(&gnetif, &grp);
    if (e != ERR_OK)
    {
        USB_Printf("sACN join U%u -> FAIL (err=%d)\n", universe, (int)e);
        return false;
    }

    if (sACN_AllocJoinedSlot(universe) < 0)
    {
        USB_Printf("sACN join U%u -> OK (WARN: join table full)\n", universe);
        return true;
    }

    USB_Printf("sACN join U%u -> OK\n", universe);
    return true;
}

bool sACN_LeaveUniverse(uint16_t universe)
{
    /* idempotente */
    if (sACN_FindJoinedSlot(universe) < 0)
        return true;

    ip4_addr_t grp;
    IP4_ADDR(&grp, 239, 255, (uint8_t)(universe >> 8), (uint8_t)(universe & 0xFF));

    err_t e = igmp_leavegroup_netif(&gnetif, &grp);
    if (e != ERR_OK)
    {
        USB_Printf("sACN leave U%u -> FAIL (err=%d)\n", universe, (int)e);
        return false;
    }

    sACN_FreeJoinedSlot(universe);
    USB_Printf("sACN leave U%u -> OK\n", universe);
    return true;
}

/* ==================== UDP RX ==================== */

static void sacn_rx_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                       const ip_addr_t *addr, u16_t port)
{
    (void)arg; (void)pcb;
    if (!p) return;

    /* Copia pbuf in buffer contiguo e libera subito la pbuf */
    uint16_t len = p->tot_len;
    if (len > sizeof sacn_rx_buf) len = sizeof sacn_rx_buf;
    if (pbuf_copy_partial(p, sacn_rx_buf, len, 0) != len)
    {
        pbuf_free(p);
        return; // copia incompleta: esci pulito
    }
    pbuf_free(p);

    const uint8_t *buf = sacn_rx_buf;

    if (len < 126) return;

    /* Lunghezze PDU devono descrivere esattamente il datagramma ricevuto. */
    if (len > 638 || (buf[16] & 0xF0) != 0x70 ||
        (((buf[16] & 15) << 8) | buf[17]) != len - 16 ||
        (buf[38] & 0xF0) != 0x70 || (((buf[38] & 15) << 8) | buf[39]) != len - 38) return;
    /* Root layer */
    if (((buf[0] << 8) | buf[1]) != 0x0010) return;
    if (((buf[2] << 8) | buf[3]) != 0x0000) return;

    static const uint8_t ACN_PID[12] = { 'A','S','C','-','E','1','.','1','7',0,0,0 };
    if (memcmp(buf + 4, ACN_PID, 12) != 0) return;

    uint32_t root_vec = ((uint32_t)buf[18] << 24) |
                        ((uint32_t)buf[19] << 16) |
                        ((uint32_t)buf[20] << 8)  |
                         (uint32_t)buf[21];
    if (root_vec != 0x00000004u) return;  /* solo Data Packet */

    /* ---- Framing Layer ---- */
    if (len < 38 + 77) return;
    const uint8_t *fr = buf + 38;

    uint32_t fr_vec = ((uint32_t)fr[2] << 24) |
                      ((uint32_t)fr[3] << 16) |
                      ((uint32_t)fr[4] << 8)  |
                       (uint32_t)fr[5];
    if (fr_vec != 0x00000002u) return;

    uint8_t  priority = fr[70];
    uint8_t  sequence = fr[73];
    uint16_t universe = (uint16_t)((fr[75] << 8) | fr[76]);

    if (universe != sacn_universe || universe < 1 || universe > 63999 || priority > 200)
    {
    	return; // scarta frame di altri universi in modalità unicast
    }

    /* ---- DMP Layer ---- */
    const uint8_t *dmp = fr + 77;

    /* deve arrivare almeno fino allo start code (offset 10) */
    if (len < (uint16_t)(dmp - buf + 11)) return;

    if ((dmp[0] & 0xF0) != 0x70 || (((dmp[0] & 15) << 8) | dmp[1]) != len - 115 ||
        dmp[4] != 0 || dmp[5] != 0 || dmp[6] != 0 || dmp[7] != 1) return;
    /* Nessun supporto sincronizzazione: scarta Force_Synchronization,
       altrimenti usa i livelli senza attendere sync (modalità asincrona). */
    if (fr[74] & 0x20) return;
    if (dmp[2] != 0x02) return;  /* DMP Vector */
    if (dmp[3] != 0xA1) return;  /* Addr/Data type */

    /* prop_count include Start Code */
    uint16_t prop_count = (uint16_t)((dmp[8] << 8) | dmp[9]);
    if (prop_count < 1 || prop_count > 513 || len != 125u + prop_count) return;

    uint8_t  start_code   = dmp[10];       /* SC corretto */
    const uint8_t *levels = dmp + 11;      /* CH1 corretto */
    uint16_t level_count  = (prop_count > 1) ? (prop_count - 1) : 0;

    if ((uint32_t)(levels - buf) + level_count > len) return;
    if (level_count > 512) level_count = 512;

    /* Accetta SOLO DMX data (SC=0x00) */
    if (start_code != 0x00)
    {
        return;
    }

    uint32_t now = HAL_GetTick();
    g_sacn_diag.last_any_ms = now;
    g_sacn_diag.any_count++;

    if (universe == sacn_universe)
    {
        g_sacn_diag.last_cfg_ms = now;
        g_sacn_diag.cfg_count++;
    }

    /* Pubblica nel DB bufferizzato */
    sACN_OnDataPacket(universe, priority, sequence, start_code,
                      levels, level_count, addr, port, buf + 22, fr[74]);
}

/* ==================== Start/Stop ==================== */

bool sACN_StartListening(void)
{
    if (sacn_pcb) { return true; }

    sacn_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!sacn_pcb) { return false; }

    if (udp_bind(sacn_pcb, IP_ANY_TYPE, 5568) != ERR_OK)
    {
        udp_remove(sacn_pcb);
        sacn_pcb = NULL;
        return false;
    }

    udp_recv(sacn_pcb, sacn_rx_cb, NULL);
    USB_Printf("sACN: listening on UDP/5568\n");
    return true;
}

void sACN_StopListening(void)
{
    sACN_ResetData();
    memset(&g_sacn_diag, 0, sizeof g_sacn_diag);
    if (!sacn_pcb) { return; }
    udp_recv(sacn_pcb, NULL, NULL);
    udp_remove(sacn_pcb);
    sacn_pcb = NULL;
    USB_Printf("sACN: stopped\n");
}
