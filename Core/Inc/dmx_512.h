/*
 * dmx_512.h
 *
 *  Created on: Aug 15, 2025
 *      Author: gianlucatini
 */

#ifndef DMX_512_H
#define DMX_512_H

#include "stm32h7xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// costanti

#define	DMX_ST_CODE		0U

// tipo per dmxFlags

typedef union
{
	uint32_t v;

	struct
	{
		uint32_t dmxReady  : 1;
		uint32_t dmxTxOn   : 1;
		uint32_t wlReady   : 1;
		uint32_t wlTxOn    : 1;
		uint32_t rcvngRdm  : 1;
		uint32_t rdmComm   : 1;
		uint32_t rdmReady  : 1;
		uint32_t rdmBroadc : 1;
	} b;
} _DMX_FLAGS;

// variabili globali

extern volatile uint8_t dmxRxBuffer[512];
extern volatile int16_t dmxRxCount;
extern volatile _DMX_FLAGS dmxFlags;

// prototipi pubblici

void InitDMX512(UART_HandleTypeDef *huart);
void DMX512UARTInterrupt(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* DMX_512_H */
