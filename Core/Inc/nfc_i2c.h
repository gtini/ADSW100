#ifndef NFC_I2C_H
#define NFC_I2C_H

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NFC_I2C_TIMEOUT_MS
#define NFC_I2C_TIMEOUT_MS      (20u)     /* timeout robusto per NTAG I2C Plus */
#endif

#ifndef NFC_I2C_ADDR_DEFAULT_A
#define NFC_I2C_ADDR_DEFAULT_A  (0x55u)   /* NT3H2211 (default) */
#endif

#ifndef NFC_I2C_ADDR_DEFAULT_B
#define NFC_I2C_ADDR_DEFAULT_B  (0x57u)   /* NT3H2111 (altra variante) */
#endif

#ifndef NFC_I2C_ADDR_PREFERRED
#define NFC_I2C_ADDR_PREFERRED  (0x59u)   /* opzionale: se già configurato così lo usiamo */
#endif

#define NFC_I2C_PAGE_SIZE       (16u)

/* === API principali (nomi inalterati) === */
bool    NFC_I2C_InitQuickReaddress(I2C_HandleTypeDef *hi2c);

void    NFC_I2C_SetRuntimeAddress(uint8_t addr);
uint8_t NFC_I2C_GetRuntimeAddress(void);

bool    NFC_I2C_ReadBlock4(I2C_HandleTypeDef *hi2c, uint8_t block, uint8_t out4[4]);
bool    NFC_I2C_ReadPage16(I2C_HandleTypeDef *hi2c, uint8_t start_block, uint8_t out16[NFC_I2C_PAGE_SIZE]);

bool    NFC_I2C_ReadUID(I2C_HandleTypeDef *hi2c, uint8_t uid7[7]);
bool    NFC_I2C_DumpFirstBlocks(I2C_HandleTypeDef *hi2c);

bool NFC_UserArea_Erase(I2C_HandleTypeDef *hi2c, uint8_t start_page, uint16_t pages);
bool NFC_FormatIfVirgin(I2C_HandleTypeDef *hi2c);
bool NFC_InitMemory(I2C_HandleTypeDef *hi2c);

// --- Boot & Sync ---
void NFC_BootApplyIfValid(void);
void NFC_Service(void);

bool ndef_get_cfg_json(I2C_HandleTypeDef *hi2c, char *out_json, uint16_t out_sz);
bool ndef_write_mime_json(I2C_HandleTypeDef *hi2c, const char *json);

void    NFC_Test(void);

#ifdef __cplusplus
}
#endif
#endif /* NFC_I2C_H */
