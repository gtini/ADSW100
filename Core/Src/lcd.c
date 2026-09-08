/*
 * lcd.c
 *
 *  Created on: Aug 17, 2025
 *      Author: fiorenzodario
 */

#include "h_files.h"
#include "defs.h"
#include <string.h>

// HAL/CubeMX
#include "stm32h7xx_hal.h"
#include "main.h"
#include "gpio.h"
#include "spi.h"

extern SPI_HandleTypeDef hspi1;

uint16_t dmx_timeout, temp_dmx;
uint8_t lcd_backlight;


static int16_t encoder_val = 0;

// --------------------------- Framebuffer & Blink ------------------------------
uint8_t lcd_fb[LCD_PAGES][LCD_WIDTH];

// Cache per riga (page) per evitare ridisegni inutili
typedef struct
{
    uint16_t last_w_px;
    uint8_t  last_hash;
    int      last_x;
    int      last_y;
    int      last_char_h;
    bool     valid;
} ui_row_cache_t;

static ui_row_cache_t s_row_cache[LCD_PAGES];

static volatile bool s_blink_phase = true;

// Invalida tutta la cache delle righe
static void LCD_InvalidateRowCacheAll(void)
{
    for (int p = 0; p < LCD_PAGES; ++p)
    {
        s_row_cache[p].valid       = false;
        s_row_cache[p].last_w_px   = 0;
        s_row_cache[p].last_hash   = 0;
        s_row_cache[p].last_x      = 0;
        s_row_cache[p].last_y      = 0;
        s_row_cache[p].last_char_h = 0;
    }
}

// wrapper "pubblico" da richiamare al cambio pagina UI

void LCD_UIInvalidateAll(void)
{
    LCD_InvalidateRowCacheAll();
}

static inline bool in_bounds(int x, int y)
{
    return (x >= 0 && x < LCD_WIDTH && y >= 0 && y < LCD_HEIGHT);
}

static uint8_t ui_fast_hash(const char* s, bool invert, int scale)
{
    uint8_t h = 0x5A ^ (invert ? 0xA5 : 0x00) ^ (uint8_t)scale;
    if (s)
    {
        for (const char* p = s; *p; ++p)
            h = (uint8_t)((h << 5) - h) ^ (uint8_t)(*p);
    }
    return h;
}

static void put_pixel(int x, int y, bool color)
{
    if (!in_bounds(x,y)) return;
    int page = y >> 3;
    uint8_t bit = (1u << (y & 7));
    if (color) lcd_fb[page][x] |= bit;
    else       lcd_fb[page][x] &= (uint8_t)~bit;
}

