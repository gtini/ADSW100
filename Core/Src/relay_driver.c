/*
 * relay_driver.c
 *
 *  Created on: Aug 15, 2025
 *      Author: gianlucatini
 */

#include "h_files.h"
#include "main.h"           // per CHxx_ON/OFF _GPIO_Port/_Pin generati da CubeMX
#include "relay_driver.h"

// ---------------------------------------------------------------------------
//                      MAPPING PORTE / PIN RELAY
// ---------------------------------------------------------------------------

const GPIO_TypeDef *RLY_ON_PORT[N_RLY] = {
        CH01_ON_GPIO_Port,
        CH02_ON_GPIO_Port,
        CH03_ON_GPIO_Port,
        CH04_ON_GPIO_Port,
        CH05_ON_GPIO_Port,
        CH06_ON_GPIO_Port,
        CH07_ON_GPIO_Port,
        CH08_ON_GPIO_Port,
        CH09_ON_GPIO_Port,
        CH10_ON_GPIO_Port,
        CH11_ON_GPIO_Port,
        CH12_ON_GPIO_Port
};

const uint16_t RLY_ON_PIN[N_RLY] = {
        CH01_ON_Pin,
        CH02_ON_Pin,
        CH03_ON_Pin,
        CH04_ON_Pin,
        CH05_ON_Pin,
        CH06_ON_Pin,
        CH07_ON_Pin,
        CH08_ON_Pin,
        CH09_ON_Pin,
        CH10_ON_Pin,
        CH11_ON_Pin,
        CH12_ON_Pin
};

const GPIO_TypeDef *RLY_OFF_PORT[N_RLY] = {
        CH01_OFF_GPIO_Port,
        CH02_OFF_GPIO_Port,
        CH03_OFF_GPIO_Port,
        CH04_OFF_GPIO_Port,
        CH05_OFF_GPIO_Port,
        CH06_OFF_GPIO_Port,
        CH07_OFF_GPIO_Port,
        CH08_OFF_GPIO_Port,
        CH09_OFF_GPIO_Port,
        CH10_OFF_GPIO_Port,
        CH11_OFF_GPIO_Port,
        CH12_OFF_GPIO_Port
};

const uint16_t RLY_OFF_PIN[N_RLY] = {
        CH01_OFF_Pin,
        CH02_OFF_Pin,
        CH03_OFF_Pin,
        CH04_OFF_Pin,
        CH05_OFF_Pin,
        CH06_OFF_Pin,
        CH07_OFF_Pin,
        CH08_OFF_Pin,
        CH09_OFF_Pin,
        CH10_OFF_Pin,
        CH11_OFF_Pin,
        CH12_OFF_Pin
};

// ---------------------------------------------------------------------------
//                      VARIABILI GLOBALI
// ---------------------------------------------------------------------------

RLY_STATUS rlyStatus[N_RLY];
int32_t rlyTimer[N_RLY];
static volatile uint8_t relay_manual_mode;
static volatile uint16_t relay_manual_mask;
static volatile uint16_t relay_target_mask;
static volatile bool relay_initialized;
static volatile uint16_t stable_mask;
static uint8_t phase[N_RLY];
static uint32_t deadline[N_RLY];

/* Inizializzazione prima del servizio TIM6; nessuna attesa bloccante. */
void InitRelay(void)
{
    relay_initialized = false;
    relay_manual_mode = 0;
    relay_manual_mask = relay_target_mask = stable_mask = 0;
    for (unsigned i = 0; i < N_RLY; ++i)
    {
        HAL_GPIO_WritePin((GPIO_TypeDef *)RLY_ON_PORT[i], RLY_ON_PIN[i], GPIO_PIN_RESET);
        HAL_GPIO_WritePin((GPIO_TypeDef *)RLY_OFF_PORT[i], RLY_OFF_PIN[i], GPIO_PIN_RESET);
        phase[i] = 0;
        rlyTimer[i] = 0;
        rlyStatus[i] = ST_UNKN;
    }
    relay_initialized = true;
}

/* Stato comandato noto: nessun feedback fisico dei contatti disponibile. */
uint16_t Relay_GetOnMask(void)
{
    return relay_manual_mode ? relay_manual_mask : stable_mask;
}

void Relay_ManualEnter(void)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    relay_manual_mask = stable_mask;
    relay_manual_mode = 1;
    __set_PRIMASK(key);
}

void Relay_ManualExit(void)
{
    relay_manual_mode = 0;
}

