#include "h_files.h"
#include "defs.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/ip4_addr.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/igmp.h"
#include <stdbool.h>

#if LWIP_DNS
#include "lwip/dns.h"
#endif


#define NETLED_BLINK_TOGGLE_MS   100   // toggle ogni 100 ms
#define NETLED_ACTIVITY_MS       250   // finestra di lampeggio dopo evento

extern struct netif gnetif;
extern ETH_HandleTypeDef heth;

extern int32_t LAN9303_GetLinkState(void);

// --- Stato rete applicato (snapshot) ----------------------------------------
typedef struct
{
    uint8_t  use_dhcp;        // 0=static, 1=DHCP
    uint32_t ip_be;           // IP in network order (big endian)
    uint32_t netmask_be;      // netmask in network order
    uint32_t gateway_be;      // gateway in network order
} net_cfg_snapshot_t;

// Nome lungo per evitare collisioni con macro/variabili
static net_cfg_snapshot_t g_net_applied = { 0xFF, 0, 0, 0 };

void NET_SetStatic(uint32_t ip_be, uint32_t mask_be, uint32_t gw_be);
void NETLED_Task_1ms(void);

extern void NET_ApplyFromConfig(void);
extern void UI_OnNetRuntime(bool link_up, uint32_t ip_runtime_be);

void NET_UseDHCP(void);
void NET_SetStatic(uint32_t ip_be, uint32_t mask_be, uint32_t gw_be);

static void start_stream_listeners(void);
static void stop_stream_listeners(void);

uint8_t use_dhcp;
uint32_t ip_address;
uint32_t net_mask;
uint32_t gateway;
uint32_t dns1;
uint32_t dns2;

static bool art_listening  = false;
static bool sacn_listening = false;

static volatile uint8_t  netled_link_up   = 0;
static volatile uint16_t netled_activity  = 0;
static uint16_t          netled_blink_acc = 0;

volatile net_diag_state_t g_net_diag_state = NET_DIAG_NO_LINK;

// per il watchdog streaming (soft)
static uint8_t g_art_idle_s  = 0;
static uint8_t g_sacn_idle_s = 0;

volatile uint8_t g_net_apply_pending = 0;

static netif_input_fn      s_orig_input      = NULL;
static netif_linkoutput_fn s_orig_linkoutput = NULL;

net_diag_state_t NET_GetDiagState(void)
{
    return g_net_diag_state;
}

// Netmask valida tipo 255.255.255.0, 255.255.0.0, 255.255.248.0, ecc.
// In DHCP non viene controllata
static bool Net_IsValidMask(uint32_t mask)
{
    // zero o tutti 1 -> invalido
    if (mask == 0 || mask == 0xFFFFFFFFu)
        return false;

    uint8_t o[4];
    o[0] =  mask        & 0xFF;
    o[1] = (mask >>  8) & 0xFF;
    o[2] = (mask >> 16) & 0xFF;
    o[3] = (mask >> 24) & 0xFF;

    // non permettere maschere tipo 0.xxx.xxx.xxx
    if (o[0] == 0)
        return false;

    // deve essere: 255.255.255.xxx o 255.255.0.0 ecc.
    int i = 0;
    while (i < 4 && o[i] == 255)
        i++;

    if (i == 4)  // 255.255.255.255 -> invalida
        return false;

    uint8_t v = o[i];

    // ottetti ammessi per il primo byte "parziale"
    bool ok_partial =
        (v == 0)   ||
        (v == 128) ||
        (v == 192) ||
        (v == 224) ||
        (v == 240) ||
        (v == 248) ||
        (v == 252) ||
        (v == 254);

    if (!ok_partial)
        return false;

    // dopo il primo ottetto non-255, tutti gli altri devono essere 0
    for (int j = i + 1; j < 4; ++j)
    {
        if (o[j] != 0)
            return false;
    }

    // Esclude 255.255.255.255 (già filtrato) e 0.0.0.0
    return true;
}

