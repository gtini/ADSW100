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
int32_t    rlyTimer[N_RLY];

// Modalità manuale
//  - relay_manual_mode = 0 → AUTO (DMX / ArtNet / sACN)
//  - relay_manual_mode = 1 → MANUAL (UI Manual mode)
//  - relay_manual_mask: bit i = ON (1) / OFF (0) desiderato per relè i

static volatile uint8_t  relay_manual_mode = 0;
static volatile uint16_t relay_manual_mask = 0;

// ---------------------------------------------------------------------------
//                      FUNZIONI DI SUPPORTO (PULSE)
// ---------------------------------------------------------------------------

// Impulso ON blocking (usato SOLO in manuale)
static void Relay_PulseOn(uint8_t idx)
{
    if (idx >= N_RLY) return;

    HAL_GPIO_WritePin((GPIO_TypeDef *)RLY_ON_PORT[idx],
                      (uint16_t)RLY_ON_PIN[idx],
                      GPIO_PIN_SET);
    HAL_Delay(RLY_PULSE);
    HAL_GPIO_WritePin((GPIO_TypeDef *)RLY_ON_PORT[idx],
                      (uint16_t)RLY_ON_PIN[idx],
                      GPIO_PIN_RESET);

    // piccola guardia per sicurezza
    HAL_Delay(RLY_GUARD);
}

// Impulso OFF blocking (usato SOLO in manuale)
static void Relay_PulseOff(uint8_t idx)
{
    if (idx >= N_RLY) return;

    HAL_GPIO_WritePin((GPIO_TypeDef *)RLY_OFF_PORT[idx],
                      (uint16_t)RLY_OFF_PIN[idx],
                      GPIO_PIN_SET);
    HAL_Delay(RLY_PULSE);
    HAL_GPIO_WritePin((GPIO_TypeDef *)RLY_OFF_PORT[idx],
                      (uint16_t)RLY_OFF_PIN[idx],
                      GPIO_PIN_RESET);

    // piccola guardia per sicurezza
    HAL_Delay(RLY_GUARD);
}

// ---------------------------------------------------------------------------
//                      INIZIALIZZAZIONE
// ---------------------------------------------------------------------------

void InitRelay(void)
{
    for (int i = 0; i < N_RLY; i++)
    {
        rlyStatus[i] = ST_UNKN;
        rlyTimer[i]  = 0;
    }

    relay_manual_mode = 0;
    relay_manual_mask = 0;
}

// ---------------------------------------------------------------------------
//                      API MANUAL MODE (UI)
// ---------------------------------------------------------------------------

// Bitmask relè ON
// - in manuale: usa relay_manual_mask (desiderato dall'utente)
// - in automatico: deriva da rlyStatus[] (stato effettivo noto)
uint16_t Relay_GetOnMask(void)
{
    if (relay_manual_mode)
    {
        return relay_manual_mask;
    }

    uint16_t m = 0;
    for (int i = 0; i < N_RLY; i++)
    {
        if (rlyStatus[i] == ST_ON)
            m |= (1u << i);
    }
    return m;
}

// Entrata in modalità manuale (chiamata quando entri in pagina Manual mode)
void Relay_ManualEnter(void)
{
    relay_manual_mode = 1;

    // All’ingresso in manuale, inizializza la mask allo stato attuale
    uint16_t m = 0;
    for (int i = 0; i < N_RLY; i++)
    {
        if (rlyStatus[i] == ST_ON)
            m |= (1u << i);
    }
    relay_manual_mask = m;
}

// Uscita da modalità manuale (click lungo in pagina manuale)
void Relay_ManualExit(void)
{
    relay_manual_mode = 0;
    // Non forziamo nessun cambiamento: i relè restano come li hai lasciati.
}

