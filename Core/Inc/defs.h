


// defs.h
#ifndef __DEFS_H
#define __DEFS_H

#include "h_files.h"

/* ===================================================================================== */
/*                                     Costanti                                          */
/* ===================================================================================== */

// Versione Firmware
#define FW_VERSION_MAJOR   1
#define FW_VERSION_MINOR   4
#define FW_VERSION_PATCH   0

// Codice di sblocco
#define UNLOCK_CODE_1	1
#define UNLOCK_CODE_2	2
#define UNLOCK_CODE_3	6
#define UNLOCK_CODE_4	8

// Valori di default
#define CFG_DEFAULT_USE_DHCP		1
#define CFG_DEFAULT_IP				0
#define CFG_DEFAULT_NETMASK			0
#define CFG_DEFAULT_GATEWAY			0
#define CFG_DEFAULT_DNS1			0
#define CFG_DEFAULT_DNS2			0

#define CFG_DEFAULT_DMX_ADDR		1
#define CFG_DEFAULT_DMX_MODE		2   // sACN

#define CFG_DEFAULT_ARTNET_UNI		0
#define CFG_DEFAULT_SACN_UNI		1
#define CFG_DEFAULT_SACN_TRANSPORT	0   // multicast
#define CFG_DEFAULT_SACN_MIN_PRIO	0
#define CFG_DEFAULT_LCD_BL			0
#define CFG_DEFAULT_OFF_TIMEOUT_S   30
#define CFG_DEFAULT_PROTO_PRIORITY  0

/* ===================================================================================== */
/*                               Registri EEPROM                                         */
/* ===================================================================================== */

// 24AA02E64 = 256 byte totali (0x00..0xFF)
// 0xF8..0xFF riservati (EUI-64, read-only)
#define EE_SIZE					256
#define EE_EUI64_BASE			0xF8	// F8..FF (8 byte)

// Header
#define EE_OFFS_INIT			0x00	// 2 byte
#define EE_OFFS_VERSION			0x02	// 1 byte

// Network
#define EE_OFFS_USE_DHCP		0x03	// 1 byte: 0=statico, 1=dhcp
#define EE_OFFS_IP				0x04	// 4 byte (big-endian come lwIP ip4_addr_t.addr)
#define EE_OFFS_NETMASK			0x08	// 4 byte
#define EE_OFFS_GATEWAY			0x0C	// 4 byte
#define EE_OFFS_DNS1			0x10	// 4 byte
#define EE_OFFS_DNS2			0x14	// 4 byte

// DMX
#define EE_OFFS_DMX_ADDR		0x18	// 2 byte (1..512)
#define EE_OFFS_DMX_MODE		0x1A	// 1 byte (0=DMX, 1=ArtNet, 2=sACN, 3=Auto)

// Art-Net
#define EE_OFFS_ARTNET_UNI		0x1C	// 2 byte (0..32767), flat Port-Address

// sACN
#define EE_OFFS_SACN_UNI		0x1E	// 2 byte (1..63999)
#define EE_OFFS_SACN_TRANSPORT	0x20	// 1 byte: 0=multicast, 1=unicast
#define EE_OFFS_SACN_MIN_PRIO	0x21	// 1 byte: 0..200

#define EE_OFFS_LCD_BACKLIGHT	0x22	// 1 byte Luminosità display

// System (v1.3.1+)
#define EE_OFFS_OFF_TIMEOUT_S   0x23    // 1 byte: 0..30 s (timeout spegnimento relè)
#define EE_OFFS_PROTO_PRIORITY  0x24    // 1 byte: 0..5 (ordine protocolli per Auto)

/* Identità: valori marcati per distinguere metadati nuovi da byte legacy indefiniti.
   Nessuno spostamento dei parametri 0x03..0x24. Versione scritta per ultima. */
#define EE_OFFS_BRAND          0x25
#define EE_OFFS_MENU_CODE      0x26
#define EE_BRAND_TAG           0xD0u
#define EE_MENU_CODE_TAG       0xA4u
#define EE_VERSION_PENDING     0xF3u
#define EE_OFFS_DMX_PORT_MODE   0x27
#define EE_DMX_PORT_TAG        0xB0u
/* Spazio applicativo residuo: 0x28..0x7F; EUI esclusa dalle scritture. */

