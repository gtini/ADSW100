/*
 * cfg_io.h
 *  Created on: Nov 1, 2025
 *      Author: gianlucatini
 */

#ifndef INC_CFG_IO_H_
#define INC_CFG_IO_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ===========================================================
 *  Magic string per identificare la configurazione del device
 * =========================================================== */
void build_magic_from_eui(char *magic, size_t msz, const uint8_t eui[6]);

/* ===========================================================
 *  Conversione JSON ↔ variabili di configurazione generiche
 * =========================================================== */

/* Serializza i valori in un JSON deterministico (senza CRC) */
bool cfg_to_json_vars(uint8_t dmx_mode, uint16_t universe,
                      uint8_t net, uint8_t subuni, uint16_t prio,
                      bool dhcp, const char *ip, const char *mask, const char *gw,
                      const char *magic,
                      char *json, size_t jsz);

/* Esegue il parsing del JSON nel formato sopra */
bool json_to_cfg_vars(const char *json,
                      char *magic_out, size_t magic_sz,
                      uint8_t *dmx_mode, uint16_t *universe,
                      uint8_t *net, uint8_t *subuni, uint16_t *prio,
                      bool *dhcp, char ip[16], char mask[16], char gw[16]);

/* ===========================================================
 *  Interfaccia con le variabili globali del firmware
 * =========================================================== */

/* Legge le variabili globali attuali e le esporta in formato compatibile JSON */
void get_runtime_vars(uint8_t *dmx_mode, uint16_t *universe,
                      uint8_t *net, uint8_t *subuni, uint16_t *prio,
                      bool *dhcp, char ip[16], char mask[16], char gw[16]);

/* Applica i valori ricevuti (da NFC o Web) alle variabili globali */
bool apply_config_vars(uint8_t dmx_mode_in, uint16_t universe_in,
                       uint8_t net, uint8_t subuni, uint16_t prio_in,
                       bool dhcp_in, const char *ip_str, const char *mask_str, const char *gw_str);

typedef enum
{
    CFG_DMX_ADDRESS, CFG_DMX_PORT_MODE, CFG_SOURCE_MODE,
    CFG_ARTNET_UNIVERSE, CFG_SACN_UNIVERSE, CFG_SACN_TRANSPORT,
    CFG_SACN_PRIORITY, CFG_AUTO_PRIORITY, CFG_DISPLAY_AUTO_OFF, CFG_OFF_TIME,
    CFG_USE_DHCP, CFG_IP, CFG_MASK, CFG_GATEWAY, CFG_DNS1, CFG_DNS2,
    CFG_PARAMETER_COUNT
} cfg_parameter_t;
/* Main, valore numerico; IPv4 è raw lwIP. Setter valida prima di applicare,
   senza EEPROM: il chiamante sceglie commit immediato UI o differito RDM. */
bool CFG_GetValue(cfg_parameter_t parameter, uint32_t *value);
bool CFG_ValidateValue(cfg_parameter_t parameter, uint32_t value);
bool CFG_SetValue(cfg_parameter_t parameter, uint32_t value);
const char *CFG_DmxPortName(uint8_t mode);

#endif /* INC_CFG_IO_H_ */