// Toggle di un singolo relè in manuale (0..N_RLY-1)
void Relay_ManualToggle(uint8_t idx)
{
    if (idx >= N_RLY) return;

    uint16_t bit = (uint16_t)(1u << idx);

    if (relay_manual_mask & bit)
    {
        // Stato desiderato: ON → user vuole OFF
        relay_manual_mask &= (uint16_t)(~bit);

        // Impulso OFF immediato
        Relay_PulseOff(idx);
        rlyStatus[idx] = ST_OFF;
    }
    else
    {
        // Stato desiderato: OFF → user vuole ON
        relay_manual_mask |= bit;

        // Impulso ON immediato
        Relay_PulseOn(idx);
        rlyStatus[idx] = ST_ON;
    }
}

// ---------------------------------------------------------------------------
//                      LOGICA AUTOMATICA (DMX / ArtNet / sACN)
// ---------------------------------------------------------------------------
//
//  RelayTask() viene chiamata ogni 1 ms.
//
//  - Se relay_manual_mode == 1:
//        --> non fa NULLA, esce subito.
//        --> i relè sono comandati SOLO da Relay_ManualToggle().
//
//  - Se relay_manual_mode == 0:
//        --> logica originale basata su DMX / ArtNet / sACN
//


// ---------------------------------------------------------------------------
//                AUTO protocol selection (dmx_mode == 3)
// ---------------------------------------------------------------------------

static const uint8_t s_prio_tbl[6][3] =
{
    {0, 1, 2}, // D, A, s
    {0, 2, 1}, // D, s, A
    {2, 0, 1}, // s, D, A
    {2, 1, 0}, // s, A, D
    {1, 0, 2}, // A, D, s
    {1, 2, 0}, // A, s, D
};

static bool Relay_IsProtoAvailable(uint8_t proto)
{
    const uint8_t *p = NULL;
    uint16_t v = 0;

    switch (proto)
    {
        case 0: // DMX fisico
            return (dmx_timeout > 0);

        case 1: // ArtNet
            v = ArtNet_GetSlotsView(artnet_universe, &p);
            return (v > 0 && p != NULL);

        case 2: // sACN
            v = sACN_GetSlotsView(sacn_universe, &p);
            return (v > 0 && p != NULL);

        default:
            return false;
    }
}

static uint8_t Relay_SelectAutoProto(uint8_t current)
{
    // current può essere sporco su reset/upgrade
    if (current > 2) current = 0;

    // Scegli SEMPRE il protocollo a priorità più alta tra quelli disponibili.
    // Se un protocollo con priorità più alta torna disponibile, deve prevaricare
    // immediatamente (comportamento richiesto in AUTO).
    uint8_t pri = (proto_priority < 6) ? proto_priority : 0;

    for (int k = 0; k < 3; k++)
    {
        uint8_t cand = s_prio_tbl[pri][k];
        if (Relay_IsProtoAvailable(cand))
            return cand;
    }

    // Nessuna disponibile: resta sull'ultima attiva (anche se "assente")
    return current;
}