/* ===================================================================================== */
/*                       Controlla link ogni 100 ms                                      */
/* ===================================================================================== */
void Network_LinkCheck_100ms(void)
{
    static int32_t prev_state = USER_PHY_STATUS_ERROR;
    static bool    streaming_on = false;    // stato logico listeners (anti start/stop ripetuti)

    int32_t st = LAN9303_GetLinkState();

    if (st == prev_state)
        return;      // nessun cambiamento

    prev_state = st;

    bool link_up = false;

    switch (st)
    {
        case USER_PHY_STATUS_100MBITS_FULLDUPLEX:
        case USER_PHY_STATUS_100MBITS_HALFDUPLEX:
        case USER_PHY_STATUS_10MBITS_FULLDUPLEX:
        case USER_PHY_STATUS_10MBITS_HALFDUPLEX:
            link_up = true;
            break;

        case USER_PHY_STATUS_LINK_DOWN:
        case USER_PHY_STATUS_AUTONEGO_NOTDONE:
        case USER_PHY_STATUS_ERROR:
        default:
            link_up = false;
            break;
    }

    if (!link_up)
    {
        //-----------------------------------
        //   LINK va DOWN
        //-----------------------------------
        USB_Printf("LINK DOWN -> listeners OFF\n");
        NETLED_OnLinkChange(false);

        /* Aggiorna lwIP: link fisico giù */
        netif_set_link_down(&gnetif);

        /* Spegni listeners solo se erano accesi */
        if (streaming_on)
        {
            stop_stream_listeners();
            streaming_on = false;
        }

        /* Invalida lo stato multicast/join sACN */
        sACN_ResetJoinState();

#if LWIP_DHCP
        if (use_dhcp)
        {
            dhcp_release_and_stop(&gnetif);
            USB_Printf("DHCP: release+stop\n");
        }
#endif

        UI_OnNetRuntime(false, 0);

        return;
    }

    //-----------------------------------
    //   LINK va UP
    //-----------------------------------
    USB_Printf("LINK UP detected\n");
    NETLED_OnLinkChange(true);

    /* Aggiorna lwIP: link fisico su */
    netif_set_link_up(&gnetif);

#if LWIP_DHCP
    if (use_dhcp)
    {
        USB_Printf("DHCP restart\n");

        dhcp_stop(&gnetif);
        dhcp_start(&gnetif);

        UI_OnNetRuntime(true, 0);
        return;
    }
#endif

    //-----------------------------------
    //   LINK va UP (STATIC IP)
    //-----------------------------------
    USB_Printf("Static-IP link UP\n");

    if (!ip4_addr_isany_val(*netif_ip4_addr(&gnetif)))
    {
    	uint32_t ip_be = netif_ip4_addr(&gnetif)->addr;

    	/* Aggiorna lo stato runtime */
    	UI_OnNetRuntime(true, ip_be);

        if (!streaming_on)
        {
            start_stream_listeners();
            streaming_on = true;
        }

        /* Applica config (inclusi join sACN) SOLO con link UP */
        NET_ApplyFromConfig();

        USB_Printf("[NET] LinkUP: ApplyFromConfig called\n");
    }
    else
    {
        USB_Printf("Static IP mode but IP=0.0.0.0\n");
        UI_OnNetRuntime(true, 0);
    }
}

