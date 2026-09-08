
#ifndef USER_INTERFACE_H
#define USER_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

// Stati di pagina
typedef enum {
    UI_HOME = 0,
    UI_MENU,
    UI_PAGE_MODE,
    UI_PAGE_DMX,
    UI_PAGE_NET,
    UI_PAGE_ANET,
    UI_PAGE_SACN,
    UI_PAGE_SYS
} ui_state_t;

// Eventi di input generati dallo strato encoder/pulsante
typedef enum
{
    EVT_NONE = 0,
    EVT_ENC_LEFT,
    EVT_ENC_RIGHT,
    EVT_BTN_SHORT,
    EVT_BTN_LONG,
    EVT_BTN_DBL,
    EVT_IDLE_TIMEOUT
}
ui_event_t;



#ifdef __cplusplus
extern "C" {
#endif

void InitUserInterface(void);
void UserInterfaceTask(void);     // chiamare ~ogni 10 ms

// Callback per aggiornare la Home con stato rete corrente
void UI_OnNetRuntime(bool link_up, uint32_t ip_runtime_be);
void UI_OnModeRuntime(uint8_t mode); // 0=DMX,1=ArtNet,2=sACN

void NET_ApplyFromConfig(void);

#ifdef __cplusplus
}
#endif

#endif // USER_INTERFACE_H
