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
/* Main, non bloccante: servizio fabbrica CDC, scritture solo tick < 30000. */
void USB_ServiceTask(void);
/* Main: reader provisioning con sole query boot?/i2c?/ee? anche in attesa. */
int USB_ReadLineTimeout(char *out, size_t maxlen, uint32_t timeout_ms);
/* Definita in main.c; fotografia cached, nessun accesso EEPROM. */
void Startup_FormatDiagnostics(char *out, size_t capacity);

#endif // __USB_DEBUG_H__
