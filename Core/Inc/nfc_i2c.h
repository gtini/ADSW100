#ifndef NFC_I2C_H
#define NFC_I2C_H
#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
/* Main: inizializza soltanto il servizio; IRQ attivati dopo init HAL I2C1. Nessuna formattazione automatica. */
void NFC_Init(void);
/* Main cooperativo: uno scambio NTAG per passo; buffer statici posseduti dal servizio. */
void NFC_Service(void);
/* Solo main: abilitazione diagnostica volatile, scarica prima le operazioni pendenti. */
void NFC_SetEnabled(bool enabled);
void NFC_RecordBoot(uint32_t stage, uint32_t reset_flags);
void NFC_FormatLiveDiagnostics(char *out, size_t capacity);
void NFC_FormatDiagnostics(char *out, size_t capacity);
void NFC_FormatBusDiagnostics(char *out, size_t capacity);
/* Bridge IRQ I2C1, senza I/O applicativi o stampe. */
void NFC_I2C_EventIRQ(void);
void NFC_I2C_ErrorIRQ(void);
#endif
