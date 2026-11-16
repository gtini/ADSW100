#ifndef EEPROM_H
#define EEPROM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "stm32h7xx_hal.h"

// Indirizzo 7-bit della 24AA02E48
#define EE_I2C_ADDR_7B  (0x53)

extern I2C_HandleTypeDef hi2c1;
/* Main/ISR: riserva I2C1 durante STOP/READ/rilascio NTAG, anche con HAL READY. */
extern volatile bool ee_nfc_bus_reserved;
#define EE_NFC_COMMITTED 0xA5u
#define EE_NFC_REQUEST 0xB6u
#define EE_NFC_LOCAL 0xC3u
#define EE_NFC_INIT_PREPARE 0xD7u
#define EE_NFC_INIT_TAG 0xE8u
typedef struct
{
    uint32_t revision, request, crc;
    uint8_t state;
} ee_nfc_meta_t;
/* Main: 1 valido, 0 legacy senza metadati, -1 I/O/corruzione riconoscibile. */
HAL_StatusTypeDef EE_ReadConfigSnapshot(uint8_t bytes[40]);
int EE_NfcRead(ee_nfc_meta_t *meta);
HAL_StatusTypeDef EE_NfcStore(const ee_nfc_meta_t *meta);
/* Sospende soltanto l'invalidazione locale durante un commit NFC esplicito. */
void EE_NfcTransaction(bool active);
bool EE_NfcLocalLive(void);
uint32_t EE_NfcLocalAt(void);

uint8_t EE_ReadU8(uint16_t addr);
HAL_StatusTypeDef EE_WriteU8(uint16_t addr, uint8_t v);
uint16_t EE_ReadU16(uint16_t addr);
HAL_StatusTypeDef EE_WriteU16(uint16_t addr, uint16_t v);
uint32_t EE_ReadU32(uint16_t addr);
HAL_StatusTypeDef EE_WriteU32(uint16_t addr, uint32_t v);
HAL_StatusTypeDef EE_Read (uint16_t memAddr, uint8_t* buf, uint16_t len);
HAL_StatusTypeDef EE_Write(uint16_t memAddr, const uint8_t* buf, uint16_t len);

HAL_StatusTypeDef EE_ReadEui64(uint8_t eui64[8]);
HAL_StatusTypeDef EE_GetLocalMAC48(uint8_t mac[6]);

HAL_StatusTypeDef EE_LoadConfig(void);
HAL_StatusTypeDef EE_SaveConfig(void);
/* Main, solo servizio CDC: salva identità/policy per il prossimo avvio.
   Il chiamante impone la finestra di fabbrica; non cambia la RAM attiva. */
HAL_StatusTypeDef EE_SaveIdentity(uint8_t new_brand, uint8_t require_code);
/* Primitiva EEPROM: per Init Memory utente usare EE_InitWithNfc. */
HAL_StatusTypeDef EE_InitMemory(void);
/* Azione esplicita Init Memory: default verificati e consenso NFC persistente. */
HAL_StatusTypeDef EE_InitWithNfc(bool preserve_identity);
HAL_StatusTypeDef EE_NfcSeal(const ee_nfc_meta_t *expected, uint8_t state);
/* Lettura metadati commessi senza migrazione, pubblicazione RAM o scritture. */
HAL_StatusTypeDef EE_ReadIdentity(uint8_t *stored_brand, uint8_t *require_code);
/* Reset parametri utente preservando versione, brand e policy di fabbrica. */
HAL_StatusTypeDef EE_ResetUserMemory(void);

/* CDC: ultimo esito salvato in RAM, senza accessi I2C. rc usa HAL_OK/ERROR/BUSY/TIMEOUT. */
void EE_FormatDiagnostics(char *out, size_t capacity);
void EE_BusScan(void);
void EE_DumpTail(void);

#endif // EEPROM_H
