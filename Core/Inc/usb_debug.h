/*
 * usb_debug.h
 *
 *  Created on: Jul 29, 2025
 *      Author: gianlucatini
 */

#ifndef __USB_DEBUG_H__
#define __USB_DEBUG_H__

#include "main.h"
#include "h_files.h"

void USB_Send(const char *msg);
void USB_Printf(const char *fmt, ...);

void RelayConsole_Init(void);
void USB_Debug_OnRxBytes(const uint8_t *buf, uint32_t len);

#endif // __USB_DEBUG_H__
