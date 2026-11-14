#ifndef EEPROM_H
#define EEPROM_H

#include <stdint.h>
#include "stm32h7xx_hal.h"

// Indirizzo 7-bit della 24AA02E48
#define EE_I2C_ADDR_7B  (0x53)

extern I2C_HandleTypeDef hi2c1;

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
HAL_StatusTypeDef EE_InitMemory(void);
/* Lettura metadati commessi senza migrazione, pubblicazione RAM o scritture. */
HAL_StatusTypeDef EE_ReadIdentity(uint8_t *stored_brand, uint8_t *require_code);
/* Reset parametri utente preservando versione, brand e policy di fabbrica. */
HAL_StatusTypeDef EE_ResetUserMemory(void);

void EE_BusScan(void);
void EE_DumpTail(void);

#endif // EEPROM_H
