/*
 * dmx_512.c
 *
 *  Created on: Aug 15, 2025
 *      Author: gianlucatini
 */

#include "h_files.h"
#include "defs.h"
#include "rdm.h"

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

/* TIM7 a 1 MHz governa BREAK/MAB e finestre RX, TXE/TC i byte.
   Adattamento del proprietario unico USART di DRV_STG4; nessun DMA o attesa main. */
static UART_HandleTypeDef *port_uart;
static volatile uint8_t tx_state;
static volatile uint16_t tx_index, tx_count;
static uint8_t tx_data[513], node_slots[512];
static volatile uint32_t node_at, listen_until, rx_activity_at, controller_at;
static volatile bool controller_seen, rx_activity_seen;
/* Cessione RDM in RAM, distinta dal rilevamento DMX e valida per entrambi i Node. */
static volatile bool rdm_controller_seen;
static volatile uint32_t rdm_controller_at;
#define RDM_CONTROLLER_HOLD_MS 30000u
static uint8_t applied_port = 255, listen_phase;
static uint8_t rdm_staging[257], rdm_pending[257], rdm_next[257];
/* Due snapshot: il secondo broadcast non deve sparire mentre il main
   elabora il primo. Nessuna allocazione o logica PID nel proprietario UART. */
static volatile bool rdm_next_ready;
static volatile uint16_t rdm_next_length;
static volatile uint32_t rdm_next_end;
static volatile uint16_t rdm_length;
static volatile uint32_t rdm_end;
static volatile bool rdm_ready;
static bool receiving_rdm;
static uint16_t rdm_count;
static uint32_t frame_start;
static volatile uint32_t frames_sent, rdm_seen, tx_faults;
static volatile uint32_t rx_packets, rx_timeouts, rx_dropped, rx_fe, rx_ne, rx_ore;
static volatile uint32_t rx_bytes, dmx_frames, rx_deferred;

/* Proprietà RX distinta dalle fasi TX: un timer periodico non può sottrarre
   il bus a un pacchetto in corso o a una risposta non ancora terminata al TC. */
typedef enum
{
    BUS_IDLE, BUS_START_CODE, BUS_RDM_RX, BUS_DMX_RX,
    BUS_RDM_PENDING, BUS_RDM_PROCESSING, BUS_RDM_REPLY, BUS_OTHER_REPLY
} bus_state_t;
static volatile bus_state_t bus_state;
static volatile uint32_t rx_started_at, rx_last_cycles, rx_started_cycles;
#define RDM_INTERBYTE_US 2200u /* Slot 44 us + pausa RX ammessa 2100 us, margine ISR. */
#define RDM_PACKET_US 35000u   /* Oltre il massimo nominale di 257 slot con pause medie 76 us. */

static void ReceiverAbort(void)
{
    rx_index = -2;
    rx_count = rdm_count = 0;
    receiving_rdm = false;
    bus_state = BUS_IDLE;
}

/* ISR TIM7 o sezione critica main. Il completamento normale viene da lunghezza
   e checksum, poi TC; questi limiti servono solo a recuperare pacchetti interrotti. */
static void ReceiverExpire(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t cycles = DWT->CYCCNT, per_us = SystemCoreClock / 1000000u;
    if ((bus_state == BUS_START_CODE && now - rx_started_at > 2002u) ||
        (bus_state == BUS_DMX_RX && now - rx_started_at > 1252u) ||
        (bus_state == BUS_RDM_RX &&
         (cycles - rx_last_cycles > RDM_INTERBYTE_US * per_us ||
          cycles - rx_started_cycles > RDM_PACKET_US * per_us)) ||
        ((bus_state == BUS_RDM_PENDING || bus_state == BUS_RDM_PROCESSING) &&
         cycles - rdm_end > DMX_RDM_SERVICE_US * per_us) ||
        (bus_state == BUS_OTHER_REPLY && now - rx_started_at >= 6u))
    {
        ++rx_timeouts;
        /* Scade la proprieta' del bus, non il messaggio. Un broadcast UN_MUTE
           resta da elaborare nel main anche oltre la deadline delle risposte. */
        ReceiverAbort();
    }
}

