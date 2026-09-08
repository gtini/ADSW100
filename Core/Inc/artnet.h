/*
 * artnet.h
 * Subset Art-Net v4: OpDmx (0x5000), OpPoll (0x2000) + OpPollReply (0x2100)
 */

#pragma once
#ifndef INC_ARTNET_H_
#define INC_ARTNET_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "lwip/ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Config ===== */
#define ARTNET_MAX_UNIVERSES   16
#define ARTNET_SLOTS_MAX       512
#define ARTNET_UDP_PORT        6454

#define ARTNET_SHORT_NAME      "ADSW100"
#define ARTNET_LONG_NAME       "Viedelio HMS DMX/ArtNet/sACN Power Switches"

/* ===== Public data structures ===== */
typedef struct
{
    /* Meta */
    uint16_t  universe;                 /* 0..32767 (15-bit Port-Address: Net<<8 | SubUni) */
    uint8_t   sequence;                 /* 0..255 (0=unused) */
    uint8_t   priority;                 /* es. 100 per compat con sACN merge */
    uint16_t  slots_valid;              /* numero canali validi */

    /* DMX data */
    uint8_t   slots[ARTNET_SLOTS_MAX];  /* CH1..CH512 */

    /* Telemetry */
    uint8_t   in_use;                   /* entry allocata nel DB */
    uint32_t  last_rx_ms;
} artnet_universe_t;

// ================== DIAGNOSTICA STREAMING ==================

typedef struct
{
    uint32_t last_any_ms;   // ultimo OpDmx ricevuto (qualsiasi universe)
    uint32_t last_cfg_ms;   // ultimo OpDmx per il mio universe configurato
    uint32_t any_count;     // conteggio totale pacchetti DMX
    uint32_t cfg_count;     // conteggio pacchetti per l'universo impostato
} artnet_diag_info_t;

/* ===== API ===== */

/* Callback applicativa */
void ArtNet_OnDataPacket(uint16_t universe,
                         uint8_t sequence,
                         const uint8_t *levels,
                         uint16_t level_count,
                         const ip_addr_t *src_ip,
                         uint16_t src_port);

/* Avvio/stop listener UDP */
void ArtNet_StartListening(void);
void ArtNet_StopListening(void);

/* Diagnostica */
void     ArtNet_GetDiag(artnet_diag_info_t *out);

/* Trova (o crea) l'entry di un universo nel DB interno */
artnet_universe_t* ArtNet_FindUniverse(uint16_t universe, bool create);

uint16_t ArtNet_GetSlotsView(uint16_t universe, const uint8_t **slots_out);

/* Facoltativo: universo corrente (SubUni LSB) da UI/EEPROM */
extern uint16_t artnet_universe;

#ifdef __cplusplus
}
#endif

#endif /* INC_ARTNET_H_ */