void Network_DiagTick_1s(void)
{
    uint32_t now = HAL_GetTick();
    bool link = netif_is_link_up(&gnetif) ? true : false;

    // Sorgente effettiva (Auto -> active_proto)
    uint8_t src_mode = (dmx_mode == 3) ? active_proto : dmx_mode;

    // Default
    net_diag_state_t st = NET_DIAG_NO_LINK;

    if (!link)
    {
        st = NET_DIAG_NO_LINK;
        g_art_idle_s  = 0;
        g_sacn_idle_s = 0;
    }
    else
    {
        // === CHECK NETMASK SOLO SE STATICO ===
        if (!use_dhcp && !Net_IsValidMask(net_mask))
        {
            // Subnet marcia -> mostra errore e non andare oltre
            st = NET_DIAG_MASK_ERR;

            g_art_idle_s  = 0;
            g_sacn_idle_s = 0;
        }
        else
        {
            // !!! Aggiungere eventuale distinzione tipo di errore
            int32_t phy = LAN9303_GetLinkState();
            bool phy_error =
                (phy == USER_PHY_STATUS_AUTONEGO_NOTDONE) ||
                (phy == USER_PHY_STATUS_READ_ERROR)       ||
                (phy == USER_PHY_STATUS_WRITE_ERROR)      ||
                (phy == USER_PHY_STATUS_ADDRESS_ERROR)    ||
                (phy == USER_PHY_STATUS_RESET_TIMEOUT)    ||
                (phy == USER_PHY_STATUS_ERROR);

            if (phy_error)
            {
                st = NET_DIAG_ERROR;
            }
            else if (!art_listening && !sacn_listening)
            {
                st = NET_DIAG_LISTENERS_OFF;
            }
            else
            {
                // ---- STREAMING ACTIVE / IDLE ----
                artnet_diag_info_t ad = {0};
                sacn_diag_info_t   sd = {0};
                ArtNet_GetDiag(&ad);
                sACN_GetDiag(&sd);

                const uint32_t ACTIVE_MS = 1000;   // finestra ricezione

                bool art_any_active  = (ad.last_any_ms  && (now - ad.last_any_ms  <= ACTIVE_MS));
                bool art_cfg_active  = (ad.last_cfg_ms  && (now - ad.last_cfg_ms  <= ACTIVE_MS));
                bool sacn_any_active = (sd.last_any_ms && (now - sd.last_any_ms <= ACTIVE_MS));
                bool sacn_cfg_active = (sd.last_cfg_ms && (now - sd.last_cfg_ms <= ACTIVE_MS));

                if (src_mode == 1)   // ArtNet
                {
                    if (art_cfg_active)      st = NET_DIAG_ARTNET_RX;
                    else if (art_any_active) st = NET_DIAG_ARTNET;
                    else                     st = NET_DIAG_LINK_ONLY;
                }
                else if (src_mode == 2) // sACN
                {
                    if (sacn_cfg_active)       st = NET_DIAG_SACN_RX;
                    else if (sacn_any_active)  st = NET_DIAG_SACN;
                    else                       st = NET_DIAG_LINK_ONLY;
                }
                else // src_mode == 0 (DMX fisico)
                {
                    if (art_any_active)       st = NET_DIAG_ARTNET;
                    else if (sacn_any_active) st = NET_DIAG_SACN;
                    else                      st = NET_DIAG_LINK_ONLY;
                }

                // ===== WATCHDOG SOFT STREAMING =====
                // ArtNet
                if (src_mode == 1 && link && art_listening)
                {
                    if (!art_any_active)
                    {
                        if (g_art_idle_s < 255) g_art_idle_s++;
                    }
                    else
                    {
                        g_art_idle_s = 0;
                    }

                    if (g_art_idle_s >= 60)   // ~60s senza alcun OpDmx
                    {
                        USB_Printf("ArtNet watchdog: restart listener\r\n");
                        ArtNet_StopListening();
                        ArtNet_StartListening();
                        g_art_idle_s = 0;
                    }
                }
                else
                {
                    g_art_idle_s = 0;
                }

                // sACN
                if (src_mode == 2 && link && sacn_listening)
                {
                    if (!sacn_any_active)
                    {
                        if (g_sacn_idle_s < 255) g_sacn_idle_s++;
                    }
                    else
                    {
                        g_sacn_idle_s = 0;
                    }

                    if (g_sacn_idle_s >= 60) // ~60s senza sACN
                    {
                        USB_Printf("sACN watchdog: restart listener\r\n");
                        sACN_StopListening();
                        sACN_StartListening();
                        g_sacn_idle_s = 0;
                    }
                }
                else
                {
                    g_sacn_idle_s = 0;
                }
            }
        }
    }

    g_net_diag_state = st;
}

/* ===================================================================================== */
/*                       IGMP --> filtro MAC multicast (STM32H7)                         */
/* ===================================================================================== */