#define EE_INIT_VALUE			0xA55Au
#define EE_IDENTITY_VERSION      0x02u
#define EE_CFG_VERSION           0x03u

/* ===================================================================================== */
/*                               Variabili Globali                                       */
/* ===================================================================================== */

/* Tabella unica implementata in cfg_io.c, riutilizzabile da pannello/RDM/web.
   Manufacturer ESTA AXN Systems confermato dall'utente per tutti i brand.
   OEM resta il placeholder legacy non assegnato: nessun codice prodotto inventato. */
#define BRAND_ESTA_MANUFACTURER_ID 0x17FAu
#define BRAND_ARTNET_OEM_UNASSIGNED 0xFFFFu

typedef enum
{
    BRAND_AXN = 0,
    BRAND_VIDELIO = 1,
    BRAND_GENERIC = 2,
    BRAND_COUNT
} brand_id_t;

typedef struct
{
    const char *manufacturer;
    const char *model;
    uint8_t menu_code_default;
} brand_info_t;

extern uint8_t brand;
extern uint8_t menu_code_required;
/* Restituisce NULL per ID non valido; nessuna accettazione tramite fallback. */
const brand_info_t *Brand_GetInfo(uint8_t id);
/* Generic richiede sempre codice disabilitato; gli altri brand ammettono 0/1. */
bool Brand_IsIdentityValid(uint8_t id, uint8_t require_code);
/* Identità corrente per rendering: fallback sicuro AXN soltanto se RAM invalida. */
const brand_info_t *Brand_Current(void);

/* Ruolo fisico indipendente dal selettore sorgenti legacy dmx_mode. */
typedef enum
{
    DMX_PORT_IN = 0,
    DMX_PORT_ARTNET_NODE = 1,
    DMX_PORT_SACN_NODE = 2
} dmx_port_mode_t;
extern uint8_t dmx_port_mode;

extern uint16_t dmx_address;
extern uint8_t  dmx_mode;				// 0=DMX, 1=ArtNet, 2=sACN, 3 Auto

extern uint8_t  active_proto;			// Protocollo attivo in mode Auto (0=DMX, 1=ArtNet, 2=sACN)
extern uint8_t  off_timeout_s;			// 0..30 secondi
extern uint8_t  proto_priority;			// 0..5 (ordine protocolli per Auto)

extern uint32_t ip_address;
extern uint8_t  use_dhcp;
extern uint32_t net_mask;
extern uint32_t gateway;
extern uint32_t dns1;
extern uint32_t dns2;

extern uint16_t artnet_universe;

extern uint16_t sacn_universe;
extern uint8_t  sacn_transport;
extern uint8_t  sacn_min_prio;

extern uint8_t lcd_backlight;

extern uint8_t g_netmask_bad;


extern volatile bool timer_1ms_flag;	// Flag timer 1mS

/* ===================================================================================== */
/*                                   Macro                                               */
/* ===================================================================================== */

// helper per trasformare il numero in stringa
#define STR_HELPER(x)  #x
#define STR(x)         STR_HELPER(x)

// stringa
#define FW_VERSION_STR  STR(FW_VERSION_MAJOR) "." STR(FW_VERSION_MINOR) "." STR(FW_VERSION_PATCH)



static inline void DMX_LED_On(void)  { HAL_GPIO_WritePin(DMX_LED_GPIO_Port, DMX_LED_Pin, GPIO_PIN_SET); }
static inline void DMX_LED_Off(void) { HAL_GPIO_WritePin(DMX_LED_GPIO_Port, DMX_LED_Pin, GPIO_PIN_RESET); }

static inline void NETLED_On (void) { HAL_GPIO_WritePin(ETH_LED_GPIO_Port, ETH_LED_Pin, GPIO_PIN_SET); }
static inline void NETLED_Off(void) { HAL_GPIO_WritePin(ETH_LED_GPIO_Port, ETH_LED_Pin, GPIO_PIN_RESET); }
static inline void NETLED_Toggle(void) { HAL_GPIO_TogglePin(ETH_LED_GPIO_Port, ETH_LED_Pin); }


#endif /* __DEFS_H */
