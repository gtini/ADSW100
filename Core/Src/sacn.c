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

// Ritorna la "vista" dei dati: se scaduti -> valid = 0 e slots_out = NULL
uint16_t sACN_GetSlotsView(uint16_t universe, const uint8_t **slots_out)
{
    sacn_universe_t *u = sACN_FindUniverse(universe, false);
    if (!u) {
        if (slots_out) *slots_out = NULL;
        return 0;
    }

    uint32_t now = HAL_GetTick();
    if (u->slots_valid == 0 || (now - u->last_update_ms) > SACN_LOSS_MS) {
        if (slots_out) *slots_out = NULL;   // buffer "virtualmente" tutto zero
        return 0;
    }

    if (slots_out) *slots_out = u->slots;
    return u->slots_valid;
}

/* ==================== DB helpers ==================== */

sacn_universe_t* sACN_FindUniverse(uint16_t universe, bool allow_create)
{
    for (size_t i = 0; i < SACN_MAX_UNIVERSES; ++i)
    {
        if (g_sacn_db[i].in_use && g_sacn_db[i].universe == universe)
        {
            return &g_sacn_db[i];
        }
    }

    if (!allow_create)
    {
        return NULL;
    }

    for (size_t i = 0; i < SACN_MAX_UNIVERSES; ++i)
    {
        if (!g_sacn_db[i].in_use)
        {
            sacn_universe_t *u = &g_sacn_db[i];
            memset(u, 0, sizeof(*u));
            u->in_use   = 1;
            u->universe = universe;
            u->priority = 100;
            u->last_update_ms= 0;
            u->slots_valid   = 0;
            return u;
        }
    }
    return NULL;
}

bool sACN_HasUniverse(uint16_t universe)
{
    return sACN_FindUniverse(universe, false) != NULL;
}

uint8_t sACN_GetLevel(uint16_t universe, uint16_t ch_index)
{
    if (ch_index >= SACN_SLOTS_MAX)
    {
        return 0;
    }
    sacn_universe_t *u = sACN_FindUniverse(universe, false);
    if (!u || u->slots_valid == 0 || ch_index >= u->slots_valid)
    {
        return 0;
    }
    return u->slots[ch_index];
}

/* ==================== Update dal parser ==================== */

void sACN_OnDataPacket(uint16_t universe,
                       uint8_t priority,
                       uint8_t sequence,
                       uint8_t start_code,
                       const uint8_t *levels,
                       uint16_t level_count,
                       const ip_addr_t *src_ip,
                       uint16_t src_port)
{
    if (level_count > SACN_SLOTS_MAX)
    {
        level_count = SACN_SLOTS_MAX;
    }

    sacn_universe_t *u = sACN_FindUniverse(universe, true);
    if (!u)
    {
        return;
    }

    u->priority       = priority;
    u->sequence       = sequence;
    u->start_code     = start_code;
    u->slots_valid    = level_count;
    u->last_update_ms = HAL_GetTick();
    u->last_src_port  = src_port;
    if (src_ip) { u->last_src_ip = *src_ip; }

    if (levels && level_count)
    {
        memcpy(u->slots, levels, level_count);
        if (level_count < SACN_SLOTS_MAX)
        {
            memset(u->slots + level_count, 0, SACN_SLOTS_MAX - level_count);
        }
    }
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
        g_igmp_joined_used[i] = 0;
        g_igmp_joined_uni[i]  = 0;
    }
    USB_Printf("[sACN] Join state reset\n");
}

/* ==================== IGMP join/leave ==================== */

bool sACN_JoinUniverse(uint16_t universe)
{
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
    uint8_t  sequence = fr[72];
    uint16_t universe = (uint16_t)((fr[75] << 8) | fr[76]);

    if (sacn_transport == 1 && universe != sacn_universe)
    {
    	return; // scarta frame di altri universi in modalità unicast
    }

    /* ---- DMP Layer ---- */
    const uint8_t *dmp = fr + 77;

    /* deve arrivare almeno fino allo start code (offset 10) */
    if (len < (uint16_t)(dmp - buf + 11)) return;

    if (dmp[2] != 0x02) return;  /* DMP Vector */
    if (dmp[3] != 0xA1) return;  /* Addr/Data type */

    /* prop_count include Start Code */
    uint16_t prop_count = (uint16_t)((dmp[8] << 8) | dmp[9]);
    if (prop_count < 1) return;

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
                      levels, level_count, addr, port);
}

/* ==================== Start/Stop ==================== */

void sACN_StartListening(void)
{
    if (sacn_pcb) { return; }

    sacn_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!sacn_pcb) { return; }

    if (udp_bind(sacn_pcb, IP_ANY_TYPE, 5568) != ERR_OK)
    {
        udp_remove(sacn_pcb);
        sacn_pcb = NULL;
        return;
    }

    udp_recv(sacn_pcb, sacn_rx_cb, NULL);
    USB_Printf("sACN: listening on UDP/5568\n");
}

void sACN_StopListening(void)
{
    if (!sacn_pcb) { return; }
    udp_recv(sacn_pcb, NULL, NULL);
    udp_remove(sacn_pcb);
    sacn_pcb = NULL;
    USB_Printf("sACN: stopped\n");
}