/* Programma MAC Address 1 come filtro perfetto per l'indirizzo multicast */
static void set_mac_address1_multicast(const uint8_t mac[6], bool enable)
{
    uint32_t high = ((uint32_t)mac[5] << 8) | (uint32_t)mac[4];
    uint32_t low  = ((uint32_t)mac[3] << 24) |
                    ((uint32_t)mac[2] << 16) |
                    ((uint32_t)mac[1] << 8)  |
                    (uint32_t)mac[0];

    if (enable)
    {
        high |= ETH_MACA1HR_AE;  // Address Enable
        // NB: ETH_MACA1HR_SA è 1 per SA, 0 per DA: lasciamo 0 = DA
        // ETH_MACA1HR_MBC bits = mask (0 = match, 1 = don't care), default 0
    }

    heth.Instance->MACA1HR = high;
    heth.Instance->MACA1LR = low;
}

/* === STM32H7: IGMP --> filtro MAC per-gruppo (Perfect Filter su MACA1 + flags) === */
static err_t ethernetif_igmp_mac_filter(struct netif *netif,
                                        const ip4_addr_t *group, u8_t action)
{
    (void)netif;

    /* 1) IP multicast → MAC 01:00:5E:xx:xx:xx (bit23=0) */
    uint32_t g = lwip_htonl(group->addr);
    uint8_t mac[6];
    mac[0] = 0x01; mac[1] = 0x00; mac[2] = 0x5E;
    mac[3] = (uint8_t)((g >> 16) & 0x7F);
    mac[4] = (uint8_t)((g >> 8)  & 0xFF);
    mac[5] = (uint8_t)( g        & 0xFF);

    USB_Printf("IGMP: group=%s action=%s -> MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
               ipaddr_ntoa(group),
               (action == IGMP_ADD_MAC_FILTER) ? "ADD" : "DEL",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* 2) Programma lo slot MAC Address 1 come filtro perfetto (DA) */
    /*    (hai già definito set_mac_address1_multicast(...)) */
    set_mac_address1_multicast(mac, (action == IGMP_ADD_MAC_FILTER));

    /* 3) Assicurati che il Packet Filter usi perfect/hash e NON pass-all */
#ifdef ETH_MACPFR_HPF
    SET_BIT(heth.Instance->MACPFR, ETH_MACPFR_HPF);  /* Hash/Perfect filter abilitato */
#endif
#ifdef ETH_MACPFR_PM
    CLEAR_BIT(heth.Instance->MACPFR, ETH_MACPFR_PM); /* NO PassAll multicast */
#endif
#ifdef ETH_MACPFR_HMC
    /* opzionale: tieni anche hash multicast abilitato (utile se userai MACA2/3) */
    SET_BIT(heth.Instance->MACPFR, ETH_MACPFR_HMC);
#endif

    /* 4) (Opzionale) pulisci eventuale hash table residua se vuoi essere “strict” */
    /* heth.Instance->MACHT1R = 0; */
    /* heth.Instance->MACHT0R = 0; */

    /* 5) Debug registri utili */
    USB_Printf("MACPFR=%08lX  MACA1HR=%08lX  MACA1LR=%08lX\n",
               heth.Instance->MACPFR, heth.Instance->MACA1HR, heth.Instance->MACA1LR);

    return ERR_OK;
}

/* ===================================================================================== */
/*                       Config rete da EEPROM                                           */
/* ===================================================================================== */

