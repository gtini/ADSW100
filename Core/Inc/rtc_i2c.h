#ifndef RTC_I2C_H
#define RTC_I2C_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "stm32h7xx_hal.h"

/* =============================================================
 *  Driver I2C per RTC Microchip MCP7940N (ADSW100 / STM32H7)
 *
 *  Funzioni principali:
 *    - RTC_I2C_Init()        : avvio oscillatore e forzatura 24h
 *    - RTC_I2C_ReadTime()    : lettura HH:MM:SS
 *    - RTC_I2C_WriteTime()   : scrittura HH:MM:SS
 *    - RTC_I2C_Format_hh_mm_ss() : formatta "hh_mm_ss" (con zeri)
 *    - RTC_I2C_ShowTime()    : stampa su display tramite hook
 *
 *  Note:
 *    - Indirizzo 7-bit MCP7940 = 0x6F. HAL usa indirizzo a 8-bit
 *      (left-shift di 1).
 *    - Usa API HAL bloccanti per semplicità nel bring-up.
 * ============================================================= */

/* === Configurazione I2C ===
 * Modifica qui se vuoi usare un altro handle I2C.
 * Per richiesta dell'utente, si usa hi2c1.
 */
#ifndef RTC_I2C_HANDLE
#define RTC_I2C_HANDLE hi2c1
#endif

/* Indirizzo I2C (8-bit) atteso da HAL */
#ifndef MCP7940_I2C_ADDR
#define MCP7940_I2C_ADDR  (0x6Fu << 1)
#endif

/* Mappa registri MCP7940 (parziale) */
#define MCP7940_REG_SECONDS   0x00u  /* bit7=ST (Start oscillator), bit6..0=secondi BCD */
#define MCP7940_REG_MINUTES   0x01u
#define MCP7940_REG_HOURS     0x02u  /* bit6=12/24 (0=24h), in 24h bits5..0 = ore BCD */
#define MCP7940_REG_DAY       0x03u  /* giorno della settimana + flag */
#define MCP7940_REG_DATE      0x04u  /* giorno del mese */
#define MCP7940_REG_MONTH     0x05u
#define MCP7940_REG_YEAR      0x06u  /* 00–99 (2000–2099) */
#define MCP7940_REG_CONTROL   0x07u

/* Tipo semplice per l'orario */
typedef struct {
    uint8_t hours;   /* 0–23 */
    uint8_t minutes; /* 0–59 */
    uint8_t seconds; /* 0–59 */
} rtc_time_t;

/* === API di alto livello === */
bool RTC_I2C_Init(void);                                     /* Avvia oscillatore (ST) e imposta 24h */
bool RTC_I2C_ReadTime(rtc_time_t *t);                        /* Legge HH:MM:SS */
bool RTC_I2C_WriteTime(const rtc_time_t *t, bool startOsc);  /* Scrive HH:MM:SS, opz. avvia osc */

/* Accesso generico ai registri (utile per debug) */
bool RTC_I2C_ReadRegs(uint8_t startReg, uint8_t *buf, uint16_t len);
bool RTC_I2C_WriteRegs(uint8_t startReg, const uint8_t *buf, uint16_t len);

/* Utile per UI: format "hh_mm_ss" (8 char + NUL => array[9]) */
bool RTC_I2C_Format_hh_mm_ss(char out[9]);

/* Mostra l'ora sul display usando l'hook di disegno */
void RTC_I2C_ShowTime(void);

/* Visualizza l'orario corrente (hh:mm:ss) sul display
 * posizione fissa: x=2, y=10, font=LCD_FONT_8 */
void RTC_ShowTime_OnLCD(void);

/* === Hook di disegno (weak) ===
 * Implementa questa funzione nel tuo modulo display.
 * Esempio:
 *   void RTC_DrawText(const char *s) { LCD_DrawString(0, 0, s); }
 */
void RTC_DrawText(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* RTC_I2C_H */