void Relay_ManualToggle(uint8_t idx)
{
    if (idx < N_RLY)
    {
        relay_manual_mask ^= (uint16_t)(1u << idx);
    }
}

/* ISR TIM6: completa SEMPRE impulsi/guardie, anche con main bloccato.
   Una bobina per uscita, inversione consentita solo dopo la guardia. */
void Relay_PulseService1ms(void)
{
    if (!relay_initialized) return;
    uint32_t now = HAL_GetTick();
    uint16_t target = relay_manual_mode ? relay_manual_mask : relay_target_mask;
    for (unsigned i = 0; i < N_RLY; ++i)
    {
        uint16_t bit = (uint16_t)(1u << i);
        bool on = (target & bit) != 0;
        if (phase[i] == 2 || phase[i] == 3)
        {
            if ((int32_t)(now - deadline[i]) < 0) continue;
            HAL_GPIO_WritePin((GPIO_TypeDef *)RLY_ON_PORT[i], RLY_ON_PIN[i], GPIO_PIN_RESET);
            HAL_GPIO_WritePin((GPIO_TypeDef *)RLY_OFF_PORT[i], RLY_OFF_PIN[i], GPIO_PIN_RESET);
            if (phase[i] == 2) stable_mask |= bit;
            else stable_mask &= (uint16_t)~bit;
            rlyStatus[i] = (stable_mask & bit) ? ST_ON : ST_OFF;
            phase[i] = 4;
            deadline[i] = now + RLY_GUARD;
            continue;
        }
        if (phase[i] == 4)
        {
            if ((int32_t)(now - deadline[i]) < 0) continue;
            phase[i] = 0;
        }
        if (phase[i] == 1)
        {
            if (on) phase[i] = 0;
            else if ((int32_t)(now - deadline[i]) < 0) continue;
        }
        if (phase[i] == 0 && rlyStatus[i] != ST_UNKN && on == ((stable_mask & bit) != 0)) continue;
        if (!on && phase[i] == 0 && !relay_manual_mode && off_timeout_s > 0)
        {
            phase[i] = 1;
            deadline[i] = now + (uint32_t)off_timeout_s * 1000u;
            continue;
        }
        phase[i] = on ? 2 : 3;
        deadline[i] = now + RLY_PULSE;
        HAL_GPIO_WritePin((GPIO_TypeDef *)(on ? RLY_ON_PORT[i] : RLY_OFF_PORT[i]),
                          on ? RLY_ON_PIN[i] : RLY_OFF_PIN[i], GPIO_PIN_SET);
    }
}

static const uint8_t s_prio_tbl[6][3] =
{
    {0,1,2}, {0,2,1}, {2,0,1}, {2,1,0}, {1,0,2}, {1,2,0}
};

/* Seleziona livelli completi pubblicati, mai il buffer UART in costruzione. */
static uint16_t Relay_GetSource(uint8_t proto, const uint8_t **data)
{
    *data = NULL;
    if (proto == 0)
    {
        if (dmx_timeout == 0) return 0;
        *data = (const uint8_t *)dmxRxBuffer;
        return (uint16_t)dmxRxCount;
    }
    if (proto == 1) return ArtNet_GetSlotsView(artnet_universe, data);
    if (proto == 2) return sACN_GetSlotsView(sacn_universe, data);
    return 0;
}

/* Main: pubblica in una sola scrittura i desiderati delle dodici uscite. */
void RelayTask(void)
{
    const uint8_t *data = NULL;
    uint16_t count = 0;
    uint8_t selected = dmx_port_mode == DMX_PORT_ARTNET_NODE ? 1 :
                       dmx_port_mode == DMX_PORT_SACN_NODE ? 2 : dmx_mode;
    if (selected == 3)
    {
        uint8_t pri = proto_priority < 6 ? proto_priority : 0;
        for (unsigned k = 0; k < 3; ++k)
        {
            count = Relay_GetSource(s_prio_tbl[pri][k], &data);
            if (count)
            {
                active_proto = s_prio_tbl[pri][k];
                break;
            }
        }
    }
    else
    {
        active_proto = selected;
        count = Relay_GetSource(active_proto, &data);
    }
    uint16_t mask = 0;
    unsigned address = dmx_address ? dmx_address - 1u : 0u;
    for (unsigned i = 0; i < N_RLY; ++i)
    {
        unsigned ch = address + i;
        if (data && ch < count && ch < 512 && data[ch] > 127) mask |= (uint16_t)(1u << i);
    }
    relay_target_mask = mask;
}
