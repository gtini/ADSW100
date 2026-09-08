


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

// Spazio libero: 0x23 .. 0xF7
// (nota: da v1.3.1 lo spazio libero parte da 0x25)

#define EE_INIT_VALUE			0xA55Au
#define EE_CFG_VERSION			0x01

/* ===================================================================================== */
/*                               Variabili Globali                                       */
/* ===================================================================================== */

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
