/*
 * usb_debug.c
 *
 *  Created on: Jul 29, 2025
 *      Author: gianlucatini
 */
#include "usb_debug.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include "usbd_cdc_if.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "usbd_cdc.h"
extern USBD_HandleTypeDef hUsbDeviceFS;

/* ====== Config ====== */
#ifndef USB_LOG_BUDGET_MS
#define USB_LOG_BUDGET_MS  2   /* massimo tempo speso per provare a inviare (ms) */
#endif

static inline int CDC_IsConfigured(void) {
  return (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED);
}

/* opzionale: se in CDC_Control_FS gestisci DTR, usa anche quello per essere più “precisi” */
// extern volatile uint8_t cdc_dtr;  // settalo in CDC_SET_CONTROL_LINE_STATE
// static inline int CDC_IsReady(void) { return CDC_IsConfigured() && cdc_dtr; }

void USB_Send(const char *msg)
{
  if (msg == NULL) return;
  if (!CDC_IsConfigured()) return;                   /* cavo non connesso/porta non aperta: drop */

  uint16_t len = (uint16_t)strlen(msg);
  uint16_t sent = 0;
  uint32_t t0 = HAL_GetTick();

  while (sent < len) {
    uint16_t chunk = len - sent;
#ifdef CDC_DATA_FS_MAX_PACKET_SIZE
    if (chunk > CDC_DATA_FS_MAX_PACKET_SIZE) chunk = CDC_DATA_FS_MAX_PACKET_SIZE;
#else
    if (chunk > 64) chunk = 64;                      /* fallback FS */
#endif
    uint8_t *ptr = (uint8_t *)&msg[sent];

    if (CDC_Transmit_FS(ptr, chunk) == USBD_OK) {
      sent += chunk;
    } else {
      /* BUSY: non bloccare — prova per poco, poi lascia perdere */
      if ((HAL_GetTick() - t0) > USB_LOG_BUDGET_MS) break;
      /* piccolo yield: NO HAL_Delay lunghi */
    }
  }
}

void USB_Printf(const char *fmt, ...)
{
  if (!CDC_IsConfigured()) return;                   /* drop se non pronta */
  char buffer[256];
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  if (n <= 0) return;
  /* se il messaggio supera il buffer, inviamo quello che c'è (n viene clampato da vsnprintf) */
  USB_Send(buffer);
}

/* === Line buffer per sblocco semplice === */
static volatile uint8_t  s_line_ready = 0;
static char              s_line_buf[160];
static volatile uint32_t s_line_len = 0;

static inline void linebuf_reset(void){
  s_line_len = 0;
  s_line_ready = 0;
  s_line_buf[0] = 0;
}

/* Bloccante con timeout "soft": aspetta una riga finché non arriva CR/LF */
int USB_ReadLineTimeout(char *out, size_t maxlen, uint32_t timeout_ms)
{
  if (!out || maxlen == 0) return 0;
  uint32_t t0 = HAL_GetTick();
  while ((HAL_GetTick() - t0) < timeout_ms) {
    if (s_line_ready) {
      size_t n = (s_line_len < (maxlen-1)) ? s_line_len : (maxlen-1);
      memcpy(out, s_line_buf, n);
      out[n] = 0;
      linebuf_reset();
      return (int)n;
    }
    /* piccolo yield non bloccante */
  }
  return 0; /* timeout */
}

/* ================= Relay console (USB CDC) ================= */

//static int8_t s_rel_shadow[12] = {
//  /* stato software dei 12 relè: -1=unknown, 0=RESET, 1=SET */
//  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
//};

void RelayConsole_Init(void)
{
  USB_Printf("Relay console pronta.\r\n");
  USB_Printf("Comandi: 0-9,A-B = toggle CH01..CH12 | o=ALL ON | f=ALL OFF | s=stato | ?=help\r\n");
}

//static int char_to_index(uint8_t c)
//{
//  if (c >= '0' && c <= '9') return (int)(c - '0');  // 0..9 -> CH01..CH10
//  c = (uint8_t)toupper(c);
//  if (c == 'A') return 10;                           // A -> CH11
//  if (c == 'B') return 11;                           // B -> CH12
//  return -1;
//}

static void RelayConsole_ProcessByte(uint8_t c)
{
//  int idx = char_to_index(c);
//  if (idx >= 0) {
//    /* toggle: se sconosciuto parte con SET */
//    int desired = (s_rel_shadow[idx] == 1) ? REL2C_OFF : REL2C_ON;
//
//    if (Relay2C_IsBusy((uint8_t)idx)) {
//      USB_Printf("CH%02d BUSY\r\n", idx+1);
//      return;
//    }
//    HAL_StatusTypeDef st = Relay2C_Command((uint8_t)idx, (rel2c_state_t)desired);
//    if (st == HAL_OK) {
//      s_rel_shadow[idx] = (int8_t)desired;
//      USB_Printf("CH%02d -> %s\r\n", idx+1, desired ? "SET" : "RESET");
//    } else {
//      USB_Printf("CH%02d ERR(%d)\r\n", idx+1, (int)st);
//    }
//    return;
//  }
//
//  /* comandi extra */
//  switch (c) {
//    case 'o': case 'O':  /* ALL ON */
//      for (uint8_t i=0;i<12;i++) {
//        if (!Relay2C_IsBusy(i) && Relay2C_Command(i, REL2C_ON) == HAL_OK) s_rel_shadow[i]=1;
//      }
//      USB_Printf("ALL SET\r\n");
//      break;
//
//    case 'f': case 'F':  /* ALL OFF */
//      for (uint8_t i=0;i<12;i++) {
//        if (!Relay2C_IsBusy(i) && Relay2C_Command(i, REL2C_OFF) == HAL_OK) s_rel_shadow[i]=0;
//      }
//      USB_Printf("ALL RESET\r\n");
//      break;
//
//    case 's': case 'S':  /* stato */
//      for (uint8_t i=0;i<12;i++) {
//        int v = s_rel_shadow[i];
//        USB_Printf("CH%02d: %s%s\r\n", i+1,
//                   (v<0)?"UNK":(v? "SET":"RESET"),
//                   Relay2C_IsBusy(i) ? " (BUSY)":"");
//      }
//      break;
//
//    case '?':
//      USB_Printf("0-9,A-B toggle | o=ALL ON | f=ALL OFF | s=stato\r\n");
//      break;
//
//    default:
//      break;
//  }
}

/* Entry point da chiamare quando arrivano byte dal CDC */
void USB_Debug_OnRxBytes(const uint8_t *buf, uint32_t len)
{
  if (!buf || !len) return;

  for (uint32_t i=0; i<len; ++i) {
    uint8_t c = buf[i];

    /* --- accumula in line buffer finché CR/LF --- */
    if (!s_line_ready) {
      if (c == '\r' || c == '\n') {
        if (s_line_len > 0) {
          s_line_ready = 1;
        }
      } else if (s_line_len < sizeof(s_line_buf)-1) {
        s_line_buf[s_line_len++] = (char)c;
        s_line_buf[s_line_len]   = 0;
      } else {
        /* overflow: resetta la linea */
        linebuf_reset();
      }
    }

    /* (resto della tua console relay — lasciata com’era) */
    RelayConsole_ProcessByte(c);
  }
}

