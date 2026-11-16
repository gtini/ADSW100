


#include "h_files.h"
#include <string.h>

/* Handle I2C esterno dichiarato nel progetto */
extern I2C_HandleTypeDef RTC_I2C_HANDLE; /* alias di hi2c1 definito in main.c */

/* ===================== Helper locali ===================== */
/* Conversioni BCD <-> binario */
static inline uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10u + (v & 0x0Fu)); }
static inline uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10u) << 4) | (v % 10u)); }

/* Hook debole per scrivere sul display: default NOP.
 * Ridefinisci nel tuo modulo UI. */
__attribute__((weak)) void RTC_DrawText(const char *s) { (void)s; }

/* =================== Accesso I2C di base ================== */
bool RTC_I2C_ReadRegs(uint8_t startReg, uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0u || ee_nfc_bus_reserved) return false;
    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(&RTC_I2C_HANDLE,
                                            MCP7940_I2C_ADDR,
                                            startReg,
                                            I2C_MEMADD_SIZE_8BIT,
                                            buf,
                                            len,
                                            50u);
    return (st == HAL_OK);
}

bool RTC_I2C_WriteRegs(uint8_t startReg, const uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0u || ee_nfc_bus_reserved) return false;
    HAL_StatusTypeDef st = HAL_I2C_Mem_Write(&RTC_I2C_HANDLE,
                                             MCP7940_I2C_ADDR,
                                             startReg,
                                             I2C_MEMADD_SIZE_8BIT,
                                             (uint8_t*)buf,
                                             len,
                                             50u);
    return (st == HAL_OK);
}

/* ===================== Funzioni core ====================== */
/* Inizializzazione minima:
 *  - Se necessario, imposta ST (Start oscillator) nel registro SECONDS.
 *  - Forza formato 24 ore (bit6=0 nel registro HOURS).
 */
bool RTC_I2C_Init(void)
{
    // 1) Abilita VBATEN (switchover su batteria) nel registro DAY (0x03, bit3)
    uint8_t day;
    if (!RTC_I2C_ReadRegs(MCP7940_REG_DAY, &day, 1u)) {
        return false; // I2C KO
    }
    bool need_write_day = false;
    if ((day & 0x08u) == 0u) {   // 0x08 = VBATEN
        day |= 0x08u;
        need_write_day = true;
    }
    if (need_write_day) {
        if (!RTC_I2C_WriteRegs(MCP7940_REG_DAY, &day, 1u)) {
            return false;
        }
    }

    // 2) Forza formato 24h (bit6=0) preservando il valore corrente delle ore
    uint8_t hrs;
    if (!RTC_I2C_ReadRegs(MCP7940_REG_HOURS, &hrs, 1u)) {
        return false;
    }
    if (hrs & 0x40u) {                 // se in 12h, passa a 24h
        hrs &= (uint8_t)~0x40u;        // bit6=0
        if (!RTC_I2C_WriteRegs(MCP7940_REG_HOURS, &hrs, 1u)) {
            return false;
        }
    }

    // 3) Avvia l'oscillatore: imposta ST=1 nel registro SECONDS (0x00)
    //    senza alterare il valore dei secondi (resta quello letto).
    uint8_t sec;
    if (!RTC_I2C_ReadRegs(MCP7940_REG_SECONDS, &sec, 1u)) {
        return false;
    }

    bool st_was_off = ((sec & 0x80u) == 0u);
    if (st_was_off) {
        uint8_t sec_bcd = (uint8_t)(sec & 0x7Fu); // mantieni i secondi BCD
        uint8_t out = (uint8_t)(sec_bcd | 0x80u); // ST=1

        // (Best practice) Scrivi prima MINUTES+HOURS, poi SECONDS con ST=1
        uint8_t buf_mh[2];
        if (!RTC_I2C_ReadRegs(MCP7940_REG_MINUTES, buf_mh, 2u)) {
            return false;
        }
        // Riscrivi invariati (serve solo a rispettare la sequenza)
        if (!RTC_I2C_WriteRegs(MCP7940_REG_MINUTES, buf_mh, 2u)) {
            return false;
        }

        // Ora abilita ST mantenendo i secondi
        if (!RTC_I2C_WriteRegs(MCP7940_REG_SECONDS, &out, 1u)) {
            return false;
        }

        // (Opzionale) piccola attesa per stabilizzare l'oscillatore
        HAL_Delay(200);
    }

    return true;
}