bool DMX512_CanReply(void)
{
    return port_uart && !tx_state && !rdm_ready &&
           (bus_state == BUS_IDLE || bus_state == BUS_RDM_PROCESSING) &&
           !(port_uart->Instance->ISR & (USART_ISR_BUSY | USART_ISR_RXNE_RXFNE |
                                       USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE));
}

bool DMX512_BusBusy(void)
{
    return tx_state != 0 || bus_state != BUS_IDLE || rdm_ready;
}

uint32_t DMX512_TxFailureCount(void)
{
    return tx_faults;
}

uint32_t DMX512_TxCompletionCount(void)
{
    return frames_sent;
}

/* Diagnostica non bloccante nel main; nessuna formattazione o stampa ISR.
   FE include anche BREAK validi, NE/ORE contano i singoli flag osservati. */
void DMX512_FormatDiagnostics(char *out, size_t capacity)
{
    if (!out || !capacity) return;
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    uint32_t cr1 = port_uart ? port_uart->Instance->CR1 : 0;
    uint32_t cr3 = port_uart ? port_uart->Instance->CR3 : 0;
    uint32_t isr = port_uart ? port_uart->Instance->ISR : 0;
    uint32_t packets = rx_packets, expired = rx_timeouts, drops = rx_dropped;
    uint32_t bytes = rx_bytes, frames = dmx_frames, deferred = rx_deferred;
    uint32_t fe = rx_fe, ne = rx_ne, ore = rx_ore, faults = tx_faults, sent = frames_sent;
    uint32_t timer_cr1 = TIM7->CR1, timer_dier = TIM7->DIER, timer_sr = TIM7->SR;
    uint32_t timer_arr = TIM7->ARR, timer_cnt = TIM7->CNT;
    unsigned tx = tx_state, bus = bus_state, ready = (unsigned)rdm_ready + (unsigned)rdm_next_ready;
    unsigned de = HAL_GPIO_ReadPin(RDM_DIR_GPIO_Port, RDM_DIR_Pin) == GPIO_PIN_SET;
    unsigned irq = NVIC_GetEnableIRQ(USART1_IRQn), irq_pending = NVIC_GetPendingIRQ(USART1_IRQn);
    unsigned rx_pin = HAL_GPIO_ReadPin(RDM_RXD_GPIO_Port, RDM_RXD_Pin) == GPIO_PIN_SET;
    unsigned role = applied_port, configured_role = dmx_port_mode;
    uint32_t age = HAL_GetTick() - rdm_controller_at;
    uint32_t rdm_hold = rdm_controller_seen && age < RDM_CONTROLLER_HOLD_MS ?
                        RDM_CONTROLLER_HOLD_MS - age : 0;
    __set_PRIMASK(key);
    snprintf(out, capacity,
             "ms=%lu tx=%u bus=%u pending=%u de=%u hold=%lu irq=%u/%u pin=%u role=%u/%u cr1=%lX cr3=%lX isr=%lX tim=%lX/%lX/%lX/%lu/%lu rx=%lu exp=%lu drop=%lu fe=%lu ne=%lu ore=%lu sent=%lu fault=%lu bytes=%lu dmx=%lu defer=%lu\r\n",
             (unsigned long)HAL_GetTick(), tx, bus, ready, de, (unsigned long)rdm_hold, irq, irq_pending, rx_pin, role, configured_role,
             (unsigned long)cr1, (unsigned long)cr3,
             (unsigned long)isr, (unsigned long)timer_cr1, (unsigned long)timer_dier, (unsigned long)timer_sr,
             (unsigned long)timer_arr, (unsigned long)timer_cnt,
             (unsigned long)packets,(unsigned long)expired,(unsigned long)drops,(unsigned long)fe,
             (unsigned long)ne,(unsigned long)ore, (unsigned long)sent, (unsigned long)faults,
             (unsigned long)bytes, (unsigned long)frames, (unsigned long)deferred);
}