// ------------------------------ Font 6x8 --------------------------------------
// ASCII 0x20..0x7F, 8 byte per char (row-major), bit0 = pixel a sinistra
// Ogni riga usa solo i 6 bit meno significativi (2 bit a 0 a destra)
const uint8_t font6x8[96][8] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 20 ' '
  {0x04,0x04,0x04,0x04,0x04,0x00,0x04,0x00}, // 21 '!'
  {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, // 22 '"'
  {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A,0x00}, // 23 '#'
  {0x04,0x1E,0x05,0x0E,0x14,0x0F,0x04,0x00}, // 24 '$'
  {0x03,0x13,0x08,0x04,0x02,0x19,0x18,0x00}, // 25 '%'
  {0x06,0x09,0x05,0x02,0x15,0x09,0x16,0x00}, // 26 '&'
  {0x06,0x04,0x02,0x00,0x00,0x00,0x00,0x00}, // 27 '\''
  {0x08,0x04,0x02,0x02,0x02,0x04,0x08,0x00}, // 28 '('
  {0x02,0x04,0x08,0x08,0x08,0x04,0x02,0x00}, // 29 ')'
  {0x00,0x04,0x15,0x0E,0x15,0x04,0x00,0x00}, // 2A '*'
  {0x00,0x04,0x04,0x1F,0x04,0x04,0x00,0x00}, // 2B '+'
  {0x00,0x00,0x00,0x00,0x06,0x04,0x02,0x00}, // 2C ','
  {0x00,0x00,0x00,0x1F,0x00,0x00,0x00,0x00}, // 2D '-'
  {0x00,0x00,0x00,0x00,0x00,0x06,0x06,0x00}, // 2E '.'
  {0x00,0x10,0x08,0x04,0x02,0x01,0x00,0x00}, // 2F '/'
  {0x0E,0x11,0x19,0x15,0x13,0x11,0x0E,0x00}, // 30 '0'
  {0x04,0x06,0x04,0x04,0x04,0x04,0x0E,0x00}, // 31 '1'
  {0x0E,0x11,0x10,0x08,0x04,0x02,0x1F,0x00}, // 32 '2'
  {0x1F,0x08,0x04,0x08,0x10,0x11,0x0E,0x00}, // 33 '3'
  {0x08,0x0C,0x0A,0x09,0x1F,0x08,0x08,0x00}, // 34 '4'
  {0x1F,0x01,0x0F,0x10,0x10,0x11,0x0E,0x00}, // 35 '5'
  {0x0C,0x02,0x01,0x0F,0x11,0x11,0x0E,0x00}, // 36 '6'
  {0x1F,0x10,0x08,0x04,0x02,0x02,0x02,0x00}, // 37 '7'
  {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,0x00}, // 38 '8'
  {0x0E,0x11,0x11,0x1E,0x10,0x08,0x06,0x00}, // 39 '9'
  {0x00,0x06,0x06,0x00,0x06,0x06,0x00,0x00}, // 3A ':'
  {0x00,0x06,0x06,0x00,0x06,0x04,0x02,0x00}, // 3B ';'
  {0x08,0x04,0x02,0x01,0x02,0x04,0x08,0x00}, // 3C '<'
  {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00,0x00}, // 3D '='
  {0x02,0x04,0x08,0x10,0x08,0x04,0x02,0x00}, // 3E '>'
  {0x0E,0x11,0x10,0x08,0x04,0x00,0x04,0x00}, // 3F '?'
  {0x0E,0x11,0x15,0x1D,0x0D,0x01,0x1E,0x00}, // 40 '@'
  {0x04,0x0A,0x11,0x11,0x1F,0x11,0x11,0x00}, // 41 'A'
  {0x0F,0x11,0x11,0x0F,0x11,0x11,0x0F,0x00}, // 42 'B'
  {0x0E,0x11,0x01,0x01,0x01,0x11,0x0E,0x00}, // 43 'C'
  {0x07,0x09,0x11,0x11,0x11,0x09,0x07,0x00}, // 44 'D'
  {0x1F,0x01,0x01,0x0F,0x01,0x01,0x1F,0x00}, // 45 'E'
  {0x1F,0x01,0x01,0x0F,0x01,0x01,0x01,0x00}, // 46 'F'
  {0x0E,0x11,0x01,0x1D,0x11,0x11,0x1E,0x00}, // 47 'G'
  {0x11,0x11,0x11,0x1F,0x11,0x11,0x11,0x00}, // 48 'H'
  {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E,0x00}, // 49 'I'
  {0x1C,0x08,0x08,0x08,0x08,0x09,0x06,0x00}, // 4A 'J'
  {0x01,0x01,0x09,0x05,0x03,0x05,0x09,0x00}, // 4B 'K'
  {0x01,0x01,0x01,0x01,0x01,0x01,0x1F,0x00}, // 4C 'L'
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11,0x00}, // 4D 'M'
  {0x11,0x11,0x13,0x15,0x19,0x11,0x11,0x00}, // 4E 'N'
  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,0x00}, // 4F 'O'
  {0x0F,0x11,0x11,0x0F,0x01,0x01,0x01,0x00}, // 50 'P'
  {0x0E,0x11,0x11,0x11,0x15,0x09,0x16,0x00}, // 51 'Q'
  {0x0F,0x11,0x11,0x0F,0x05,0x09,0x11,0x00}, // 52 'R'
  {0x1E,0x01,0x01,0x0E,0x10,0x10,0x0F,0x00}, // 53 'S'
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04,0x00}, // 54 'T'
  {0x11,0x11,0x11,0x11,0x11,0x11,0x0E,0x00}, // 55 'U'
  {0x11,0x11,0x11,0x11,0x11,0x0A,0x04,0x00}, // 56 'V'
  {0x11,0x11,0x11,0x15,0x15,0x15,0x0A,0x00}, // 57 'W'
  {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,0x00}, // 58 'X'
  {0x11,0x11,0x11,0x0A,0x04,0x04,0x04,0x00}, // 59 'Y'
  {0x1F,0x10,0x08,0x04,0x02,0x01,0x1F,0x00}, // 5A 'Z'
  {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E,0x00}, // 5B '['
  {0x00,0x01,0x02,0x04,0x08,0x10,0x00,0x00}, // 5C '\'
  {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E,0x00}, // 5D ']'
  {0x04,0x0A,0x11,0x00,0x00,0x00,0x00,0x00}, // 5E '^'
  {0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x00}, // 5F '_'
  {0x02,0x04,0x00,0x00,0x00,0x00,0x00,0x00}, // 60 '`'
  {0x00,0x00,0x0E,0x10,0x1E,0x11,0x1E,0x00}, // 61 'a'
  {0x01,0x01,0x0D,0x13,0x11,0x11,0x0F,0x00}, // 62 'b'
  {0x00,0x00,0x0E,0x01,0x01,0x11,0x0E,0x00}, // 63 'c'
  {0x10,0x10,0x16,0x19,0x11,0x11,0x1E,0x00}, // 64 'd'
  {0x00,0x00,0x0E,0x11,0x1F,0x01,0x0E,0x00}, // 65 'e'
  {0x0C,0x12,0x02,0x07,0x02,0x02,0x02,0x00}, // 66 'f'
  {0x00,0x00,0x1E,0x11,0x1E,0x10,0x0E,0x00}, // 67 'g'
  {0x01,0x01,0x0D,0x13,0x11,0x11,0x11,0x00}, // 68 'h'
  {0x04,0x00,0x06,0x04,0x04,0x04,0x0E,0x00}, // 69 'i'
  {0x08,0x00,0x0C,0x08,0x08,0x09,0x06,0x00}, // 6A 'j'
  {0x01,0x01,0x09,0x05,0x03,0x05,0x09,0x00}, // 6B 'k'
  {0x06,0x04,0x04,0x04,0x04,0x04,0x0E,0x00}, // 6C 'l'
  {0x00,0x00,0x0B,0x15,0x15,0x11,0x11,0x00}, // 6D 'm'
  {0x00,0x00,0x0D,0x13,0x11,0x11,0x11,0x00}, // 6E 'n'
  {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E,0x00}, // 6F 'o'
  {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01,0x00}, // 70 'p'
  {0x00,0x00,0x16,0x19,0x1E,0x10,0x10,0x00}, // 71 'q'
  {0x00,0x00,0x0D,0x13,0x01,0x01,0x01,0x00}, // 72 'r'
  {0x00,0x00,0x1E,0x01,0x0E,0x10,0x0F,0x00}, // 73 's'
  {0x02,0x02,0x07,0x02,0x02,0x12,0x0C,0x00}, // 74 't'
  {0x00,0x00,0x11,0x11,0x11,0x19,0x16,0x00}, // 75 'u'
  {0x00,0x00,0x11,0x11,0x11,0x0A,0x04,0x00}, // 76 'v'
  {0x00,0x00,0x11,0x11,0x15,0x15,0x0A,0x00}, // 77 'w'
  {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11,0x00}, // 78 'x'
  {0x00,0x00,0x11,0x11,0x1E,0x10,0x0E,0x00}, // 79 'y'
  {0x00,0x00,0x1F,0x08,0x04,0x02,0x1F,0x00}, // 7A 'z'
  {0x08,0x04,0x04,0x02,0x04,0x04,0x08,0x00}, // 7B '{'
  {0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00}, // 7C '|'
  {0x02,0x04,0x04,0x08,0x04,0x04,0x02,0x00}, // 7D '}'
  {0x02,0x15,0x08,0x00,0x00,0x00,0x00,0x00}, // 7E '~'
  {0x0C,0x12,0x12,0x12,0x12,0x12,0x0C,0x00}, // 7F '?'
};

