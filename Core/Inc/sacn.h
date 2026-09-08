#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "lwip/ip_addr.h"

/* Numero massimo di universi gestiti nel buffer */
#ifndef SACN_MAX_UNIVERSES
#define SACN_MAX_UNIVERSES 16
#endif

#define SACN_SLOTS_MAX 512

typedef struct
{
    /* Meta */
    uint16_t  universe;                 /* 1..63999 */
    uint8_t   priority;                 /* 0..200 (100 default) */
    uint8_t   sequence;                 /* 0..255 */
    uint8_t   start_code;               /* 0x00 = DMX data */

    /* Dati DMX */
    uint16_t  slots_valid;              /* quanti canali (max 512) */
    uint8_t   slots[SACN_SLOTS_MAX];    /* CH1..CH512 */

    /* Telemetria */
    uint32_t  last_update_ms;
    ip_addr_t last_src_ip;
    uint16_t  last_src_port;

    /* Stato interno */
    uint8_t   in_use;
} sacn_universe_t;

#ifdef __cplusplus
extern "C" {
#endif

void sACN_ResetJoinState(void);

/* DB: trova/crea universo (lazy) */
sacn_universe_t* sACN_FindUniverse(uint16_t universe, bool allow_create);

/* Letture “sicure”: ritorna false/0 se non disponibile */
bool    sACN_HasUniverse(uint16_t universe);
uint8_t sACN_GetLevel(uint16_t universe, uint16_t ch_index);

/* Parser --> aggiorna DB */
void sACN_OnDataPacket(uint16_t universe,
                       uint8_t priority,
                       uint8_t sequence,
                       uint8_t start_code,
                       const uint8_t *levels,
                       uint16_t level_count,
                       const ip_addr_t *src_ip,
                       uint16_t src_port);

/* UDP listener */
void sACN_StartListening(void);
void sACN_StopListening(void);

/* IGMP join/leave per un universo */
bool sACN_JoinUniverse(uint16_t universe);
bool sACN_LeaveUniverse(uint16_t universe);

uint16_t sACN_GetSlotsView(uint16_t universe, const uint8_t **slots_out);

typedef struct
{
    uint32_t last_any_ms;   // ultimo pacchetto sACN (qualsiasi universe)
    uint32_t last_cfg_ms;   // ultimo pacchetto sACN per l'universo impostato
    uint32_t any_count;
    uint32_t cfg_count;
} sacn_diag_info_t;

/* Diagnostica */
void     sACN_GetDiag(sacn_diag_info_t *out);


#ifdef __cplusplus
}
#endif