void NetIf_ApplyConfigFromEeprom(void)
{
    if (EE_LoadConfig() != HAL_OK)
    {
        USB_Printf("NetIf: EE_LoadConfig FAILED\n");
        return;
    }

    netif_set_default(&gnetif);

    if (!netif_is_up(&gnetif))
    {
        netif_set_up(&gnetif);
    }

#if LWIP_DNS
    /* reset DNS */
    {
        ip_addr_t nil; ip_addr_set_zero(&nil);
        for (int i = 0; i < DNS_MAX_SERVERS; i++)
        {
            dns_setserver(i, &nil);
        }
    }
#endif

    if (!use_dhcp)
    {
        dhcp_release_and_stop(&gnetif);

        /* Valori raw letti da EEPROM (diagnostica, 1 riga, no spam) */
        USB_Printf("NetIf: CFG raw ip=0x%08lX nm=0x%08lX gw=0x%08lX dns1=0x%08lX dns2=0x%08lX\n",
                   (unsigned long)ip_address,
                   (unsigned long)net_mask,
                   (unsigned long)gateway,
                   (unsigned long)dns1,
                   (unsigned long)dns2);

        ip4_addr_t ip, nm, gw;
        ip.addr = ip_address;
        nm.addr = net_mask;
        gw.addr = gateway;

        if ((ip.addr == 0) || (nm.addr == 0))
        {
            USB_Printf("NetIf: STATIC config INVALID (IP/NM is 0) -> SKIP apply\n");
            USB_Printf("NetIf: keep IP=%s NM=%s GW=%s\n",
                       ip4addr_ntoa(netif_ip4_addr(&gnetif)),
                       ip4addr_ntoa(netif_ip4_netmask(&gnetif)),
                       ip4addr_ntoa(netif_ip4_gw(&gnetif)));
            return;
        }

        netif_set_addr(&gnetif, &ip, &nm, &gw);

    #if LWIP_DNS
        if (dns1)
        {
            ip_addr_t a; ip_addr_set_zero(&a);
            a.type = IPADDR_TYPE_V4;
            a.u_addr.ip4.addr = dns1;
            dns_setserver(0, &a);
        }
        if (dns2)
        {
            ip_addr_t a; ip_addr_set_zero(&a);
            a.type = IPADDR_TYPE_V4;
            a.u_addr.ip4.addr = dns2;
            dns_setserver(1, &a);
        }
    #endif

        char ip_s[16], nm_s[16], gw_s[16];

        ip4addr_ntoa_r(netif_ip4_addr(&gnetif),     ip_s, sizeof(ip_s));
        ip4addr_ntoa_r(netif_ip4_netmask(&gnetif),  nm_s, sizeof(nm_s));
        ip4addr_ntoa_r(netif_ip4_gw(&gnetif),       gw_s, sizeof(gw_s));

        USB_Printf("NetIf: STATIC IP=%s NM=%s GW=%s\n", ip_s, nm_s, gw_s);
    }
    else
    {
        USB_Printf("NetIf: DHCP mode (deferred)\n");
    }
}

/* ===================================================================================== */
/*                       Cambio DHCP/STATICO e Universo Runtime                                               */
/* ===================================================================================== */

void NET_UseDHCP(void)
{
    // Ferma i servizi che dipendono dall'IP corrente
    stop_stream_listeners();

    // Azzera gli indirizzi sul netif: 0.0.0.0
    ip4_addr_t zero; ip4_addr_set_zero(&zero);
    netif_set_addr(&gnetif, &zero, &zero, &zero);

    // Ferma un eventuale client già attivo, poi avvia DHCP
    dhcp_stop(&gnetif);
    (void)dhcp_start(&gnetif);

    // Notifica UI: link attuale + IP runtime = 0 finché non arriva il lease
    UI_OnNetRuntime(netif_is_link_up(&gnetif), 0);
}

void NET_SetStatic(uint32_t ip_be, uint32_t mask_be, uint32_t gw_be)
{
    stop_stream_listeners();
    dhcp_stop(&gnetif);

    ip4_addr_t ip, nm, gw;
    ip.addr = ip_be; nm.addr = mask_be; gw.addr = gw_be;
    netif_set_addr(&gnetif, &ip, &nm, &gw);

    UI_OnNetRuntime(netif_is_link_up(&gnetif), ip_be);

    NET_ScheduleApply();  // NET_PollApply deve fare NET_ApplyFromConfig solo quando link/ip ok
}

void NET_ScheduleApply(void)
{
    g_net_apply_pending = 1;
}