uint32_t DMX512_Cycles(void)
{
    return DWT->CYCCNT;
}

/* UG carica il prescaler ma non deve generare un IRQ software: con UIE
   ancora attivo può restare una richiesta NVIC anche dopo aver azzerato SR. */
static void TimerStop(void)
{
    TIM7->DIER = 0;
    TIM7->CR1 = TIM_CR1_URS;
    TIM7->SR = 0;
    __DSB();
    HAL_NVIC_ClearPendingIRQ(TIM7_IRQn);
}

static void TimerArm(uint16_t microseconds)
{
    /* TIM6/TIM7: ARR=0 blocca il contatore anche con CEN=1. Con tick da
       1 us il minimo one-shot valido e' 2 us (ARR=1), mai 1 us (ARR=0). */
    if (microseconds < 2u) microseconds = 2u;
    TimerStop();
    TIM7->ARR = microseconds - 1u;
    TIM7->CNT = 0;
    TIM7->EGR = TIM_EGR_UG;
    TIM7->SR = 0;
    __DSB();
    HAL_NVIC_ClearPendingIRQ(TIM7_IRQn);
    TIM7->DIER = TIM_DIER_UIE;
    TIM7->CR1 = TIM_CR1_URS | TIM_CR1_OPM | TIM_CR1_CEN;
}

