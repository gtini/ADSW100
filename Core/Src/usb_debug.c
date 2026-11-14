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

/* Risposte di servizio persistenti e prioritarie rispetto ai log diagnostici. */
static char s_service_response[64];
static bool s_service_response_pending;
static bool s_factory_window_closed;

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

/* Un solo invio in volo: buffer statico posseduto da CDC fino al completamento.
   I messaggi diagnostici eccedenti il buffer o ricevuti durante BUSY sono scartati. */
void USB_Send(const char *msg)
{
    static uint8_t tx[256];
    if (!msg || !CDC_IsConfigured() || s_service_response_pending) return;
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    USBD_CDC_HandleTypeDef *cdc = hUsbDeviceFS.pClassData;
    if (cdc && cdc->TxState == 0)
    {
        size_t n = strlen(msg);
        if (n > sizeof tx) n = sizeof tx;
        memcpy(tx, msg, n);
        (void)CDC_Transmit_FS(tx, (uint16_t)n);
    }
    __set_PRIMASK(key);
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
static bool s_line_discard;

static inline void linebuf_reset(void){
  s_line_len = 0;
  s_line_ready = 0;
  s_line_buf[0] = 0;
  s_line_discard = false;
}

/* Main: copia atomica della riga pubblicata dall'ISR USB; nessun I/O in IRQ. */
static bool USB_ServiceTakeLine(char *out, size_t capacity)
{
    if (!out || !capacity) return false;
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    bool ready = s_line_ready != 0;
    if (ready)
    {
        size_t n = s_line_len < capacity - 1 ? s_line_len : capacity - 1;
        memcpy(out, s_line_buf, n);
        out[n] = 0;
        linebuf_reset();
    }
    __set_PRIMASK(key);
    return ready;
}

/* Stessa forma decimale di RSH7: solo cifre, nessun segno o suffisso. */
static bool USB_ServiceParseValue(const char *text, uint8_t *out)
{
    if (!text || !*text) return false;
    unsigned value = 0;
    while (*text)
    {
        if (*text < '0' || *text > '9') return false;
        value = value * 10u + (unsigned)(*text++ - '0');
        if (value > 255) return false;
    }
    *out = (uint8_t)value;
    return true;
}

/* Scritture solo nei primi 30 s da HAL_Init, come la deadline assoluta RSH7.
   La chiusura resta memorizzata anche dopo il wrap di HAL_GetTick. */
void USB_ServiceTask(void)
{
    if (HAL_GetTick() >= 30000u) s_factory_window_closed = true;
    USBD_CDC_HandleTypeDef *cdc = hUsbDeviceFS.pClassData;
    if (!CDC_IsConfigured() || !cdc || cdc->TxState != 0) return;
    if (s_service_response_pending)
    {
        if (CDC_Transmit_FS((uint8_t *)s_service_response, (uint16_t)strlen(s_service_response)) == USBD_OK)
        {
            s_service_response_pending = false;
        }
        return;
    }
    char line[160];
    if (!USB_ServiceTakeLine(line, sizeof line)) return;
    const char *response = "ERROR\r\n";
    bool brand_query = strcmp(line, "brand?") == 0;
    bool code_query = strcmp(line, "menu_code?") == 0;
    bool brand_write = strncmp(line, "brand=", 6) == 0;
    bool code_write = strncmp(line, "menu_code=", 10) == 0;
    if (strcmp(line, "rdm?") == 0)
    {
        RDM_FormatDiagnostics(s_service_response, sizeof s_service_response);
        s_service_response_pending = true;
        return;
    }
    if (brand_query || code_query)
    {
        uint8_t id, code;
        if (EE_ReadIdentity(&id, &code) == HAL_OK)
        {
            snprintf(s_service_response, sizeof s_service_response, "%s=%u\r\n",
                     brand_query ? "brand" : "menu_code", (unsigned)(brand_query ? id : code));
            s_service_response_pending = true;
            return;
        }
    }
    else if (brand_write || code_write)
    {
        uint8_t value;
        if (USB_ServiceParseValue(line + (brand_write ? 6 : 10), &value) &&
            (brand_write ? value < BRAND_COUNT : value <= 1))
        {
            if (s_factory_window_closed)
            {
                response = "LOCKED\r\n";
            }
            else
            {
                uint8_t id, code;
                if (EE_ReadIdentity(&id, &code) == HAL_OK &&
                    EE_SaveIdentity(brand_write ? value : id, brand_write ? Brand_GetInfo(value)->menu_code_default : value) == HAL_OK)
                {
                    response = "OK REBOOT\r\n";
                }
            }
        }
    }
    snprintf(s_service_response, sizeof s_service_response, "%s", response);
    s_service_response_pending = true;
}

/* Compatibilità con il provisioning FGM preesistente; lettura IRQ-safe. */
int USB_ReadLineTimeout(char *out, size_t maxlen, uint32_t timeout_ms)
{
    if (!out || !maxlen) return 0;
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < timeout_ms)
    {
        if (USB_ServiceTakeLine(out, maxlen)) return (int)strlen(out);
    }
    return 0;
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

/* ISR CDC: una riga alla volta, CR/LF. Overflow e controlli non stampabili
   invalidano l'intera riga fino al terminatore, senza accettarne un suffisso. */
void USB_Debug_OnRxBytes(const uint8_t *buf, uint32_t len)
{
    if (!buf || !len) return;
    for (uint32_t i = 0; i < len; ++i)
    {
        uint8_t c = buf[i];
        if (!s_line_ready)
        {
            if (c == '\r' || c == '\n')
            {
                if (s_line_discard)
                {
                    s_line_buf[0] = '?';
                    s_line_buf[1] = 0;
                    s_line_len = 1;
                }
                if (s_line_len) s_line_ready = 1;
            }
            else if (c < 32 || c > 126 || s_line_len >= sizeof s_line_buf - 1)
            {
                s_line_discard = true;
            }
            else if (!s_line_discard)
            {
                s_line_buf[s_line_len++] = (char)c;
                s_line_buf[s_line_len] = 0;
            }
        }
        RelayConsole_ProcessByte(c);
    }
}