void NET_PollApply(void)
{
    if (!g_net_apply_pending)
        return;

    /* Applica solo quando ha senso (link up + IP pronto) */
    if (!netif_is_link_up(&gnetif))
        return;

    if (use_dhcp)
    {
        if (ip4_addr_isany_val(*netif_ip4_addr(&gnetif)))
            return; // DHCP: ancora niente IP
    }
    else
    {
        if (ip4_addr_isany_val(*netif_ip4_addr(&gnetif)))
            return; // STATIC: ancora 0.0.0.0
    }

    g_net_apply_pending = 0;
    NET_ApplyFromConfig();   // join/leave + mapping universi (già idempotente + cache join)
}

/* ===================================================================================== */
/*                       Helpers listener                                                */
/* ===================================================================================== */

static void start_stream_listeners(void)
{
    if (!art_listening)
    {
        ArtNet_StartListening();
        art_listening = true;
    }

    if (!sacn_listening)
    {
        sACN_StartListening();
        sacn_listening = true;
    }
    USB_Printf("Listeners ON (ArtNet=%d, sACN=%d)\r\n", art_listening, sacn_listening);
}

static void stop_stream_listeners(void)
{
    if (art_listening)
    {
        ArtNet_StopListening();
        art_listening = false;
    }

    if (sacn_listening)
    {
        sACN_StopListening();
        sacn_listening = false;
    }
    USB_Printf("Listeners OFF\r\n");
}

/* ===================================================================================== */
/*                       Netif status callback                                           */
/* ===================================================================================== */

static void netif_status_cb(struct netif *n)
{
    const bool has_ip = !ip4_addr_isany_val(*netif_ip4_addr(n));

    if (!has_ip)
    {
        USB_Printf("IP lost (status_cb)\n");

        stop_stream_listeners();
        sACN_ResetJoinState();

        UI_OnNetRuntime(netif_is_link_up(n), 0);
        return;
    }

    USB_Printf("IP=%s (status_cb)\n", ipaddr_ntoa(netif_ip4_addr(n)));

    /* Aggiorna PRIMA lo stato runtime */
    UI_OnNetRuntime(netif_is_link_up(n), netif_ip4_addr(n)->addr);

    /* In DHCP, se uno dei listener non è attivo, riavvia */
    if (use_dhcp && (!art_listening || !sacn_listening))
    {
        start_stream_listeners();

        USB_Printf("DHCP BOUND -> listeners ON\n");
    }

    /* per sACN multicast */
    if (use_dhcp)
    {
        NET_ScheduleApply();
    }
}

/* ===================================================================================== */
/*                       Link callback                                                   */
/* ===================================================================================== */

void link_cb(struct netif *n)
{
    if (netif_is_link_up(n))
    {
        NETLED_OnLinkChange(true);

        if (!ip4_addr_isany_val(*netif_ip4_addr(n)))
        {
            /* Link UP con IP già presente */
            if (!use_dhcp)
            {
                /* Statico: puoi assicurarti che i listener siano ON */
                start_stream_listeners();
                NET_ApplyFromConfig();
                UI_OnNetRuntime(true, netif_ip4_addr(n)->addr);
                USB_Printf("LINK UP (static) -> listeners ON, config applied\n");
            }
            else
            {
                /* DHCP: NON aprire qui — aspetta netif_status_cb() quando BOUND */
                USB_Printf("LINK UP (DHCP) -> waiting for DHCP BOUND\n");
            }
        }
        else
        {
            USB_Printf("LINK UP (no IP yet)\n");
        }
    }
    else
    {
        NETLED_OnLinkChange(false);
        stop_stream_listeners();
        UI_OnNetRuntime(false, 0);
        USB_Printf("LINK DOWN -> listeners OFF\n");
    }
}

/* ===================================================================================== */
/*                      LED Ethernet (link + Activity)                                   */
/* ===================================================================================== */



/* Aggiorna stato link su una delle due porte (almeno una porta UP) */
void NETLED_OnLinkChange(bool up)
{
    netled_link_up   = up ? 1u : 0u;
    netled_activity  = 0;
    netled_blink_acc = 0;
    if (netled_link_up) NETLED_On(); else NETLED_Off();
}

