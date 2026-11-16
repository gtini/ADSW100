#ifndef ADSW100_RDM_H
#define ADSW100_RDM_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
/* Assegnazione utente 15/11/2026: ADSW100/NODIM01, tutti i brand sotto17FA.
   Model ID RDM16 bit, distinto da UID unita', OEM e codice uploader. */
#define ADSW_RDM_MODEL_ID 0x0060u
/* Nuove assegnazioni ADSW100, distinte da RSH7 8001..8003 e DRV_STG4 82xx/E1xx. */
#define PID_DMX_PORT_MODE  0x9100u
#define PID_SOURCE_MODE    0x9101u
#define PID_ARTNET_UNIVERSE 0x9102u
#define PID_SACN_UNIVERSE   0x9103u
#define PID_SACN_TRANSPORT  0x9104u
#define PID_SACN_PRIORITY   0x9105u
#define PID_AUTO_PRIORITY   0x9106u
#define PID_DISPLAY_AUTO_OFF 0x9107u
#define PID_RELAY_OFF_TIME  0x9108u
/* Main: abilita soltanto con EUI fisico valido, nessun hash/fallback condiviso. */
void RDM_Init(void);
void RDM_Task(void);
/* Main, pacchetto completo SC..checksum e timestamp DWT dell'ultimo byte. */
void RDM_Receive(const uint8_t *data, uint16_t length, uint32_t end_cycles);
bool RDM_PacketValid(const uint8_t *data, uint16_t length);
/* Main, query CDC di sola lettura; nessun accesso I2C o modifica configurazione. */
void RDM_FormatDiagnostics(char *out, size_t capacity);
bool RDM_Enabled(void);
bool RDM_SavePending(void);
#endif
