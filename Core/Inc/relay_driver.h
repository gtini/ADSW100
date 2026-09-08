/*
 * relay_driver.h
 *
 *  Created on: Aug 15, 2025
 *      Author: gianlucatini
 */

#ifndef RELAY_DRIVER_H
#define RELAY_DRIVER_H

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// tipi
typedef enum {
    ST_UNKN,
    ST_OFF,
    ST_ON,
    ST_GUARD
} RLY_STATUS;

// costanti
#define N_RLY           12
#define RLY_PULSE       40
#define RLY_GUARD       30

// variabili globali
extern RLY_STATUS rlyStatus[N_RLY];
extern int32_t    rlyTimer[N_RLY];

// prototipi pubblici
void     InitRelay(void);
void     RelayTask(void);

// API per modalità manuale / lettura stato ===
void     Relay_ManualEnter(void);         // abilita override manuale dagli stati attuali
void     Relay_ManualExit(void);          // disabilita override manuale
void     Relay_ManualToggle(uint8_t idx); // idx = 0..N_RLY-1, toggle ON/OFF
uint16_t Relay_GetOnMask(void);           // bit i = 1 se relè i è ON

#ifdef __cplusplus
}
#endif

#endif /* RELAY_DRIVER_H */