static void TxPin(bool gpio_mode, bool high)
{
    GPIO_InitTypeDef pin = {0};
    HAL_GPIO_WritePin(RDM_TXD_GPIO_Port, RDM_TXD_Pin, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    pin.Pin = RDM_TXD_Pin;
    pin.Mode = gpio_mode ? GPIO_MODE_OUTPUT_PP : GPIO_MODE_AF_PP;
    pin.Speed = GPIO_SPEED_FREQ_HIGH;
    pin.Alternate = GPIO_AF4_USART1;
    HAL_GPIO_Init(RDM_TXD_GPIO_Port, &pin);
}

static void ReceiveDirection(void)
{
    TimerStop();
    CLEAR_BIT(port_uart->Instance->CR1, USART_CR1_TXEIE_TXFNFIE | USART_CR1_TCIE);
    HAL_GPIO_WritePin(RDM_DIR_GPIO_Port, RDM_DIR_Pin, GPIO_PIN_RESET);
    SET_BIT(port_uart->Instance->CR1, USART_CR1_RE);
    port_uart->Instance->RQR = USART_RQR_RXFRQ;
    __HAL_UART_CLEAR_FLAG(port_uart, UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_OREF | UART_CLEAR_TCF);
    tx_state = 0;
    rx_index = -2;
    rx_count = rdm_count = 0;
    receiving_rdm = false;
    bus_state = BUS_IDLE;
}

static void BeginBreak(void)
{
    CLEAR_BIT(port_uart->Instance->CR1, USART_CR1_RE);
    HAL_GPIO_WritePin(RDM_DIR_GPIO_Port, RDM_DIR_Pin, GPIO_PIN_SET);
    TxPin(true, false);
    tx_state = 1;
    TimerArm(176);
}

/* Main con IRQ mascherati oppure ISR: scadenza memorizzata anche in IN,
   senza riaprire una vecchia cessione al wrap del tick. */
static bool RDMControllerPresent(uint32_t now)
{
    if (rdm_controller_seen && now - rdm_controller_at >= RDM_CONTROLLER_HOLD_MS)
    {
        rdm_controller_seen = false;
    }
    return rdm_controller_seen;
}

/* Nodo: mai iniziare se linea bassa, attività pendente o altro controller.
   DMX mantiene la cessione esistente di 2 s; RDM valido la rinnova a 30 s. */
static void NodeTryStart(void)
{
    ReceiverExpire();
    uint32_t now = HAL_GetTick();
    if (applied_port == DMX_PORT_IN || applied_port > DMX_PORT_SACN_NODE) return;
    bool rdm_controller = RDMControllerPresent(now);
    if ((int32_t)(now - listen_until) < 0 || bus_state != BUS_IDLE ||
        (rx_activity_seen && now - rx_activity_at < 30u) ||
        (controller_seen && now - controller_at < 2000u) ||
        rdm_controller ||
        (port_uart->Instance->ISR & (USART_ISR_RXNE_RXFNE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE | USART_ISR_BUSY)) ||
        HAL_GPIO_ReadPin(RDM_RXD_GPIO_Port, RDM_RXD_Pin) == GPIO_PIN_RESET)
    {
        TimerArm(1000);
        return;
    }
    tx_data[0] = 0;
    /* Cache aggiornata dal main; se il main non produce più, blackout entro 50 ms. */
    if (now - node_at < 50u) memcpy(tx_data + 1, node_slots, 512);
    else memset(tx_data + 1, 0, 512);
    tx_index = 0;
    tx_count = 513;
    BeginBreak();
}

/* Handler applicativo su vettore TIM7 già presente nella startup CMSIS. */
void TIM7_IRQHandler(void)
{
    /* Una richiesta NVIC obsoleta non è una scadenza BREAK/MAB/TX. */
    if (!(TIM7->SR & TIM_SR_UIF) || !(TIM7->DIER & TIM_DIER_UIE)) return;
    TIM7->SR = 0;
    TIM7->DIER = 0;
    __DSB();
    if (tx_state == 1)
    {
        HAL_GPIO_WritePin(RDM_TXD_GPIO_Port, RDM_TXD_Pin, GPIO_PIN_SET);
        tx_state = 2;
        TimerArm(16);
    }
    else if (tx_state == 2 || tx_state == 5)
    {
        bool no_break = tx_state == 5;
        if (no_break) CLEAR_BIT(port_uart->Instance->CR1, USART_CR1_RE);
        /* Come URC01: prepara AF prima di DE. La discovery non può spendere
           HAL_GPIO_Init o aspettare un altro IRQ tra DE e primo start bit (4 us). */
        TxPin(false, true);
        tx_state = 3;
        __HAL_UART_CLEAR_FLAG(port_uart, UART_CLEAR_TCF);
        tx_index = 1;
        if (no_break) HAL_GPIO_WritePin(RDM_DIR_GPIO_Port, RDM_DIR_Pin, GPIO_PIN_SET);
        port_uart->Instance->TDR = tx_data[0];
        SET_BIT(port_uart->Instance->CR1, USART_CR1_TXEIE_TXFNFIE);
        TimerArm(30000); /* Sorveglianza TX: nessun DE lasciato alto per UART guasta. */
    }
    else if (tx_state == 6)
    {
        BeginBreak();
    }
    else if (tx_state)
    {
        ++tx_faults;
        TxPin(false, true);
        ReceiveDirection();
        listen_until = HAL_GetTick() + 30u;
        TimerArm(1000);
    }
    else NodeTryStart();
}

/* Pacchetto completo in RAM; chiamato dal main per risposte RDM.
   Discovery senza BREAK, normale con BREAK/MAB; turnaround di almeno 176 us, margine di campionamento incluso. */
bool DMX512_SendPacket(const uint8_t *data, uint16_t count, bool discovery, uint32_t request_end)
{
    uint32_t age = DMX512_Cycles() - request_end;
    uint32_t per_us = SystemCoreClock / 1000000u;
    if (!port_uart || !data || !count || count > sizeof tx_data || age > DMX_RDM_SERVICE_US * per_us) return false;
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    if (tx_state || rdm_ready ||
        (bus_state != BUS_IDLE && bus_state != BUS_RDM_PROCESSING) ||
        (port_uart->Instance->ISR & (USART_ISR_BUSY | USART_ISR_RXNE_RXFNE |
                                     USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)))
    {
        __set_PRIMASK(key);
        return false;
    }
    memcpy(tx_data, data, count);
    tx_count = count;
    tx_index = 0;
    bus_state = BUS_RDM_REPLY;
    tx_state = discovery ? 5 : 6;
    uint32_t elapsed = (DMX512_Cycles() - request_end) / per_us;
    TimerArm((uint16_t)(elapsed < 200u ? 200u - elapsed : 1u));
    __set_PRIMASK(key);
    return true;
}

bool DMX512_TxBusy(void)
{
    return tx_state != 0;
}

void DMX512_ObserveController(void)
{
    controller_seen = true;
    controller_at = HAL_GetTick();
    /* Chiamata dal main dopo verifica completa del pacchetto, incluso CRC.
       BREAK/SC e pacchetto parziale sono gia' protetti dagli stati BUS_RX;
       rumore, pacchetti interrotti e CRC errati non rinnovano i 30 secondi. */
    rdm_controller_at = HAL_GetTick();
    rdm_controller_seen = true;
    ++rdm_seen;
}

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
    applied_port = DMX_PORT_IN;
    tx_state = 0;
    rdm_ready = rdm_next_ready = false;
    rx_activity_seen = controller_seen = rdm_controller_seen = false;
    rdm_controller_at = 0;
    bus_state = BUS_IDLE;
    port_uart = huart;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __HAL_RCC_TIM7_CLK_ENABLE();
    TIM7->PSC = (HAL_RCC_GetPCLK1Freq() * 2u / 1000000u) - 1u;
    HAL_NVIC_SetPriority(TIM7_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM7_IRQn);
    ReceiveDirection();
    if (huart->RxState != HAL_UART_STATE_READY) return;
    huart->ReceptionType = HAL_UART_RECEPTION_STANDARD;
    huart->RxISR = NULL;
    UART_MASK_COMPUTATION(huart);
    huart->ErrorCode = HAL_UART_ERROR_NONE;
    huart->RxState = HAL_UART_STATE_BUSY_RX;
    ATOMIC_SET_BIT(huart->Instance->CR3, USART_CR3_EIE);
    ATOMIC_SET_BIT(huart->Instance->CR1, USART_CR1_RXNEIE_RXFNEIE);
}