// -------------------------- Blit helpers --------------------------------------
void LCD_WriteCommand(uint8_t cmd)
{
    HAL_GPIO_WritePin(LCD_CD_GPIO_Port,  LCD_CD_Pin,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_CS0_GPIO_Port, LCD_CS0_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &cmd, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(LCD_CS0_GPIO_Port, LCD_CS0_Pin, GPIO_PIN_SET);
}

void LCD_WriteData(uint8_t data)
{
    HAL_GPIO_WritePin(LCD_CD_GPIO_Port,  LCD_CD_Pin,  GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_CS0_GPIO_Port, LCD_CS0_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &data, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(LCD_CS0_GPIO_Port, LCD_CS0_Pin, GPIO_PIN_SET);
}

void LCD_WriteDataBuffer(const uint8_t* buf, uint16_t len)
{
    if (!buf || !len) return;
    HAL_GPIO_WritePin(LCD_CD_GPIO_Port,  LCD_CD_Pin,  GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_CS0_GPIO_Port, LCD_CS0_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, (uint8_t*)buf, len, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(LCD_CS0_GPIO_Port, LCD_CS0_Pin, GPIO_PIN_SET);
}

void LCD_SetPage(uint8_t page)   { LCD_WriteCommand(0xB0 | (page & 0x07)); }
void LCD_SetColumn(uint8_t x)    { LCD_WriteCommand(0x10 | ((x>>4)&0x0F)); LCD_WriteCommand(0x00 | (x&0x0F)); }

void LCD_Reset(void)
{
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(10);
}

void LCD_Init(void)
{
    LCD_Reset();

    LCD_WriteCommand(0x40);  HAL_Delay(10);
    LCD_WriteCommand(0xA1);  HAL_Delay(10);
    LCD_WriteCommand(0xC0);  HAL_Delay(10);
    LCD_WriteCommand(0xA4);  HAL_Delay(10);
    LCD_WriteCommand(0xA6);  HAL_Delay(10);
    LCD_WriteCommand(0xA2);  HAL_Delay(10);
    LCD_WriteCommand(0x2F);  HAL_Delay(10);
    LCD_WriteCommand(0x27);  HAL_Delay(10);
    LCD_WriteCommand(0x81);  HAL_Delay(10);
    LCD_WriteCommand(0x05);  HAL_Delay(10);
    LCD_WriteCommand(0xFA);  HAL_Delay(10);
    LCD_WriteCommand(0x90);  HAL_Delay(10);
    LCD_WriteCommand(0xAF);  HAL_Delay(10);

    LCD_ClearDisplay();

    LCD_InvalidateRowCacheAll();
}

void LCD_ClearDisplay(void)
{
    // Pulisci HW
    for (uint8_t p = 0; p < LCD_PAGES; ++p)
    {
        LCD_SetPage(p);
        LCD_SetColumn(0);
        uint8_t line[LCD_WIDTH] = {0};
        LCD_WriteDataBuffer(line, LCD_WIDTH);
    }

    // Pulisci anche il framebuffer (coerenza con HW)
    memset(lcd_fb, 0, sizeof(lcd_fb));

    // Invalida la cache (tutte le righe)
    LCD_InvalidateRowCacheAll();
}

// image: 8*120 bytes, layout page-major [page][x]
void LCD_DisplayImage(const uint8_t* image)
{
    if (!image) return;

    for (uint8_t p = 0; p < LCD_PAGES; ++p)
    {
        // Copia nel framebuffer
        memcpy(lcd_fb[p], image + (p * LCD_WIDTH), LCD_WIDTH);

        // Scrivi all’LCD
        LCD_SetPage(p);
        LCD_SetColumn(0);
        LCD_WriteDataBuffer(&lcd_fb[p][0], LCD_WIDTH);
    }

    // Cambia completamente il contenuto visivo -> invalida cache
    LCD_InvalidateRowCacheAll();
}

void LCD_BlitFB(const uint8_t fb[LCD_PAGES][LCD_WIDTH])
{
    for (uint8_t p = 0; p < LCD_PAGES; ++p) {
        LCD_SetPage(p);
        LCD_SetColumn(0);
        LCD_WriteDataBuffer(fb[p], LCD_WIDTH);
    }
}

// Blit parziale: invia tutte le colonne delle pagine toccate
void LCD_BlitRegion(int x, int y, int w, int h)
{
    if (w<=0 || h<=0) return;
    int x2 = x + w - 1, y2 = y + h - 1;
    if (x2 < 0 || y2 < 0 || x >= LCD_WIDTH || y >= LCD_HEIGHT) return;

    int xs = (x < 0) ? 0 : x;
    int ys = (y < 0) ? 0 : y;
    int xe = (x2 >= LCD_WIDTH) ? (LCD_WIDTH-1) : x2;
    int ye = (y2 >= LCD_HEIGHT) ? (LCD_HEIGHT-1) : y2;

    uint8_t p_start = (uint8_t)(ys >> 3);
    uint8_t p_end   = (uint8_t)(ye >> 3);

    for (uint8_t p = p_start; p <= p_end; ++p) {
        LCD_SetPage(p);
        LCD_SetColumn((uint8_t)xs);
        LCD_WriteDataBuffer(&lcd_fb[p][xs], (uint16_t)(xe - xs + 1));
    }
}

// ------------------------------ Drawing API -----------------------------------
void LCD_Clear(void)
{
    memset(lcd_fb, 0, sizeof(lcd_fb));
    LCD_ClearDisplay(); // aggiorna subito tutto
}

void LCD_FillRect(int x, int y, int w, int h, bool color)
{
    if (w <= 0 || h <= 0) return;
    int x2 = x + w - 1;
    int y2 = y + h - 1;
    if (x2 < 0 || y2 < 0 || x >= LCD_WIDTH || y >= LCD_HEIGHT) return;

    int xs = (x < 0) ? 0 : x;
    int ys = (y < 0) ? 0 : y;
    int xe = (x2 >= LCD_WIDTH) ? (LCD_WIDTH-1) : x2;
    int ye = (y2 >= LCD_HEIGHT) ? (LCD_HEIGHT-1) : y2;

    for (int yy = ys; yy <= ye; ++yy)
        for (int xx = xs; xx <= xe; ++xx)
            put_pixel(xx, yy, color);

    LCD_BlitRegion(xs, ys, xe - xs + 1, ye - ys + 1);
}

static inline int scale_from_font(lcd_font_t f) { return (f==LCD_FONT_8)?1: (f==LCD_FONT_16)?2:4; }

static void draw_char_scaled(char c, int x, int y, int scale, bool invert)
{
    if (scale != 1 && scale != 2 && scale != 4)
        return;

    // Se il carattere è fuori range ASCII, sostituisci con spazio
    if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7F)
        c = 0x20;

    // Recupera il glifo dal font 6x8: 8 righe, 6 bit per riga
    const uint8_t *glyph = &font6x8[(uint8_t)c - 0x20][0];

    for (int gy = 0; gy < 8; ++gy)
    {
        uint8_t rowbits = glyph[gy] & 0x3F;  // solo i 6 bit utili

        for (int sy = 0; sy < scale; ++sy)
        {
            int py = y + gy * scale + sy;
            if (py < 0 || py >= LCD_HEIGHT)
                continue;

            for (int gx = 0; gx < 6; ++gx)
            {
                bool on = (rowbits >> gx) & 0x01;
                if (invert)
                    on = !on;

                for (int sx = 0; sx < scale; ++sx)
                {
                    int px = x + gx * scale + sx;
                    if (px < 0 || px >= LCD_WIDTH)
                        continue;

                    put_pixel(px, py, on);
                }
            }
        }
    }
}

static int i16_to_str(int16_t v, char* out, int outsz, int width, bool align_left, bool zero_pad)
{
    char tmp[8];
    int n = 0;
    bool neg = (v < 0);
    uint16_t val = neg ? -v : v;

    // Estrai cifre in ordine inverso
    do {
        tmp[n++] = '0' + (val % 10);
        val /= 10;
    } while (val && n < (int)sizeof(tmp));

    if (neg) tmp[n++] = '-';

    // Se serve padding
    int pad = (width > n) ? (width - n) : 0;
    int pos = 0;

    if (!align_left) {
        // padding a sinistra
        char padch = zero_pad ? '0' : ' ';
        while (pad-- > 0 && pos < outsz - 1) {
            out[pos++] = padch;
        }
    }

    // copia le cifre in ordine corretto
    while (n > 0 && pos < outsz - 1) {
        out[pos++] = tmp[--n];
    }

    if (align_left) {
        // padding a destra con spazi
        while (pad-- > 0 && pos < outsz - 1) {
            out[pos++] = ' ';
        }
    }

    out[pos] = '\0';
    return pos;
}

// Funzione interna per pulire un rettangolo nel framebuffer
static void fb_clear_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    int x2 = x + w - 1, y2 = y + h - 1;
    if (x2 < 0 || y2 < 0 || x >= LCD_WIDTH || y >= LCD_HEIGHT) return;

    int xs = (x < 0) ? 0 : x;
    int ys = (y < 0) ? 0 : y;
    int xe = (x2 >= LCD_WIDTH) ? (LCD_WIDTH-1) : x2;
    int ye = (y2 >= LCD_HEIGHT) ? (LCD_HEIGHT-1) : y2;

    for (int yy = ys; yy <= ye; ++yy)
        for (int xx = xs; xx <= xe; ++xx)
            put_pixel(xx, yy, false);
}

void LCD_DrawNumber(int16_t num,
                    int x, int y,
                    int field_chars,
                    bool align_left,
                    bool zero_pad,
                    lcd_font_t font_size,
                    bool invert,
                    bool blink)
{
    // Calcola dimensioni in pixel dell'area da pulire
    int scale = (font_size == LCD_FONT_8) ? 1 :
                (font_size == LCD_FONT_16) ? 2 : 4;
    int wpx = field_chars * 6 * scale;
    int hpx = 8 * scale;

    // Pulisci area precedente
    fb_clear_rect(x, y, wpx, hpx);

    // Converte il numero in stringa formattata
    char buf[12];
    i16_to_str(num, buf, sizeof(buf), field_chars, align_left, zero_pad);

    // Disegna il testo
    LCD_DrawText(buf, x, y, font_size, invert, blink);
}

void LCD_DrawTextInvSpan(const char* text,
                         int x, int y,
                         lcd_font_t font_size,
                         int inv_start_chars,
                         int inv_len_chars,
                         bool blink)
{
    if (!text) return;

    const int scale  = scale_from_font(font_size);
    const int cw     = 6 * scale;   // font 6x8
    const int ch     = 8 * scale;

    // Lunghezza (prima della eventuale \n)
    int n = 0;
    for (const char* p = text; *p && *p!='\n'; ++p) ++n;
    if (n <= 0) return;

    // BBox
    const int minx = x;
    const int maxx = x + n*cw - 1;

    // Blink OFF → pulisci e blitta, poi esci
    if (blink && !s_blink_phase) {
        fb_clear_rect((x<0?0:x), y, (maxx-minx+1), ch);
        LCD_BlitRegion((x<0?0:x), y, (maxx-minx+1), ch);
        return;
    }

    // Cache per riga (per page = y>>3)
    const int page = (y >> 3);
    if (page < 0 || page >= LCD_PAGES) return;

    // Hash: testo + posizione/lunghezza span invertita
    uint8_t h = ui_fast_hash(text, /*invert=*/false, scale);
    h ^= (uint8_t)(inv_start_chars*31 + inv_len_chars*17);

    ui_row_cache_t* rc = &s_row_cache[page];

    if (!blink && rc->valid &&
        rc->last_x == x &&
        rc->last_y == y &&
        rc->last_char_h == ch &&
        rc->last_hash == h)
    {
        // Identico → skip totale
        return;
    }

    // Pulisci bbox
    fb_clear_rect((x<0?0:x), y, (maxx-minx+1), ch);

    // Disegno carattere-per-carattere, invert solo dentro lo span
    int cx = x;
    for (int i = 0; i < n; ++i) {
        const bool inv = (i >= inv_start_chars) && (i < inv_start_chars + inv_len_chars);
        draw_char_scaled(text[i], cx, y, scale, inv);
        cx += cw;
        if (cx >= LCD_WIDTH) break;
    }

    // Blit area toccata
    LCD_BlitRegion((x<0?0:x), y, (maxx-minx+1), ch);

    // Aggiorna cache
    rc->last_x       = x;
    rc->last_y       = y;
    rc->last_w_px    = n * cw;
    rc->last_hash    = h;
    rc->last_char_h  = ch;
    rc->valid        = true;
}

void LCD_DrawText(const char* text, int x, int y, lcd_font_t font_size, bool invert, bool blink)
{
    if (!text) return;

    const int scale   = scale_from_font(font_size);
    const int char_w  = 6 * scale;   // 6x8 base
    const int char_h  = 8 * scale;

    // Calcolo bbox + larghezza testo in pixel (prima riga; se c'è '\n' lo gestiamo comunque)
    int cx = x, cy = y;
    int minx =  32767, miny =  32767;
    int maxx = -32768, maxy = -32768;
    int text_px = 0;

    for (const char* p = text; *p; ++p)
    {
        if (*p == '\n')
        {
            // fermiamoci alla prima riga per coerenza col “clear/blit riga”
            break;
        }
        int x1 = cx, y1 = cy;
        int x2 = cx + char_w - 1;
        int y2 = cy + char_h - 1;

        if (x1 < minx) minx = x1;
        if (y1 < miny) miny = y1;
        if (x2 > maxx) maxx = x2;
        if (y2 > maxy) maxy = y2;

        cx += char_w;
        text_px += char_w;
        if (cx >= LCD_WIDTH) break;
    }
    if (minx > maxx || miny > maxy) return;

    // Identifica la/e pagina/e interessate (prima riga)
    const int page = (y >> 3);
    if (page < 0 || page >= LCD_PAGES) return;

    // Hash contenuto per skip draw se identico (niente blink)
    const uint8_t h = ui_fast_hash(text, invert, scale);
    ui_row_cache_t* rc = &s_row_cache[page];

    // Se stesso x, stessa altezza char, stesso hash, niente blink -> non ridisegnare
    if (!blink && rc->valid &&
        rc->last_x == x &&
        rc->last_y == y &&           // <<< aggiungi
        rc->last_char_h == char_h &&
        rc->last_hash == h)
    {
        return; // evita SPI traffic/flicker
    }

    // Larghezza da pulire: max(ultima larghezza, larghezza attuale)
    int clear_w = text_px;
    if (rc->valid &&
        rc->last_x == x &&
        rc->last_y == y &&           // <<< aggiungi
        rc->last_char_h == char_h &&
        rc->last_w_px > clear_w)
    {
        clear_w = rc->last_w_px;
    }
    if (clear_w < 0) clear_w = 0;
    if (x + clear_w > LCD_WIDTH) clear_w = LCD_WIDTH - x;

    // Pulisci solo l’area necessaria (non tutta la riga!)
    if (clear_w > 0)
        fb_clear_rect(x < 0 ? 0 : x, y, clear_w, char_h);

    // Blink OFF? (lampeggio) -> non disegnare il testo
    if (blink && !s_blink_phase)
    {
        // Blit dell’area pulita
        LCD_BlitRegion(x < 0 ? 0 : x, y, clear_w, char_h);
        // Non aggiornare la cache (così al prossimo frame ridisegna)
        return;
    }

    // Disegno effettivo (prima riga)
    cx = x;
    for (const char* p = text; *p; ++p)
    {
        if (*p == '\n') break;
        draw_char_scaled(*p, cx, y, scale, invert);
        cx += char_w;
        if (cx >= LCD_WIDTH) break;
    }

    // Blit solo dell’area toccata
    LCD_BlitRegion(x < 0 ? 0 : x, y, clear_w, char_h);

    // Aggiorna cache
    rc->last_x       = x;
    rc->last_y       = y;
    rc->last_w_px    = text_px;
    rc->last_hash    = h;
    rc->last_char_h  = char_h;
    rc->valid        = true;
}

// leggi encoder (1mS)

void EncoderTask(void)
{
	int i;
	static bool zz_fl = false;
	uint8_t encoder_now;
	static uint8_t encoder_old = 0xFF;
	static uint16_t blink_tick_500ms = 0;
	GPIO_PinState a = HAL_GPIO_ReadPin(ENC_A_GPIO_Port, ENC_A_Pin);
	GPIO_PinState b = HAL_GPIO_ReadPin(ENC_B_GPIO_Port, ENC_B_Pin);

	encoder_now = ((uint8_t)a << 1) | (uint8_t)b;	// 11 = Detent

	if (encoder_now == 0b11)						// Detent
	{
		if (encoder_old != 0xFF)
		{
			if (zz_fl)								// C'è stata una condizione 00?
			{
				if (encoder_old == 0b01)
				{
						encoder_val++;
				}
				else if (encoder_old == 0b10)
				{
						encoder_val--;
				}

				zz_fl = false;
			}

			encoder_old = 0xFF;
		}
	}
	else
	{
		if (encoder_old == 0xFF)			// Encoder in movimento
		{
			if (encoder_now == 0b01 || encoder_now == 0b10)
			{
				encoder_old = encoder_now;
			}
		}

		if (encoder_now == 0b00)
		{
			zz_fl = true;					// conferma passaggio per 00
		}
	}

	if (++blink_tick_500ms >= 500)
	{
		blink_tick_500ms = 0;

		s_blink_phase = !s_blink_phase;

		HAL_GPIO_TogglePin(STAT_LED_GPIO_Port, STAT_LED_Pin);
	}

	if (dmx_timeout > 0)
	{
		if (--dmx_timeout == 0)
		{
			HAL_GPIO_WritePin(DMX_LED_GPIO_Port, DMX_LED_Pin, GPIO_PIN_RESET);

			for (i = 0; i < 512; i++)
			{
				dmxRxBuffer[i] = 0;
			}
		}
	}
}

// delta encoder da ultimo polling

int16_t UI_GetEncoderDelta(void)
{
    int16_t delta;
    delta = encoder_val;
    encoder_val = 0;
    return delta;
}

// bitmask: bit0=short, bit1=long, bit2=dbl (a tua scelta)
uint8_t UI_GetButtonState(void)
{
    static uint8_t prev_state = 1;        // 1 = rilasciato (pull-up)
    static uint32_t press_time = 0;       // tempo di inizio pressione
    static bool long_sent = false;        // flag per evitare doppio evento

    uint8_t event = 0;
    uint8_t curr_state = HAL_GPIO_ReadPin(ENC_P_GPIO_Port, ENC_P_Pin); // 0 = premuto
    uint32_t now = HAL_GetTick();

    // Rileva inizio pressione
    if (curr_state == 0 && prev_state == 1)
    {
        press_time = now;
        long_sent = false;
    }

    // Rileva rilascio
    if (curr_state == 1 && prev_state == 0)
    {
        uint32_t dur = now - press_time;

        // click breve: durata tra 50 e 700 ms e non già segnalato come long
        if (!long_sent && dur > 50 && dur < 700)
        {
            event = 0x01; // short press
        }

        press_time = 0;
    }

    // Rileva pressione lunga
    if (curr_state == 0 && !long_sent && press_time && (now - press_time) >= 700)
    {
        event = 0x02; // long press
        long_sent = true;
    }

    prev_state = curr_state;
    return event;
}