/* Segnala attività RX/TX valida */
static inline void NETLED_OnEthActivity(void)
{
    if (netled_link_up) netled_activity = NETLED_ACTIVITY_MS;
}

void NETLED_Task_1ms(void)
{
    if (!netled_link_up) return;

    if (netled_activity > 0)
    {
        if (++netled_blink_acc >= NETLED_BLINK_TOGGLE_MS)
        {
            netled_blink_acc = 0;
            NETLED_Toggle();
        }
        netled_activity--;
    }
    else
    {
        NETLED_On();
        netled_blink_acc = 0;
    }
}

// -------- Hook non intrusivi su netif->input (RX) e netif->linkoutput (TX) --


static err_t netled_input_wrapper(struct pbuf *p, struct netif *netif)
{
    err_t r = s_orig_input ? s_orig_input(p, netif) : ERR_IF;
    if (r == ERR_OK) NETLED_OnEthActivity();     // RX valido → attività
    return r;
}

static err_t netled_linkoutput_wrapper(struct netif *netif, struct pbuf *p)
{
    err_t r = s_orig_linkoutput ? s_orig_linkoutput(netif, p) : ERR_IF;
    if (r == ERR_OK) NETLED_OnEthActivity();     // TX riuscito → attività
    return r;
}

/* gnetif creato (da MX_LWIP_Init()) */
void NETLED_HookNetif(struct netif *n)
{
    if (!n) return;

    // salva originali e installa wrapper (idempotente)
    if (n->input && n->input != netled_input_wrapper)
    {
        s_orig_input = n->input;
        n->input = netled_input_wrapper;
    }
    if (n->linkoutput && n->linkoutput != netled_linkoutput_wrapper)
    {
        s_orig_linkoutput = n->linkoutput;
        n->linkoutput = netled_linkoutput_wrapper;
    }
}

/* ===================================================================================== */
/*                       Init post-LwIP                                                  */
/* ===================================================================================== */

void Network_InitAfterLwip(void)
{
    NETLED_HookNetif(&gnetif);

    /* IGMP PRIMA di qualunque possibile join */
    netif_set_flags(&gnetif, NETIF_FLAG_IGMP);
    netif_set_igmp_mac_filter(&gnetif, ethernetif_igmp_mac_filter);

    /* callback dopo */
    netif_set_status_callback(&gnetif, netif_status_cb);

    netif_set_link_down(&gnetif);
    UI_OnNetRuntime(false, 0);

    NetIf_ApplyConfigFromEeprom();

    USB_Printf("Network_InitAfterLwip done\n");
}

/* ===================================================================================== */
/*                       Task aggiornamento LED e cambio stato (DHCP o statico)          */
/* ===================================================================================== */

void NetworkTask(void)
{
    // 1) LED (blink su activity + stato link)
    NETLED_Task_1ms();

    // 2) Desired config (globali già esistenti)
    const uint8_t  d_use_dhcp   = (use_dhcp ? 1u : 0u);
    const uint32_t d_ip_be      = ip_address;
    const uint32_t d_netmask_be = net_mask;
    const uint32_t d_gateway_be = gateway;

    // 3) Confronto con snapshot applicato
    int changed = 0;
    if (g_net_applied.use_dhcp != d_use_dhcp) {
        changed = 1;
    } else if (!d_use_dhcp) {
        // Solo in STATIC confrontiamo IP/mask/gw
        if (g_net_applied.ip_be      != d_ip_be     ||
            g_net_applied.netmask_be != d_netmask_be||
            g_net_applied.gateway_be != d_gateway_be) {
            changed = 1;
        }
    }

    if (!changed) return;

    // 4) Applica subito
    if (d_use_dhcp) {
        NET_UseDHCP();
        g_net_applied.use_dhcp   = 1;
        g_net_applied.ip_be      = 0;
        g_net_applied.netmask_be = 0;
        g_net_applied.gateway_be = 0;
    } else {
        NET_SetStatic(d_ip_be, d_netmask_be, d_gateway_be);
        g_net_applied.use_dhcp   = 0;
        g_net_applied.ip_be      = d_ip_be;
        g_net_applied.netmask_be = d_netmask_be;
        g_net_applied.gateway_be = d_gateway_be;
    }
}

