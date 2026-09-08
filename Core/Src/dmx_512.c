/*
 * dmx_512.c
 *
 *  Created on: Aug 15, 2025
 *      Author: gianlucatini
 */

#include "h_files.h"
#include "defs.h"

// variabili globali

volatile uint8_t dmxRxBuffer[512];
volatile int16_t dmxRxCount;
volatile _DMX_FLAGS dmxFlags;

// inizializza dmx-512

void InitDMX512(UART_HandleTypeDef *huart)
{
	int i;

	for (i = 0; i < 512; i++)
	{
		dmxRxBuffer[i] = 0;
	}

	dmxRxCount = 0;
	dmxFlags.v = 0;

	if (huart->RxState != HAL_UART_STATE_READY)
	{
		return;
	}

	huart->ReceptionType = HAL_UART_RECEPTION_STANDARD;
	huart->RxISR         = NULL;
	UART_MASK_COMPUTATION(huart);
	huart->ErrorCode = HAL_UART_ERROR_NONE;
	huart->RxState   = HAL_UART_STATE_BUSY_RX;
	ATOMIC_SET_BIT(huart->Instance->CR3, USART_CR3_EIE);				// Enable the UART Error Interrupt: (Frame error, noise error, overrun error)
	ATOMIC_SET_BIT(huart->Instance->CR1, USART_CR1_RXNEIE_RXFNEIE);		// Enable the UART Data Register Not Empty interrupt
}

// uart interrupt

void DMX512UARTInterrupt(UART_HandleTypeDef *huart)
{
	static int16_t rxInd   = -2;
	static int16_t rxCount = 0;
	uint32_t isrflags, rxdata;

	isrflags = READ_REG(huart->Instance->ISR);
	rxdata   = READ_REG(huart->Instance->RDR);

	if ((isrflags & USART_ISR_FE) != 0U)							// break (frame error)
    {
		__HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_FEF);

		if (rxCount > 0)
		{
			dmxRxCount          = rxCount;
			dmxFlags.b.dmxReady = 1;

			HAL_GPIO_TogglePin(DMX_LED_GPIO_Port, DMX_LED_Pin);
		    dmx_timeout = 1000;
		}

		rxCount = 0;
		rxInd   = -1;

		return;
    }

	if ((isrflags & USART_ISR_NE) != 0U)							// noise error
    {
		__HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_NEF);

		return;
    }

	if ((isrflags & USART_ISR_ORE) != 0U)							// overrun error
    {
		__HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF);

		return;
    }

	if (((isrflags & USART_ISR_RXNE_RXFNE) != 0U) && (rxInd > -2))	// dmx data
    {
		if (rxInd == -1)
		{
			rxInd = rxdata == DMX_ST_CODE ? 0 : -2;
		}
		else
		{
			dmxRxBuffer[rxInd++] = rxdata;
			rxCount++;

			if (rxInd == 512)
			{
				rxInd = -2;
			}
		}
    }
}