/* USART1: ricezione DMX e RDM, più TXE/TC dello stesso proprietario.
   Il ricevitore è disabilitato solo durante guida del transceiver, non nelle finestre RX. */
void DMX512UARTInterrupt(UART_HandleTypeDef *huart)
{
    uint32_t flags = READ_REG(huart->Instance->ISR);
    if (tx_state == 3 && (flags & USART_ISR_TXE_TXFNF))
    {
        if (tx_index < tx_count) huart->Instance->TDR = tx_data[tx_index++];
        else
        {
            CLEAR_BIT(huart->Instance->CR1, USART_CR1_TXEIE_TXFNFIE);
            SET_BIT(huart->Instance->CR1, USART_CR1_TCIE);
            tx_state = 4;
        }
    }
    if (tx_state == 4 && (flags & USART_ISR_TC))
    {
        ++frames_sent;
        ReceiveDirection();
        listen_until = HAL_GetTick() + ((++listen_phase & 31u) == 0 ? 20u : 2u);
        if (applied_port != DMX_PORT_IN) TimerArm(2000);
    }
    if (!(flags & (USART_ISR_RXNE_RXFNE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE))) return;
    uint8_t data = (uint8_t)READ_REG(huart->Instance->RDR);
    if (flags & USART_ISR_RXNE_RXFNE) ++rx_bytes;
    if (flags & USART_ISR_FE) ++rx_fe;
    if (flags & USART_ISR_NE) ++rx_ne;
    if (flags & USART_ISR_ORE) ++rx_ore;
    if (tx_state)
    {
        /* Anche durante TX FE/NE/ORE restano sorgenti IRQ finché non azzerati.
           Scartare il dato senza ICR può bloccare la CPU nell'ISR fino al watchdog. */
        __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_OREF);
        return;
    }
    ReceiverExpire();
    rx_last_cycles = DMX512_Cycles();
    rx_activity_seen = true;
    rx_activity_at = HAL_GetTick();
    if (flags & (USART_ISR_NE | USART_ISR_ORE))
    {
        __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_NEF | UART_CLEAR_OREF | UART_CLEAR_FEF);
        ReceiverAbort();
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
            ++dmx_frames;
            frame_time = HAL_GetTick();
            frame_ready = true;
            if (DMX512_Cycles() - frame_start >= 1196u * (SystemCoreClock / 1000000u))
            {
                controller_seen = true;
                controller_at = HAL_GetTick();
            }
        }
        rx_count = rdm_count = 0;
        receiving_rdm = false;
        rx_index = data == 0 ? -1 : -2;
        frame_start = DMX512_Cycles();
        rx_started_at = HAL_GetTick();
        bus_state = data == 0 ? BUS_START_CODE : BUS_IDLE;
        return;
    }
    if (!(flags & USART_ISR_RXNE_RXFNE) || rx_index == -2) return;
    if (rx_index == -1)
    {
        receiving_rdm = data == 0xCC;
        if (receiving_rdm)
        {
            bus_state = BUS_RDM_RX;
            rx_started_cycles = DMX512_Cycles();
            rdm_staging[0] = data;
            rdm_count = 1;
            rx_index = 0;
        }
        else
        {
            rx_index = data == DMX_ST_CODE ? 0 : -2;
            bus_state = data == DMX_ST_CODE ? BUS_DMX_RX : BUS_IDLE;
        }
    }
    else if (receiving_rdm)
    {
        if (rdm_count >= sizeof rdm_staging) { ReceiverAbort(); return; }
        rdm_staging[rdm_count++] = data;
        if (rdm_count >= 3 && rdm_staging[2] < 24) { ReceiverAbort(); return; }
        if (rdm_count >= 26 && rdm_count == (uint16_t)rdm_staging[2] + 2u)
        {
            ++rx_packets;
            if (!rdm_ready)
            {
                memcpy(rdm_pending, rdm_staging, rdm_count);
                rdm_length = rdm_count;
                rdm_end = DMX512_Cycles();
                rdm_ready = true;
                bus_state = BUS_RDM_PENDING;
            }
            else if (!rdm_next_ready)
            {
                memcpy(rdm_next, rdm_staging, rdm_count);
                rdm_next_length = rdm_count;
                rdm_next_end = DMX512_Cycles();
                rdm_next_ready = true;
                bus_state = BUS_RDM_PENDING;
            }
            else ++rx_dropped;
            bus_state = BUS_RDM_PENDING;
            rx_index = -2;
        }
    }
    else
    {
        staging[rx_index++] = data;
        rx_count++;
        if (rx_index == 512)
        {
            rx_index = -2;
            bus_state = BUS_IDLE;
            controller_seen = true;
            controller_at = HAL_GetTick();
        }
    }
}