/* ===================================================================================== */
/*                       Stampa stato                                                    */
/* ===================================================================================== */

void Network_PrintStatus(void)
{
    static uint8_t  first   = 1;
    const uint8_t   link    = netif_is_link_up(&gnetif) ? 1u : 0u;
    const uint8_t   ifup    = netif_is_up(&gnetif)      ? 1u : 0u;
    const uint32_t  ip      = gnetif.ip_addr.addr;
    const uint32_t  gw      = gnetif.gw.addr;

#if LWIP_DHCP
    const struct dhcp *dh   = netif_dhcp_data(&gnetif);
    const uint8_t     dhs   = dh ? (uint8_t)dh->state : 0u;
#else
    const uint8_t     dhs   = 0u;
#endif

    static uint8_t  p_link = 0xFF, p_ifup = 0xFF, p_dhs = 0xFF;
    static uint32_t p_ip = 0, p_gw = 0;

    // Stampa SOLO se c'è un cambiamento reale
    if (!first && link == p_link && ifup == p_ifup && ip == p_ip && gw == p_gw && dhs == p_dhs)
    {
        return;
    }

#if LWIP_DHCP
    const char *dhs_s = "OFF";
    switch (dhs)
    {
        case 1: dhs_s = "INIT";        break;
        case 2: dhs_s = "SELECTING";   break;
        case 3: dhs_s = "REQUESTING";  break;
        case 4: dhs_s = "BOUND";       break;
        case 5: dhs_s = "RENEWING";    break;
        case 6: dhs_s = "REBINDING";   break;
        case 7: dhs_s = "INIT_REBOOT"; break;
        case 8: dhs_s = "REBOOTING";   break;
        default: dhs_s = "OFF";        break;
    }
#endif

    if (first)
    {
        USB_Printf("NET: MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
                   gnetif.hwaddr[0], gnetif.hwaddr[1], gnetif.hwaddr[2],
                   gnetif.hwaddr[3], gnetif.hwaddr[4], gnetif.hwaddr[5]);
    }

    char ip_str[16], nm_str[16], gw_str[16];

    ipaddr_ntoa_r(&gnetif.ip_addr,   ip_str, sizeof ip_str);
    ipaddr_ntoa_r(&gnetif.netmask,   nm_str, sizeof nm_str);
    ipaddr_ntoa_r(&gnetif.gw,        gw_str, sizeof gw_str);

#if LWIP_DHCP
    USB_Printf("NET: link=%s if=%s ip=%s nm=%s gw=%s dhcp=%s(%u)\n",
               link ? "UP" : "DOWN",
               ifup ? "UP" : "DOWN",
               ip_str,
               nm_str,
               gw_str,
               dhs_s, dhs);

    if (dhs == 4)   // BOUND
    {
    #if LWIP_DNS
        const ip_addr_t *dns0 = dns_getserver(0);
        const ip_addr_t *dns1 = dns_getserver(1);

        char dns0_str[16], dns1_str[16];
        ipaddr_ntoa_r(dns0, dns0_str, sizeof dns0_str);
        ipaddr_ntoa_r(dns1, dns1_str, sizeof dns1_str);

        USB_Printf("NET: DNS0=%s DNS1=%s\n", dns0_str, dns1_str);
    #endif
    }
#else
    USB_Printf("NET: link=%s if=%s ip=%s nm=%s gw=%s\n",
               link ? "UP" : "DOWN",
               ifup ? "UP" : "DOWN",
               ip_str,
               nm_str,
               gw_str);
#endif

    first  = 0;
    p_link = link;  p_ifup = ifup;
    p_ip   = ip;    p_gw   = gw;   p_dhs = dhs;
}
