

#ifndef NETWORK_H
#define NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

struct netif;  // forward-declare

void NetIf_ApplyConfigFromEeprom(void);   // DHCP o statico + DNS da EEPROM
void link_cb(struct netif* nif);          // callback registrato su netif_set_link_callback
void Network_InitAfterLwip(void);         // registra link_cb + applica cfg iniziale
void Network_PrintStatus(void);           // stampa stato rete

void NETLED_OnLinkChange(bool up);
void NetworkTask(void);
void NETLED_HookNetif(struct netif *n);
void NET_ScheduleApply(void);
void NET_PollApply(void);
void Network_LinkCheck_100ms(void);

typedef enum {
    NET_DIAG_NO_LINK = 0,
    NET_DIAG_ERROR,
    NET_DIAG_MASK_ERR,
    NET_DIAG_LISTENERS_OFF,
    NET_DIAG_LINK_ONLY,
    NET_DIAG_ARTNET,
    NET_DIAG_ARTNET_RX,
    NET_DIAG_SACN,
    NET_DIAG_SACN_RX
} net_diag_state_t;


// API diagnostica
net_diag_state_t NET_GetDiagState(void);
void     Network_DiagTick_1s(void);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_H */