/* Lettura orario (HH:MM:SS) in formato binario */
bool RTC_I2C_ReadTime(rtc_time_t *t)
{
    if (t == NULL) return false;

    uint8_t regs[3];
    if (!RTC_I2C_ReadRegs(MCP7940_REG_SECONDS, regs, 3u)) {
        return false;
    }

    /* Maschera bit di controllo */
    uint8_t sec_bcd = regs[0] & 0x7Fu; /* ST su bit7 */
    uint8_t min_bcd = regs[1] & 0x7Fu;
    uint8_t hrs_bcd = regs[2] & 0x3Fu; /* in 24h i bit5..0 codificano l'ora in BCD */

    t->seconds = bcd2bin(sec_bcd);
    t->minutes = bcd2bin(min_bcd);
    t->hours   = bcd2bin(hrs_bcd);

    /* Verifica range */
    if (t->seconds > 59u || t->minutes > 59u || t->hours > 23u) {
        return false;
    }
    return true;
}

/* Scrittura orario (HH:MM:SS). Se startOsc=true, imposta anche ST.
 * Nota: scrive prima MM e HH, poi i SS (eventualmente con ST),
 *       per minimizzare effetti di rollover. */
bool RTC_I2C_WriteTime(const rtc_time_t *t, bool startOsc)
{
    if (t == NULL) return false;
    if (t->seconds > 59u || t->minutes > 59u || t->hours > 23u) return false;

    uint8_t regs[3];
    regs[0] = (uint8_t)(bin2bcd(t->seconds) & 0x7Fu); /* ST inizialmente a 0 */
    regs[1] = (uint8_t)(bin2bcd(t->minutes) & 0x7Fu);
    regs[2] = (uint8_t)(bin2bcd(t->hours)   & 0x3Fu); /* 24h */

    /* 1) Minuti + Ore */
    if (!RTC_I2C_WriteRegs(MCP7940_REG_MINUTES, &regs[1], 2u)) {
        return false;
    }

    /* 2) Secondi (+ eventuale ST) */
    if (startOsc) {
        regs[0] |= 0x80u; /* ST=1 */
    }
    if (!RTC_I2C_WriteRegs(MCP7940_REG_SECONDS, &regs[0], 1u)) {
        return false;
    }

    return true;
}

/* Crea una stringa "hh_mm_ss" zero-padded. out deve avere size >= 9. */
bool RTC_I2C_Format_hh_mm_ss(char out[9])
{
    if (out == NULL) return false;
    rtc_time_t t;
    if (!RTC_I2C_ReadTime(&t)) return false;

    out[0] = (char)('0' + (t.hours / 10u));
    out[1] = (char)('0' + (t.hours % 10u));
    out[2] = '_';
    out[3] = (char)('0' + (t.minutes / 10u));
    out[4] = (char)('0' + (t.minutes % 10u));
    out[5] = '_';
    out[6] = (char)('0' + (t.seconds / 10u));
    out[7] = (char)('0' + (t.seconds % 10u));
    out[8] = '\0';
    return true;
}

void RTC_ShowTime_OnLCD(void)
{
    rtc_time_t t;
    if (!RTC_I2C_ReadTime(&t))
    {
        // In caso di errore, visualizza placeholder
        LCD_DrawText("--:--:--", 2, 10, LCD_FONT_8, false, false);
        return;
    }

    int xpos = 18;
    int ypos = 12;
    lcd_font_t font = LCD_FONT_8;

    // Ore
    LCD_DrawNumber(t.hours, xpos, ypos, 2, false, true, font, false, false);
    xpos += 2 * 8;   // ogni cifra font=8px → 16 px in totale

    // Separatore :
    LCD_DrawText(":", xpos, ypos, font, false, false);
    xpos += 8;

    // Minuti
    LCD_DrawNumber(t.minutes, xpos, ypos, 2, false, true, font, false, false);
    xpos += 2 * 8;

    // Separatore :
    LCD_DrawText(":", xpos, ypos, font, false, false);
    xpos += 8;

    // Secondi
    LCD_DrawNumber(t.seconds, xpos, ypos, 2, false, true, font, false, false);
}
