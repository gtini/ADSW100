/*
 * lcd.h
 *
 *  Created on: Oct 16, 2025
 *      Author: gianlucatini
 */

#ifndef INC_LCD_H_
#define INC_LCD_H_

// --- Pannello DOGS102-6 ------------------------------------------------------
#define LCD_WIDTH   102
#define LCD_HEIGHT   64
#define LCD_PAGES   (LCD_HEIGHT/8)

// Font size
typedef enum {
    LCD_FONT_8  = 0,   // 6x8
    LCD_FONT_16 = 1,   // 12x16 (2x)
    LCD_FONT_32 = 2,   // 24x32 (4x)
} lcd_font_t;

// --- Framebuffer (page-major: [page][x]) -------------------------------------
extern uint8_t lcd_fb[LCD_PAGES][LCD_WIDTH];

// variabili globali

extern uint16_t dmx_address, dmx_timeout;
extern const uint8_t font8x8_basic[128][8];

// prototipi pubblici

void EncoderTask(void);

// --- API drawing (scrivono SUBITO la zona modificata) ------------------------
void LCD_Clear(void);
void LCD_FillRect(int x, int y, int w, int h, bool color);
void LCD_DrawText(const char* text, int x, int y, lcd_font_t font_size, bool invert, bool blink);
void LCD_DrawTextInvSpan(const char* text, int x, int y, lcd_font_t font_size, int inv_start_chars, int inv_len_chars, bool blink);

// Disegna un numero in un campo di larghezza fissa, con opzioni di allineamento e padding.
// field_chars: numero di caratteri nel campo (pulizia automatica se le cifre diminuiscono)
// align_left: true = allineamento a sinistra, false = a destra
// zero_pad: true = riempimento con zeri se align_left=false
void LCD_DrawNumber(int16_t num, int x, int y, int field_chars, bool align_left, bool zero_pad, lcd_font_t font_size, bool invert, bool blink);

// --- Blit helpers -------------------------------------------------------------
void LCD_BlitRegion(int x, int y, int w, int h);                 // parziale
void LCD_BlitFB(const uint8_t fb[LCD_PAGES][LCD_WIDTH]);         // completo

// --- Driver LCD (DOGS102-6 stile ST7565R/UC1701) ------------------------------
void LCD_WriteCommand(uint8_t cmd);
void LCD_WriteData(uint8_t data);
void LCD_WriteDataBuffer(const uint8_t* buf, uint16_t len);
void LCD_Reset(void);
void LCD_Init(void);
void LCD_SetPage(uint8_t page);     // 0..7
void LCD_SetColumn(uint8_t x);      // 0..119
void LCD_ClearDisplay(void);        // scrive 0 su tutto il pannello
void LCD_DisplayImage(const uint8_t* image); // 8*120 bytes (page-major)

void LCD_UIInvalidateAll(void);

int16_t UI_GetEncoderDelta(void);    // delta encoder da ultimo polling
uint8_t UI_GetButtonState(void);     // bitmask: bit0=short, bit1=long, bit2=dbl (a tua scelta)

#endif /* INC_LCD_H_ */
