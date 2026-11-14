/*
 * h_files.h
 *
 *  Created on: Aug 10, 2025
 *      Author: gianlucatini
 */

#ifndef INC_H_FILES_H_
#define INC_H_FILES_H_

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "eeprom.h"
#include "LAN9303.h"
#include "usb_debug.h"
#include "network.h"
#include "defs.h"
#include "relay_driver.h"
#include "dmx_512.h"
#include "user_interface.h"
#include "rtc_i2c.h"
#include "nfc_i2c.h"
#include "artnet.h"
#include "sacn.h"
#include "lcd.h"
#include "cfg_io.h"
#include "eth_custom_phy_interface.h"

// Funzioni pubbliche  di main.c

/* true: reset utente preservando identità di fabbrica; false: inizializzazione esplicita. */
void HardFactoryReset(bool preserve_identity);
void LCD_Backlight_Wake(void);

typedef enum {
    RST_TAG_NONE       = 0,
    RST_TAG_HARDFAULT  = 1,
    RST_TAG_MEMFAULT   = 2,
    RST_TAG_BUSFAULT   = 3,
    RST_TAG_USAGEFAULT = 4,
    RST_TAG_HFR        = 10,  // HardFactoryReset
    RST_TAG_ERRORH     = 20,  // Error_Handler
} reset_tag_t;

reset_tag_t ResetTag_Read(void);
void ResetTag_Write(reset_tag_t tag);
const char *ResetTag_ToString(reset_tag_t tag);

typedef struct {
    uint32_t pc;
    uint32_t lr;
} reset_crash_info_t;

reset_crash_info_t ResetTag_ReadCrashInfo(void);
void ResetTag_ClearCrashInfo(void);
void ResetTag_SaveCrashInfo(uint32_t pc, uint32_t lr);


#endif /* INC_H_FILES_H_ */