void RelayTask(void)
{
    // In modalità manuale, il driver automatico è completamente disabilitato
    // per evitare che DMX/ArtNet/sACN interferiscano con i comandi manuali.
    if (relay_manual_mode)
        return;

    // Timeout spegnimento relè: off_timeout_s (0..30s) -> ms
    // Nota: il driver gira a tick 1ms.
    int32_t off_dly_ms = (int32_t)off_timeout_s * 1000;
    if (off_dly_ms < 0) off_dly_ms = 0;
    if (off_dly_ms > 30000) off_dly_ms = 30000;

    int i, j;
    const volatile uint8_t *activeBuffer = NULL;
    uint16_t slots_valid = 0; // 0 = nessun dato valido

    // Indice canale di partenza (0..511)
    uint16_t ch_idx = (dmx_address > 0) ? (uint16_t)(dmx_address - 1) : 0;
    if (ch_idx > 511) ch_idx = 511;

    // Sorgente in base a dmx_mode

    // Determina sorgente attiva (Auto -> selezione runtime con priorità)
    uint8_t src_mode = dmx_mode;
    if (dmx_mode == 3)
    {
        active_proto = Relay_SelectAutoProto(active_proto);
        src_mode = active_proto;
    }
    else
    {
        // Modalità fissa (0..2)
        active_proto = dmx_mode;
    }

    switch (src_mode)
    {
        case 0: /* DMX fisico */
            activeBuffer = dmxRxBuffer;
            slots_valid  = 512;
            break;

        case 1: /* Art-Net */
        {
            const uint8_t *p = NULL;
            uint16_t v = ArtNet_GetSlotsView(artnet_universe, &p);
            activeBuffer = p;     // NULL se scaduto/assente --> “tutto zero”
            slots_valid  = v;     // 0 se scaduto/assente
        }
        break;

        case 2: /* sACN */
        {
            const uint8_t *p = NULL;
            uint16_t v = sACN_GetSlotsView(sacn_universe, &p);
            activeBuffer = p;     // può essere NULL se scaduto --> "tutto zero"
            slots_valid  = v;     // 0 se scaduto
        }
        break;

        case 3: /* Auto (TODO: selezione in base a proto_priority) */
            // Per ora, comportamento neutro: usa DMX fisico.
            activeBuffer = dmxRxBuffer;
            slots_valid  = 512;
            break;

        default:
            activeBuffer = NULL;
            slots_valid  = 0;
            break;
    }

    // Logica originale
    for (i = 0, j = ch_idx; i < N_RLY && j < 512; i++, j++)
    {
        // Se non c'è dato valido o siamo oltre slots_valid, il livello è 0
        uint8_t level = (activeBuffer && (j < slots_valid)) ? activeBuffer[j] : 0;

        if (rlyTimer[i] > 0)                     // sequenza relay ON
        {
            rlyTimer[i]++;

            if (rlyStatus[i] == ST_ON)
            {
                if (rlyTimer[i] > RLY_PULSE)
                {
                    rlyStatus[i] = ST_GUARD;
                    HAL_GPIO_WritePin((GPIO_TypeDef *)RLY_ON_PORT[i],
                                      (uint16_t)RLY_ON_PIN[i],
                                      GPIO_PIN_RESET);
                }
            }
            else if (rlyTimer[i] > (RLY_PULSE + RLY_GUARD))
            {
                rlyTimer[i]  = 0;
                rlyStatus[i] = ST_ON;
            }
        }
        else if (rlyTimer[i] < 0)                // sequenza relay OFF
        {
            rlyTimer[i]--;

            if (rlyStatus[i] == ST_OFF)
            {
                if (rlyTimer[i] < -((int32_t)off_dly_ms + RLY_PULSE))
                {
                    rlyStatus[i] = ST_GUARD;
                    HAL_GPIO_WritePin((GPIO_TypeDef *)RLY_OFF_PORT[i],
                                      (uint16_t)RLY_OFF_PIN[i],
                                      GPIO_PIN_RESET);
                }
            }
            else if (rlyStatus[i] == ST_GUARD)
            {
                if (rlyTimer[i] < -((int32_t)off_dly_ms + RLY_PULSE + RLY_GUARD))
                {
                    rlyTimer[i]  = 0;
                    rlyStatus[i] = ST_OFF;
                }
            }
            else if (level > 127)
            {
                rlyTimer[i] = 0;  // annulla sequenza OFF se torna alto
            }
            else if (rlyTimer[i] < -(int32_t)off_dly_ms)
            {
                rlyStatus[i] = ST_OFF;
                HAL_GPIO_WritePin((GPIO_TypeDef *)RLY_OFF_PORT[i],
                                  (uint16_t)RLY_OFF_PIN[i],
                                  GPIO_PIN_SET);
            }
        }
        else // rlyTimer[i] == 0: nuovi comandi da livello corrente
        {
            if (level > 127)                     // richiesta ON
            {
                if (rlyStatus[i] != ST_ON)
                {
                    rlyTimer[i]  = 1;
                    rlyStatus[i] = ST_ON;
                    HAL_GPIO_WritePin((GPIO_TypeDef *)RLY_ON_PORT[i],
                                      (uint16_t)RLY_ON_PIN[i],
                                      GPIO_PIN_SET);
                }
            }
            else // level < 128 → richiesta OFF
            {
                if (rlyStatus[i] != ST_OFF)
                {
                    rlyTimer[i] = -1; // parte la sequenza OFF (rispetta ritardi)
                }
            }
        }
    }
}