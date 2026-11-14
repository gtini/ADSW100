/*
 * dmx_512.c
 *
 *  Created on: Aug 15, 2025
 *      Author: gianlucatini
 */

#include "h_files.h"
#include "defs.h"

/* ISR possiede staging/pending, main possiede dmxRxBuffer pubblicato. */
volatile uint8_t dmxRxBuffer[512];
volatile int16_t dmxRxCount;
volatile _DMX_FLAGS dmxFlags;
static uint8_t staging[512], pending[512];
static volatile uint16_t pending_count;
static volatile bool frame_ready;
static bool have_frame;
static volatile uint32_t frame_time;
static int16_t rx_index = -2;
static uint16_t rx_count;

void InitDMX512(UART_HandleTypeDef *huart)
{
    memset((void *)dmxRxBuffer, 0, sizeof(dmxRxBuffer));
    dmxRxCount = 0;
    dmxFlags.v = 0;
    frame_ready = false;
    have_frame = false;
    frame_time = 0;
    dmx_timeout = 0;
    rx_index = -2;
    rx_count = 0;
    if (huart->RxState != HAL_UART_STATE_READY) return;
    huart->ReceptionType = HAL_UART_RECEPTION_STANDARD;
    huart->RxISR = NULL;
    UART_MASK_COMPUTATION(huart);
    huart->ErrorCode = HAL_UART_ERROR_NONE;
    huart->RxState = HAL_UART_STATE_BUSY_RX;
    ATOMIC_SET_BIT(huart->Instance->CR3, USART_CR3_EIE);
    ATOMIC_SET_BIT(huart->Instance->CR1, USART_CR1_RXNEIE_RXFNEIE);
}

/* FE con dato zero è candidato BREAK; NE/ORE invalidano il frame corrente.
   La qualifica temporale BREAK/MAB richiede misura sul ricevitore fisico. */
void DMX512UARTInterrupt(UART_HandleTypeDef *huart)
{
    uint32_t flags = READ_REG(huart->Instance->ISR);
    uint8_t data = 0;
    if (flags & (USART_ISR_RXNE_RXFNE | USART_ISR_FE)) data = (uint8_t)READ_REG(huart->Instance->RDR);
    if (flags & (USART_ISR_NE | USART_ISR_ORE))
    {
        __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_NEF | UART_CLEAR_OREF | UART_CLEAR_FEF);
        rx_index = -2;
        rx_count = 0;
        return;
    }
    if (flags & USART_ISR_FE)
    {
        __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_FEF);
        if (data == 0 && rx_count > 0)
        {
            memcpy(pending, staging, rx_count);
            memset(pending + rx_count, 0, 512u - rx_count);
            pending_count = rx_count;
            frame_time = HAL_GetTick();
            frame_ready = true;
        }
        rx_count = 0;
        rx_index = data == 0 ? -1 : -2;
        return;
    }
    if (!(flags & USART_ISR_RXNE_RXFNE) || rx_index == -2) return;
    if (rx_index == -1) rx_index = data == DMX_ST_CODE ? 0 : -2;
    else
    {
        staging[rx_index++] = data;
        rx_count++;
        if (rx_index == 512) rx_index = -2;
    }
}

/* Sezione critica limitata a copia 512 byte: impedisce pubblicazioni strappate. */
void DMX512_Service(void)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    uint32_t stamp = frame_time;
    if (frame_ready)
    {
        memcpy((void *)dmxRxBuffer, pending, 512);
        dmxRxCount = pending_count;
        frame_ready = false;
        have_frame = true;
        dmxFlags.b.dmxReady = 1;
        HAL_GPIO_TogglePin(DMX_LED_GPIO_Port, DMX_LED_Pin);
    }
    __set_PRIMASK(key);
    uint32_t age = HAL_GetTick() - stamp;
    dmx_timeout = have_frame && age < 1000u ? (uint16_t)(1000u - age) : 0;
    if (!dmx_timeout)
    {
        memset((void *)dmxRxBuffer, 0, 512);
        dmxRxCount = 0;
        DMX_LED_Off();
    }
}
