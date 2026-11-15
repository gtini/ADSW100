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
/* Margine su E1.20: SOP entro 2 ms; il main deve consegnare entro 1,8 ms. */
#define DMX_RDM_SERVICE_US 1800u

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

/* Main: pubblica frame completo e applica timeout monotono di 1000 ms. */
void DMX512_Service(void);
void InitDMX512(UART_HandleTypeDef *huart);
void DMX512UARTInterrupt(UART_HandleTypeDef *huart);
/* Main: acquisisce il TX condiviso senza attese, 1..513 byte; timestamp DWT fine richiesta. */
bool DMX512_SendPacket(const uint8_t *data, uint16_t count, bool discovery, uint32_t request_end);
bool DMX512_TxBusy(void);
/* Main: proprietario RX/TX e contatore incrementato solo al vero TC (non timeout). */
bool DMX512_BusBusy(void);
bool DMX512_CanReply(void);
uint32_t DMX512_TxCompletionCount(void);
uint32_t DMX512_TxFailureCount(void);
uint32_t DMX512_Cycles(void);
void DMX512_ObserveController(void);

#ifdef __cplusplus
}
#endif

#endif /* DMX_512_H */