/* Sezione critica limitata a copia 512 byte: impedisce pubblicazioni strappate. */
void DMX512_Service(void)
{
    if (!port_uart) return;
    uint32_t expiration_lock = __get_PRIMASK();
    __disable_irq();
    ReceiverExpire();
    (void)RDMControllerPresent(HAL_GetTick());
    __set_PRIMASK(expiration_lock);
    if (applied_port != dmx_port_mode && !DMX512_BusBusy())
    {
        /* Il cambio direzione attende TC/guardia; nessun frame spezzato sul filo. */
        uint32_t lock = __get_PRIMASK();
        __disable_irq();
        if (!DMX512_BusBusy() && !(port_uart->Instance->ISR &
            (USART_ISR_BUSY | USART_ISR_RXNE_RXFNE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)))
        {
            applied_port = dmx_port_mode;
            frame_ready = have_frame = false;
            rdm_ready = rdm_next_ready = false;
            dmx_timeout = 0;
            dmxRxCount = 0;
            memset((void *)dmxRxBuffer, 0, 512);
            memset(node_slots, 0, 512);
            node_at = HAL_GetTick();
            rx_activity_seen = controller_seen = false;
            /* Conserva la cessione RDM: il SET IN->Node non riavvia il TX
               mentre il tool sta ancora configurando il dispositivo. */
            ReceiveDirection();
            TimerStop();
            listen_until = HAL_GetTick() + 2000u;
            if (applied_port != DMX_PORT_IN) TimerArm(1000);
        }
        __set_PRIMASK(lock);
    }
    /* Messaggio completo non significa linea gia' libera: non consumare lo
       snapshot se BUSY persiste. Il main riprova al giro successivo, senza
       attese; un nuovo BREAK mantiene la propria gestione e la deadline
       esistente libera comunque una transazione che non termina. */
    bool wait_uart = rdm_ready && bus_state == BUS_RDM_PENDING &&
                     (port_uart->Instance->ISR & USART_ISR_BUSY);
    if (wait_uart) ++rx_deferred;
    if (rdm_ready && !wait_uart)
    {
        uint8_t packet[257];
        uint32_t lock = __get_PRIMASK();
        __disable_irq();
        uint16_t count = rdm_length;
        uint32_t end = rdm_end;
        memcpy(packet, rdm_pending, count);
        if (rdm_next_ready)
        {
            memcpy(rdm_pending, rdm_next, rdm_next_length);
            rdm_length = rdm_next_length;
            rdm_end = rdm_next_end;
            rdm_next_ready = false;
        }
        else rdm_ready = false;
        /* Una nuova richiesta iniziata nel frattempo conserva la proprietà RX:
           la vecchia non può rispondere nella sua pausa inter-byte. */
        if (!tx_state && (bus_state == BUS_RDM_PENDING || bus_state == BUS_IDLE)) bus_state = BUS_RDM_PROCESSING;
        __set_PRIMASK(lock);
        bool valid = RDM_PacketValid(packet, count);
        RDM_Receive(packet, count, end);
        lock = __get_PRIMASK();
        __disable_irq();
        if (bus_state == BUS_RDM_PROCESSING && rdm_ready)
        {
            bus_state = BUS_RDM_PENDING;
        }
        else if (bus_state == BUS_RDM_PROCESSING)
        {
            /* Richiesta per un altro responder: ascolta la sua risposta.
               Un pacchetto risposta o un errore completa invece la ricezione. */
            if (valid && (packet[20] == 0x10 || packet[20] == 0x20 || packet[20] == 0x30))
            {
                bus_state = BUS_OTHER_REPLY;
                rx_started_at = HAL_GetTick();
            }
            else bus_state = BUS_IDLE;
        }
        __set_PRIMASK(lock);
    }
    if (applied_port != DMX_PORT_IN)
    {
        const uint8_t *slots = NULL;
        uint16_t count = applied_port == DMX_PORT_ARTNET_NODE ?
                         ArtNet_GetSlotsView(artnet_universe, &slots) : sACN_GetSlotsView(sacn_universe, &slots);
        uint32_t lock = __get_PRIMASK();
        __disable_irq();
        if (slots && count) memcpy(node_slots, slots, count);
        else count = 0;
        if (count < 512) memset(node_slots + count, 0, 512u - count);
        node_at = HAL_GetTick();
        __set_PRIMASK(lock);
    }
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
