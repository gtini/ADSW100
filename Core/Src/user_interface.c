// ============================ user_interface.c =============================
// UI per ADSW100 (DOGS102-6 102x64, font 6x8) con encoder+push
// - Mantiene le firme: InitUserInterface(), UserInterfaceTask()
// - Usa le funzioni TESTO definite in lcd.h (LCD_DrawText / LCD_DrawChar)
// - Click breve dalla HOME: richiesta conferma -> Manual mode
// - Click lungo dalla HOME: richiesta codice (pagina nuova), poi MENU
// - Idle 10s → Home
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "lcd.h"
#include "defs.h"
#include "h_files.h"

/* ===================================================================================== */
/*                       PROTOTIPI FUNZIONI ESTERNE                                      */
/* ===================================================================================== */
HAL_StatusTypeDef EE_Read(uint16_t memAddr, uint8_t* buf, uint16_t len);
HAL_StatusTypeDef EE_Write(uint16_t memAddr, const uint8_t* buf, uint16_t len);
uint8_t  EE_ReadU8(uint16_t addr);
HAL_StatusTypeDef EE_WriteU8(uint16_t addr, uint8_t v);
uint16_t EE_ReadU16(uint16_t addr);
HAL_StatusTypeDef EE_WriteU16(uint16_t addr, uint16_t v);
uint32_t EE_ReadU32(uint16_t addr);
HAL_StatusTypeDef EE_WriteU32(uint16_t addr, uint32_t v);
HAL_StatusTypeDef EE_LoadConfig(void);

// Encoder/pulsante --> input
extern int16_t UI_GetEncoderDelta(void);
extern uint8_t UI_GetButtonState(void);  // bit0=short, bit1=long, bit2=dbl (confermati)

extern void NET_ScheduleApply(void);

// Variabili globali
/* ===================================================================================== */
/*                           Variabili Globali                                           */
/* ===================================================================================== */
uint16_t dmx_address;
uint8_t  dmx_mode;
uint8_t  active_proto;   // 0=DMX,1=ArtNet,2=sACN (usato quando dmx_mode==3 Auto)

uint8_t  off_timeout_s;
uint8_t  proto_priority;

// Variabili esterne

extern uint8_t lcd_backlight;
extern uint8_t g_netmask_bad;
extern volatile net_diag_state_t g_net_diag_state;

/* ===================================================================================== */
/*                                Costanti                                               */
/* ===================================================================================== */
#define UI_COLS             17      // 102px / 6px
#define UI_ROWS             8       // 64px / 8px
#define UI_CHAR_W 6
#define UI_CHAR_H 8

#define UI_IDLE_TIMEOUT_MS  10000   // 10 s

static const uint8_t kUiTickMs = 10;

typedef struct
{
    ui_state_t state;           // pagina corrente
    uint8_t    sel;             // indice selezione nel menu/pagina
    bool       editing;         // editing in corso della riga corrente
    uint32_t   idle_ms;         // contatore inattività
    bool       force_redraw;
}
ui_ctx_t;

static struct
{
    uint8_t  mode;
    uint8_t  active_proto;
    uint16_t dmx;
    uint8_t  use_dhcp;
    uint32_t ip_be;
    bool     link_up;
    uint8_t  net_diag;   // nuovo: stato NET visualizzato
    char     net_line[18];      // cache stringa riga 6 (17+NUL)
    uint32_t net_line_tick_ms;  // ultimo refresh riga 6
} s_home = { 0xFF, 0xFF, 0xFFFF, 0xFF, 0xFFFFFFFFu, false, 0xFF, "", 0u };


static ui_ctx_t g_ui;

// Stato runtime per Home
static bool     g_link_up = false;
static uint32_t g_ip_runtime_be = 0; // IP corrente (lease DHCP o statico attivo)

// MAC address da EEPROM (EUI-48 / derivato)
static uint8_t g_mac[6] = {0};
static bool    g_mac_valid = false;

// Selettori locali per pagine con più campi
static uint8_t s_dmx_sel;
static uint32_t s_dmx_edit_value;
static uint8_t s_net_sel = 0;           // 0..5 (DHCP,IP,Subnet,DNS1,DNS2,GW)
static uint8_t s_ip_octet_sel = 0;      // 0..3
static uint8_t s_sacn_sel = 0;          // 0..2 (Universe, Cast, Priority)
static uint8_t s_sys_sel = 0;           // 0..3: backlight, OFF, priorità, reset utente

// Codice di sblocco (4 cifre)
static uint8_t s_unlock_digits[4] = {0,0,0,0};
static uint8_t s_unlock_pos       = 0;      // 0..3

// Selettore per MANUAL MODE relè
static uint8_t s_manual_sel       = 0;      // 0..N_RLY-1

// Conferma ingresso in MANUAL MODE (0=NO default, 1=YES)
static uint8_t s_manual_confirm_sel = 0;

// ---- WRAPPER DI DISEGNO TESTO ---------------------------------------------
#ifndef UI_FONT
#define UI_FONT LCD_FONT_8
#endif

/* Main: join idempotente ripetuto anche dopo link flap; snapshot aggiornato
   soltanto dopo operazioni riuscite, retry senza attese al prossimo poll. */
void NET_ApplyFromConfig(void)
{
    static uint16_t previous_universe;
    static uint8_t previous_transport = 0xFF;
    if (!g_link_up || !g_ip_runtime_be) return;
    if (previous_universe != sacn_universe || previous_transport != sacn_transport)
    {
        if (previous_transport == 0 && previous_universe && !sACN_LeaveUniverse(previous_universe))
        {
            NET_ScheduleApply();
            return;
        }
        sACN_ResetData();
    }
    if (sacn_transport == 0 && !sACN_JoinUniverse(sacn_universe))
    {
        NET_ScheduleApply();
        return;
    }
    previous_universe = sacn_universe;
    previous_transport = sacn_transport;
    (void)ArtNet_FindUniverse(artnet_universe, true);
}

static void LCD_ClearRow(uint8_t row)
{
    char blank[UI_COLS + 1];
    memset(blank, ' ', UI_COLS);
    blank[UI_COLS] = 0;
    LCD_DrawText(blank, 0, row * UI_CHAR_H, UI_FONT, false, false);
}

static void LCD_PutStr(uint8_t col, uint8_t row, const char* s)
{
    LCD_DrawText(s ? s : "", col * UI_CHAR_W, row * UI_CHAR_H, UI_FONT, false, false);
}

static void LCD_PutStrCentered(uint8_t row, const char* s)
{
    size_t n = s ? strlen(s) : 0;
    if (n > UI_COLS) n = UI_COLS;
    uint8_t start_col = (uint8_t)((UI_COLS - n) / 2);
    LCD_PutStr(start_col, row, s);
}

static void LCD_PutChar(uint8_t col, uint8_t row, char c)
{
    char one[2] = { c, 0 };
    LCD_DrawText(one, col * UI_CHAR_W, row * UI_CHAR_H, UI_FONT, false, false);
}

static void LCD_DrawCursorArrow(uint8_t row)
{
    LCD_PutChar(0, row, '>');
}

static void LCD_EditMarker(uint8_t row, bool on_right)
{
    // marker a destra (colonna 16)
    LCD_PutChar(UI_COLS - 1, row, on_right ? '*' : ' ');
}

static void LCD_DrawToast(const char* s)
{
    LCD_ClearRow(7);
    LCD_PutStr(0, 7, s ? s : "");
}

/* ===================================================================================== */
/*                                UTILITY VARIE                                          */
/* ===================================================================================== */
static inline uint8_t ip_get_octet_le(uint32_t be, int idx)
{
    switch(idx & 3){
        case 0: return (be      ) & 0xFF;
        case 1: return (be >>  8) & 0xFF;
        case 2: return (be >> 16) & 0xFF;
        default:return (be >> 24) & 0xFF;
    }
}

static inline uint32_t ip_set_octet_le(uint32_t be, int idx, uint8_t v)
{
    uint32_t mask, val;
    switch(idx & 3){
        case 0: mask = 0xFFFFFF00u; val = (uint32_t)v;          break;
        case 1: mask = 0xFFFF00FFu; val = (uint32_t)v << 8;     break;
        case 2: mask = 0xFF00FFFFu; val = (uint32_t)v << 16;    break;
        default:mask = 0x00FFFFFFu; val = (uint32_t)v << 24;    break;
    }
    return (be & mask) | val;
}

static inline const char* mode_to_str(uint8_t m)
{
    return (m==0) ? "DMX   " : (m==1 ? "ArtNet" : (m==2 ? "sACN  " : "Auto "));
}

static inline const char* cast_to_str(uint8_t c_is_multicast_zero)
{
    return (c_is_multicast_zero == 0) ? "Mu" : "Un";
}

static inline const char* proto_prio_to_str(uint8_t p)
{
    static const char* k[6] = {
        "D>A>s",
        "D>s>A",
        "s>D>A",
        "s>A>D",
        "A>D>s",
        "A>s>D"
    };
    return (p < 6) ? k[p] : "?";
}

static void draw_ip_row(uint8_t row, char label_char,
                        uint32_t ip_be, bool editable,
                        bool in_edit, bool is_selected)
{
    // buffer riga: 17 colonne (UI_COLS) + NUL
    char line[UI_COLS + 1];
    memset(line, ' ', UI_COLS);
    line[UI_COLS] = '\0';

    // Colonna 0: indicatore (priorità: edit -> '*', poi selezione -> '>')
    line[0] = in_edit ? '*' : (is_selected ? '>' : ' ');

    // IP in ordine "umano" b0.b1.b2.b3
    const uint8_t b0 = (ip_be      ) & 0xFF;
    const uint8_t b1 = (ip_be >>  8) & 0xFF;
    const uint8_t b2 = (ip_be >> 16) & 0xFF;
    const uint8_t b3 = (ip_be >> 24) & 0xFF;

    if (!editable) {
        // Esempio: "I---.---.---.---"
        snprintf(&line[1], UI_COLS, "%c---.---.---.---", label_char);
    } else {
        // Esempio: "I192.168.000.142" (15 char)
        snprintf(&line[1], UI_COLS, "%c%03u.%03u.%03u.%03u",
                 label_char, b0, b1, b2, b3);
    }

    // Calcolo span invertita: ottetto selezionato occupa 3 char,
    // parte a colonna 2 (+1 per indicatore, +1 per lettera etichetta)
    int inv_start = -1, inv_len = 0;
    if (editable && in_edit) {
        inv_start = 2 + (int)s_ip_octet_sel * 4; // 2,6,10,14
        inv_len   = 3;
    }

    // Disegna in un colpo solo con la span invertita
    LCD_DrawTextInvSpan(line,
                        /*x*/ 0,
                        /*y*/ row * UI_CHAR_H,
                        UI_FONT,
                        /*inv_start_chars*/ inv_start,
                        /*inv_len_chars*/   inv_len,
                        /*blink*/ false);
}

/* ===================================================================================== */
/*                           Render delle pagine                                         */
/* ===================================================================================== */
static inline bool UI_BeginRender(void)
{
    if (g_ui.force_redraw)
    {
        LCD_ClearDisplay();
        g_ui.force_redraw = false;
        return true;
    }
    return false;
}

extern net_diag_state_t NET_GetDiagState(void);

static char proto_state_from_diag(uint32_t now, uint32_t last_any_ms, uint32_t last_cfg_ms)
{
    // Stato protocollo per UI:
    // N = nessun pacchetto recente
    // A = pacchetti recenti per il nostro universe configurato
    // I = traffico recente ma non per noi
    const uint32_t ACTIVE_MS = 3000u;

    if (last_any_ms == 0u) return 'N';
    if ((now - last_any_ms) > ACTIVE_MS) return 'N';
    if ((last_cfg_ms != 0u) && ((now - last_cfg_ms) <= ACTIVE_MS)) return 'A';
    return 'I';
}

static const char* net_diag_to_str(net_diag_state_t st)
{
    // 17 caratteri utili su una riga: pad sempre a 17 per evitare residui
    static char s_line[18];
    const char *fixed = NULL;

    switch (st)
    {
        case NET_DIAG_NO_LINK:        fixed = "NET: LINK DOWN"; break;
        case NET_DIAG_LISTENERS_OFF:  fixed = "NET: LSTN OFF";  break;
        case NET_DIAG_ERROR:          fixed = "NET: ERROR";     break;
        case NET_DIAG_MASK_ERR:       fixed = "NET: MASK ERR";  break;
        default:                      fixed = NULL;            break;
    }

    if (fixed)
    {
        // copia + padding
        size_t n = strlen(fixed);
        if (n > 17) n = 17;
        memcpy(s_line, fixed, n);
        while (n < 17) s_line[n++] = ' ';
        s_line[17] = '\0';
        return s_line;
    }

    // Link UP + listeners ON: mostra Art(x) s(y) oppure IDLE
    {
        artnet_diag_info_t ad;
        sacn_diag_info_t   sd;
        memset(&ad, 0, sizeof(ad));
        memset(&sd, 0, sizeof(sd));
        ArtNet_GetDiag(&ad);
        sACN_GetDiag(&sd);

        uint32_t now = HAL_GetTick();

        char a = proto_state_from_diag(now, ad.last_any_ms, ad.last_cfg_ms);
        char s = proto_state_from_diag(now, sd.last_any_ms, sd.last_cfg_ms);

        if ((a == 'N') && (s == 'N'))
        {
            const char *idle = "NET: IDLE";
            size_t n = strlen(idle);
            memcpy(s_line, idle, n);
            while (n < 17) s_line[n++] = ' ';
            s_line[17] = '\0';
            return s_line;
        }

        // NET: Art(A) s(N)  -> 16 char, +1 space
        snprintf(s_line, sizeof(s_line), "NET: Art(%c) s(%c)", a, s);
        // padding a 17
        size_t n = strlen(s_line);
        while (n < 17) s_line[n++] = ' ';
        s_line[17] = '\0';
        return s_line;
    }
}


static void render_home(void)
{
	bool full = UI_BeginRender();

	if (full)
	{
		// Disegno solo la parte statica
		const brand_info_t *info = Brand_Current();
        char identity[18];
        LCD_PutStr(0, 0, info->manufacturer);
        snprintf(identity, sizeof identity, "%s v%s", info->model, FW_VERSION_STR);
        LCD_PutStr(0, 1, identity);

		// invalida per forzare il primo update dei campi dinamici
		s_home.mode         = 0xFF;
		s_home.active_proto = 0xFF;
		s_home.dmx          = 0xFFFF;
		s_home.use_dhcp     = 0xFF;
		s_home.ip_be        = 0xFFFFFFFFu;
		s_home.link_up      = !g_link_up;
		s_home.net_diag     = 0xFF;

		s_home.net_line[0] = '\0';
		s_home.net_line_tick_ms = 0u;
	}

	// Aggiorna solo i campi che cambiano
	char tmp[24];

    if (dmx_port_mode != DMX_PORT_IN)
    {
        snprintf(tmp, sizeof(tmp), "Mode: %s", CFG_DmxPortName(dmx_port_mode));
        LCD_PutStr(0, 3, tmp);
        s_home.mode = 0xFF;
    }
    else if (dmx_mode == 3)
	{
		// Auto: aggiorna anche quando cambia il protocollo attivo runtime
		if ((s_home.mode != dmx_mode) || (s_home.active_proto != active_proto))
		{
			char c = '?';
			if (active_proto == 0) c = 'D';
			else if (active_proto == 1) c = 'A';
			else if (active_proto == 2) c = 's';

			snprintf(tmp, sizeof(tmp), "Mode: Auto (%c)", c);
			LCD_PutStr(0, 3, tmp);
			s_home.mode = dmx_mode;
			s_home.active_proto = active_proto;
		}
	}
	else
	{
		if (s_home.mode != dmx_mode)
		{
			snprintf(tmp, sizeof(tmp), "Mode: %s", mode_to_str(dmx_mode));
			LCD_PutStr(0, 3, tmp);
			s_home.mode = dmx_mode;
			s_home.active_proto = 0xFF; // non usato fuori da Auto
		}
	}

	if (s_home.dmx != dmx_address)
	{
		snprintf(tmp, sizeof(tmp), "DMX:  %03u", (unsigned)dmx_address);
		LCD_PutStr(0, 4, tmp);
		s_home.dmx = dmx_address;
	}

	uint32_t ip_be = (use_dhcp ? (g_ip_runtime_be ? g_ip_runtime_be : ip_address) : ip_address);

	if (s_home.use_dhcp != use_dhcp || s_home.ip_be != ip_be)
	{
		uint8_t b0=(ip_be)&0xFF, b1=(ip_be>>8)&0xFF, b2=(ip_be>>16)&0xFF, b3=(ip_be>>24)&0xFF;
		snprintf(tmp, sizeof(tmp), "%c:%u.%u.%u.%u", use_dhcp ? 'D' : 'S', b0,b1,b2,b3);
		LCD_PutStr(0, 5, tmp);
		s_home.use_dhcp = use_dhcp;
		s_home.ip_be    = ip_be;
	}

	// --- NET DIAG riga 6 ---
	{
		const uint32_t now = HAL_GetTick();

		// Aggiorna periodicamente per gestire correttamente A->N dopo timeout
		if ((now - s_home.net_line_tick_ms) >= 250u || s_home.net_diag != (uint8_t)g_net_diag_state)
		{
			char line[18]; // 17 char + NUL

			snprintf(line, sizeof(line), "%-17.17s", net_diag_to_str(g_net_diag_state));

			if (strncmp(line, s_home.net_line, 17) != 0)
			{
				LCD_PutStr(0, 6, line);
				memcpy(s_home.net_line, line, sizeof(s_home.net_line));
			}

			s_home.net_diag = (uint8_t)g_net_diag_state;
			s_home.net_line_tick_ms = now;
		}
	}

	// --- MAC address riga 7 ---
	if (g_mac_valid)
	{
		char mac_str[18]; // 17 char + NUL
		snprintf(mac_str, sizeof(mac_str),
				"%02X:%02X:%02X:%02X:%02X:%02X",
				g_mac[0], g_mac[1], g_mac[2],
				g_mac[3], g_mac[4], g_mac[5]);
		LCD_PutStr(0, 7, mac_str);
	}
}

static void render_menu(void)
{
    LCD_PutStrCentered(0, "MENU");
    static const char* items[] = { "Mode", "DMX", "Network", "Art-Net", "sACN", "System Setup" };
    for (uint8_t i = 0; i < 6; i++)
    {
        char line[20];
        snprintf(line, sizeof(line), "%c %s", (i==g_ui.sel?'>':' '), items[i]);
        LCD_PutStr(0, (uint8_t)(i+1), line);
    }
    LCD_PutStr(0, 7, "Long Click - Back");
}

static void render_page_mode(void)
{
    LCD_PutStrCentered(0, "Mode");

    char line[24];
    // mostro la riga editabile con il marker a dx se in edit
    snprintf(line, sizeof(line), "%c %s",
             (g_ui.editing ? '>' : '>'),   // freccia sempre sulla riga unica
             mode_to_str(dmx_mode));
    LCD_PutStr(0, 2, line);

    LCD_EditMarker(2, g_ui.editing);
    LCD_PutStr(0, 7, g_ui.editing ? "Click=Save" : "Long=Back");
}

static cfg_parameter_t UI_DmxParameter(void)
{
    if (s_dmx_sel == 0) return CFG_DMX_ADDRESS;
    if (s_dmx_sel == 1) return CFG_DMX_PORT_MODE;
    return dmx_port_mode == DMX_PORT_ARTNET_NODE ? CFG_ARTNET_UNIVERSE : CFG_SACN_UNIVERSE;
}

static void render_page_dmx(void)
{
    LCD_PutStrCentered(0, "DMX");
    char line[32];
    for (unsigned i = 0; i < 3; ++i)
    {
        uint32_t value = i == 0 ? dmx_address : i == 1 ? dmx_port_mode :
                         dmx_port_mode == DMX_PORT_ARTNET_NODE ? artnet_universe : sacn_universe;
        if (g_ui.editing && i == s_dmx_sel) value = s_dmx_edit_value;
        char cursor = s_dmx_sel == i ? '>' : ' ';
        if (i == 0) snprintf(line, sizeof line, "%c Address [%03u]", cursor, (unsigned)value);
        else if (i == 1) snprintf(line, sizeof line, "%c %s", cursor, CFG_DmxPortName(value));
        else if (dmx_port_mode == DMX_PORT_IN) snprintf(line, sizeof line, "%c Universe [--]", cursor);
        else snprintf(line, sizeof line, "%c Universe [%u]", cursor, (unsigned)value);
        LCD_PutStr(0, (uint8_t)(2 + i), line);
        LCD_EditMarker((uint8_t)(2 + i), g_ui.editing && s_dmx_sel == i);
    }
    LCD_PutStr(0, 7, g_ui.editing ? "Click=Save" : "Long=Back");
}

static void render_page_net(void)
{
    LCD_PutStrCentered(0, "Network");

    char line[24];
    snprintf(line, sizeof(line), "%c DHCP [%s]",
             (s_net_sel==0 ? '>' : ' '), use_dhcp? "ON":"OFF");
    LCD_PutStr(0, 1, line);

    bool editable = (use_dhcp == 0);

    draw_ip_row(2, 'I', ip_address, editable, g_ui.editing && s_net_sel==1, s_net_sel==1);
    draw_ip_row(3, 'S', net_mask,   editable, g_ui.editing && s_net_sel==2, s_net_sel==2);
    draw_ip_row(4, '1', dns1,       editable, g_ui.editing && s_net_sel==3, s_net_sel==3);
    draw_ip_row(5, '2', dns2,       editable, g_ui.editing && s_net_sel==4, s_net_sel==4);
    draw_ip_row(6, 'G', gateway,    editable, g_ui.editing && s_net_sel==5, s_net_sel==5);

    LCD_PutStr(0, 7, g_ui.editing ? "Click=Save" : "Long=Back");
}

static void render_page_anet(void)
{
    LCD_PutStrCentered(0, "Art-Net");

    char buf[28];
    snprintf(buf, sizeof(buf), "Univ. [%5u]", (unsigned)(artnet_universe + 1u));
    LCD_PutStr(1, 2, buf);

    LCD_EditMarker(2, g_ui.editing);
    LCD_PutStr(0, 7, g_ui.editing ? "Click=Save" : "Long=Back");
    LCD_DrawCursorArrow(2);
}

static void render_page_sacn(void)
{
    LCD_PutStrCentered(0, "sACN");

    char line[32];

    snprintf(line, sizeof(line), "%c Univ. [%5u]",
             (s_sacn_sel==0 ? '>' : ' '), (unsigned)sacn_universe);
    LCD_PutStr(0, 2, line);
    LCD_EditMarker(2, (g_ui.editing && s_sacn_sel==0));

    snprintf(line, sizeof(line), "%c Multicast [%s]",
             (s_sacn_sel==1 ? '>' : ' '), cast_to_str(sacn_transport));
    LCD_PutStr(0, 3, line);
    LCD_EditMarker(3, (g_ui.editing && s_sacn_sel==1));

    snprintf(line, sizeof(line), "%c Priority [%3u]",
             (s_sacn_sel==2 ? '>' : ' '), (unsigned)sacn_min_prio);
    LCD_PutStr(0, 4, line);
    LCD_EditMarker(4, (g_ui.editing && s_sacn_sel==2));

    LCD_PutStr(0, 7, g_ui.editing ? "Click=Save" : "Long=Back");
}

/* Solo impostazioni utente: identità e policy di accesso appartengono al servizio CDC. */
static void render_page_sys(void)
{
    LCD_PutStrCentered(0, "System Setup");

    // Riga 2: Backlight [30s / ON]
    {
        char line[32];

        snprintf(line, sizeof(line), "%c BLight [%s]",
                 (s_sys_sel == 0 ? '>' : ' '),
                 (lcd_backlight == 0 ? "30s" : "ON"));

        LCD_PutStr(0, 2, line);
        LCD_EditMarker(2, (g_ui.editing && s_sys_sel == 0));
    }

    // Riga 3: OFF Timeout [ss]
    {
        char line[32];
        snprintf(line, sizeof(line), "%c OFF Time [%2us]",
                 (s_sys_sel==1 ? '>' : ' '), (unsigned)off_timeout_s);
        LCD_PutStr(0, 3, line);
        LCD_EditMarker(3, (g_ui.editing && s_sys_sel==1));
    }

    // Riga 4: Priority
    {
        char line[32];
        snprintf(line, sizeof(line), "%c Prio [%s]",
                 (s_sys_sel==2 ? '>' : ' '), proto_prio_to_str(proto_priority));
        LCD_PutStr(0, 4, line);
        LCD_EditMarker(4, (g_ui.editing && s_sys_sel==2));
    }

    // Riga 5: Init Memory
    {
        char line[32];
        snprintf(line, sizeof(line), "%c Init Memory",
                 (s_sys_sel==3 ? '>' : ' '));
        LCD_PutStr(0, 5, line);
    }

    LCD_PutStr(0, 7, g_ui.editing ? "Click=Save" : "Click=Edit");
}

// Pagina UNLOCK: codice a 4 cifre
static void render_page_unlock(void)
{
    LCD_PutStr(6, 0, "Unlock");
    LCD_PutStr(3, 1, "Insert Code");
    LCD_PutStr(3, 6, "Rotate=Set");
    LCD_PutStr(3, 7, "Click=Next");

    // Riga 3: colonna 5, "0000" con reverse sul digit selezionato
    char line[UI_COLS + 1];
    memset(line, ' ', UI_COLS);
    line[UI_COLS] = '\0';

    for (int i = 0; i < 4; i++)
    {
        line[6 + i] = (char)('0' + s_unlock_digits[i]);
    }

    // inverti il digit selezionato
    LCD_DrawTextInvSpan(line,
                        0,
                        3 * UI_CHAR_H,
                        UI_FONT,
                        6 + s_unlock_pos,
                        1,
                        false);
}

// Pagina MANUAL CONFIRM: richiesta conferma prima di entrare in Manual mode
static void render_page_manual_confirm(void)
{
    LCD_PutStrCentered(0, "Manual control");
    LCD_PutStrCentered(2, "Enter manual");
    LCD_PutStrCentered(3, "output control?");

    // Riga 5: opzioni NO / YES (default selezionato: NO)
    // Mostra entrambe, invertendo quella selezionata
    char line[UI_COLS + 1];
    memset(line, ' ', UI_COLS);
    line[UI_COLS] = '\0';

    // layout: "[ NO ]   [ YES ]" centrato circa
    const uint8_t col_no  = 3;
    const uint8_t col_yes = 10;
    memcpy(&line[col_no],  "NO",  2);
    memcpy(&line[col_yes], "YES", 3);

    // inverti l'opzione selezionata
    if (s_manual_confirm_sel == 0)
    {
        LCD_DrawTextInvSpan(line, 0, 5 * UI_CHAR_H, UI_FONT, col_no, 2, false);
    }
    else
    {
        LCD_DrawTextInvSpan(line, 0, 5 * UI_CHAR_H, UI_FONT, col_yes, 3, false);
    }

    LCD_PutStr(0, 7, "Click=OK Long=Bck");
}

// Pagina MANUAL MODE: controllo relè
static void render_page_manual(void)
{
    LCD_PutStr(3, 0, "Manual mode");
    LCD_PutStr(2, 6, "Rotate=CH Sel");
    LCD_PutStr(3, 7, "Click=Toggle");

    // Riga 3: CH:<numero canale>

    char chinfo[16];
    snprintf(chinfo, sizeof(chinfo), "CH:%2u", (unsigned)(s_manual_sel + 1));
    LCD_PutStr(6, 2, chinfo);


    // Riga 4: stato relè centrato
    char line[UI_COLS + 1];
    memset(line, ' ', UI_COLS);
    line[UI_COLS] = '\0';

    uint16_t mask = Relay_GetOnMask();
    uint8_t start_col = 2;

    for (int i = 0; i < N_RLY; i++)
    {
        line[start_col + i] = (mask & (1u << i)) ? 'I' : 'O';
    }

    // inverti il carattere selezionato (s_manual_sel)
    LCD_DrawTextInvSpan(line,
                        0,
                        4 * UI_CHAR_H,
                        UI_FONT,
                        start_col + s_manual_sel,
                        1,
                        false);
}

/* ===================================================================================== */
/*                             Eventi di input                                           */
/* ===================================================================================== */
static ui_event_t PollInputToEvent(void)
{
    int16_t d  = UI_GetEncoderDelta();
    uint8_t bs = UI_GetButtonState(); // bit0=short, bit1=long, bit2=dbl

    if (bs & 0x02) return EVT_BTN_LONG;
    if (bs & 0x04) return EVT_BTN_DBL;
    if (bs & 0x01) return EVT_BTN_SHORT;
    if (d < 0)     return EVT_ENC_LEFT;
    if (d > 0)     return EVT_ENC_RIGHT;
    return EVT_NONE;
}

/* ===================================================================================== */
/*                       Gestione eventi nelle pagine                                    */
/* ===================================================================================== */
static void handle_menu_event(ui_event_t ev)
{
    switch (ev)
    {
        case EVT_ENC_LEFT:
            g_ui.sel = (uint8_t)((g_ui.sel + 5) % 6);
            break;

        case EVT_ENC_RIGHT:
            g_ui.sel = (uint8_t)((g_ui.sel + 1) % 6);
            break;

        case EVT_BTN_SHORT:
            switch (g_ui.sel)
            {
                case 0: g_ui.state = UI_PAGE_MODE; break;
                case 1: g_ui.state = UI_PAGE_DMX; break;
                case 2: g_ui.state = UI_PAGE_NET; s_net_sel = 0; break;
                case 3: g_ui.state = UI_PAGE_ANET; break;
                case 4: g_ui.state = UI_PAGE_SACN; s_sacn_sel = 0; break;
                case 5: g_ui.state = UI_PAGE_SYS; break;
            }
            g_ui.force_redraw = true;
            g_ui.editing = false;
            break;

        case EVT_BTN_LONG:
            g_ui.state = UI_HOME;
            g_ui.force_redraw = true;
            break;

        default: break;
    }
}

static void handle_page_mode_event(ui_event_t ev)
{
    switch (ev)
    {
        case EVT_ENC_LEFT:
            if (g_ui.editing)
                dmx_mode = (uint8_t)((dmx_mode + 3) % 4); // 0..3
            break;

        case EVT_ENC_RIGHT:
            if (g_ui.editing)
                dmx_mode = (uint8_t)((dmx_mode + 1) % 4);
            break;

        case EVT_BTN_SHORT:
            if (!g_ui.editing)
            {
                g_ui.editing = true;
                LCD_EditMarker(2, true);
                LCD_DrawToast("Edit: rotate, click=save");
            }
            else
            {
                // salva subito
                if (dmx_mode > 3) dmx_mode = 0;
                if (EE_WriteU8(EE_OFFS_DMX_MODE, dmx_mode) != HAL_OK) { LCD_DrawToast("EEPROM error"); return; }
                NET_ScheduleApply();
                LCD_EditMarker(2, false);
                g_ui.editing = false;
                LCD_DrawToast("Saved");
            }
            break;

        case EVT_BTN_LONG:
            g_ui.state = UI_MENU;
            g_ui.force_redraw = true;
            g_ui.editing = false;
            break;

        case EVT_IDLE_TIMEOUT:
            g_ui.state = UI_HOME;
            g_ui.force_redraw = true;
            g_ui.editing = false;
            break;

        default: break;
    }
}

/* Candidato locale: uscita senza conferma non cambia direzione o universo. */
static void handle_page_dmx_event(ui_event_t ev)
{
    if (ev == EVT_ENC_LEFT || ev == EVT_ENC_RIGHT)
    {
        if (!g_ui.editing)
        {
            s_dmx_sel = (uint8_t)((s_dmx_sel + (ev == EVT_ENC_RIGHT ? 1 : 2)) % 3);
        }
        else
        {
            uint32_t minimum = s_dmx_sel == 0 ? 1 : s_dmx_sel == 2 && dmx_port_mode == DMX_PORT_SACN_NODE ? 1 : 0;
            uint32_t maximum = s_dmx_sel == 0 ? 512 : s_dmx_sel == 1 ? 2 :
                               dmx_port_mode == DMX_PORT_ARTNET_NODE ? 32767 : 63999;
            if (ev == EVT_ENC_LEFT && s_dmx_edit_value > minimum) --s_dmx_edit_value;
            if (ev == EVT_ENC_RIGHT && s_dmx_edit_value < maximum) ++s_dmx_edit_value;
        }
        g_ui.force_redraw = true;
    }
    else if (ev == EVT_BTN_SHORT)
    {
        if (s_dmx_sel == 2 && dmx_port_mode == DMX_PORT_IN) return;
        if (!g_ui.editing)
        {
            CFG_GetValue(UI_DmxParameter(), &s_dmx_edit_value);
            g_ui.editing = true;
        }
        else
        {
            cfg_parameter_t parameter = UI_DmxParameter();
            uint32_t previous;
            CFG_GetValue(parameter, &previous);
            if (!CFG_SetValue(parameter, s_dmx_edit_value)) return;
            if (EE_SaveConfig() != HAL_OK)
            {
                (void)CFG_SetValue(parameter, previous);
                LCD_DrawToast("EEPROM error");
                return;
            }
            g_ui.editing = false;
            LCD_DrawToast("Saved");
        }
        g_ui.force_redraw = true;
    }
    else if (ev == EVT_BTN_LONG || ev == EVT_IDLE_TIMEOUT)
    {
        g_ui.editing = false;
        g_ui.state = ev == EVT_IDLE_TIMEOUT ? UI_HOME : UI_MENU;
        g_ui.force_redraw = true;
    }
}

static void ip_octet_edit(uint32_t* be, int oct_idx, int delta)
{
    int v = (int)ip_get_octet_le(*be, oct_idx) + delta;
    if (v < 0)   v += 256;
    if (v > 255) v -= 256;;
    *be = ip_set_octet_le(*be, oct_idx, (uint8_t)v);
}

static void handle_page_net_event(ui_event_t ev)
{
    switch (ev)
    {
    case EVT_ENC_LEFT:
    {
        if (!g_ui.editing) {
            // scorrimento voci: 0..5 (DHCP, IP, Mask, DNS1, DNS2, GW)
            s_net_sel = (uint8_t)((s_net_sel + 5) % 6);   // -1 modulo 6
        } else if (s_net_sel >= 1 && s_net_sel <= 5 && use_dhcp == 0) {
            uint32_t* tgt = (s_net_sel==1)? &ip_address :
                            (s_net_sel==2)? &net_mask   :
                            (s_net_sel==3)? &dns1       :
                            (s_net_sel==4)? &dns2       : &gateway;
            ip_octet_edit(tgt, s_ip_octet_sel, -1);
        }
    } break;

    case EVT_ENC_RIGHT:
    {
        if (!g_ui.editing) {
            s_net_sel = (uint8_t)((s_net_sel + 1) % 6);   // +1 modulo 6
        } else if (s_net_sel >= 1 && s_net_sel <= 5 && use_dhcp == 0) {
            uint32_t* tgt = (s_net_sel==1)? &ip_address :
                            (s_net_sel==2)? &net_mask   :
                            (s_net_sel==3)? &dns1       :
                            (s_net_sel==4)? &dns2       : &gateway;
            ip_octet_edit(tgt, s_ip_octet_sel, +1);
        }
    } break;

    case EVT_BTN_SHORT:
    {
        if (s_net_sel == 0) {
            // Toggle DHCP e salva subito
            use_dhcp ^= 1;
            if (EE_Write(EE_OFFS_USE_DHCP, (uint8_t[]){use_dhcp}, 1) != HAL_OK) { LCD_DrawToast("EEPROM error"); return; }
            NET_ScheduleApply();
            g_ui.editing = false;
            LCD_DrawToast("Saved");
            break;
        }

        // voci IP-like (1..5)
        if (use_dhcp) {
            // se era ON, spegni e salva prima di entrare in edit
            use_dhcp = 0;
            if (EE_Write(EE_OFFS_USE_DHCP, (uint8_t[]){0}, 1) != HAL_OK) { LCD_DrawToast("EEPROM error"); return; }
            NET_ScheduleApply();
            LCD_DrawToast("DHCP OFF");
            break;
        }

        if (!g_ui.editing) {
            // entra in edit sull’ottetto 0
            g_ui.editing = true;
            s_ip_octet_sel = 0;
            LCD_DrawToast("Edit: rotate, click=next");
        } else {
            // SALVA l’IP/mask/DNS/gw completo ad ogni click
            switch (s_net_sel) {
            case 1: if (EE_WriteU32(EE_OFFS_IP,      ip_address) != HAL_OK) { LCD_DrawToast("EEPROM error"); return; }  break;
            case 2: if (EE_WriteU32(EE_OFFS_NETMASK, net_mask) != HAL_OK) { LCD_DrawToast("EEPROM error"); return; }    break;
            case 3: if (EE_WriteU32(EE_OFFS_DNS1,    dns1) != HAL_OK) { LCD_DrawToast("EEPROM error"); return; }        break;
            case 4: if (EE_WriteU32(EE_OFFS_DNS2,    dns2) != HAL_OK) { LCD_DrawToast("EEPROM error"); return; }        break;
            case 5: if (EE_WriteU32(EE_OFFS_GATEWAY, gateway) != HAL_OK) { LCD_DrawToast("EEPROM error"); return; }     break;
            }
            NET_ScheduleApply();

            if (s_ip_octet_sel < 3) {
                s_ip_octet_sel++;
            } else {
                // finito il 4° ottetto --> esci dall’edit
                g_ui.editing = false;
                LCD_DrawToast("Saved");
            }
        }
    } break;

    case EVT_BTN_DBL:
        // nessuna azione
        break;

    case EVT_BTN_LONG:
    {
        // Back rapido --> Menu
        g_ui.state = UI_MENU;
        g_ui.force_redraw = true;
        g_ui.editing = false;
    } break;

    case EVT_IDLE_TIMEOUT:
    {
        g_ui.state = UI_HOME;
        g_ui.force_redraw = true;
        g_ui.editing = false;
    } break;

    default: break;
    }
}

static void handle_page_anet_event(ui_event_t ev)
{
    switch (ev)
    {
        case EVT_ENC_LEFT:
        {
            if (g_ui.editing && artnet_universe > 0)
            {
                artnet_universe--;
            }
        } break;

        case EVT_ENC_RIGHT:
        {
            if (g_ui.editing && artnet_universe < 32767)
            {
                artnet_universe++;
            }
        } break;

        case EVT_BTN_SHORT:
        {
            if (!g_ui.editing)
            {
                g_ui.editing = true;
                LCD_EditMarker(2, true);
                LCD_DrawToast("Edit: rotate, click=save");
            }
            else
            {
                if (EE_WriteU16(EE_OFFS_ARTNET_UNI, artnet_universe) != HAL_OK) { LCD_DrawToast("EEPROM error"); return; }
                LCD_EditMarker(2, false);
                g_ui.editing = false;
                NET_ScheduleApply();
                LCD_DrawToast("Saved");
            }
        } break;

        case EVT_BTN_LONG:
        {
            g_ui.state = UI_MENU;
            g_ui.force_redraw = true;
            g_ui.editing = false;
        } break;

        case EVT_IDLE_TIMEOUT:
        {
            g_ui.state = UI_HOME;
            g_ui.force_redraw = true;
            g_ui.editing = false;
        } break;

        default: break;
    }
}

static void handle_page_sacn_event(ui_event_t ev)
{
    switch (ev)
    {
    case EVT_ENC_LEFT:
    {
        if (!g_ui.editing)
        {
            // Scorri tra le 3 voci (0..2)
            s_sacn_sel = (uint8_t)((s_sacn_sel + 2) % 3); // -1 modulo 3
        }
        else
        {
            // In edit: decrementa il campo selezionato
            if (s_sacn_sel == 0 && sacn_universe > 1)
                sacn_universe--;
            if (s_sacn_sel == 2 && sacn_min_prio > 0)
                sacn_min_prio--;
        }
    } break;

    case EVT_ENC_RIGHT:
    {
        if (!g_ui.editing)
        {
            s_sacn_sel = (uint8_t)((s_sacn_sel + 1) % 3); // +1 modulo 3
        }
        else
        {
            // In edit: incrementa il campo selezionato
            if (s_sacn_sel == 0 && sacn_universe < 63999)
                sacn_universe++;
            if (s_sacn_sel == 2 && sacn_min_prio < 200)
                sacn_min_prio++;
        }
    } break;

    case EVT_BTN_SHORT:
    {
        if (s_sacn_sel == 1)
        {
            // Toggle Multicast / Unicast + salva subito
            sacn_transport ^= 1; // 0=Multicast, 1=Unicast
            if (EE_Write(EE_OFFS_SACN_TRANSPORT, (uint8_t[]){sacn_transport}, 1) != HAL_OK) { LCD_DrawToast("EEPROM error"); return; }
            NET_ScheduleApply();
            LCD_DrawToast("Saved");
        }
        else
        {
            if (!g_ui.editing)
            {
                // Entra in edit sulla riga selezionata
                g_ui.editing = true;
                LCD_EditMarker((uint8_t)(2 + s_sacn_sel), true);
                LCD_DrawToast("Edit: rotate, click=save");
            }
            else
            {
                // Salvataggio immediato del valore corrente
                if (s_sacn_sel == 0)
                {
                    if (sacn_universe < 1)     sacn_universe = 1;
                    if (sacn_universe > 63999) sacn_universe = 63999;
                    if (EE_WriteU16(EE_OFFS_SACN_UNI, sacn_universe) != HAL_OK) { LCD_DrawToast("EEPROM error"); return; }
                }
                else if (s_sacn_sel == 2)
                {
                    if (sacn_min_prio > 200) sacn_min_prio = 200;
                    if (EE_Write(EE_OFFS_SACN_MIN_PRIO, (uint8_t[]){sacn_min_prio}, 1) != HAL_OK) { LCD_DrawToast("EEPROM error"); return; }
                }

                NET_ApplyFromConfig();
                LCD_EditMarker((uint8_t)(2 + s_sacn_sel), false);
                g_ui.editing = false;
                LCD_DrawToast("Saved");
            }
        }
    } break;

    case EVT_BTN_LONG:
    {
        g_ui.state = UI_MENU;
        g_ui.force_redraw = true;
        g_ui.editing = false;
    } break;

    case EVT_IDLE_TIMEOUT:
    {
        g_ui.state = UI_HOME;
        g_ui.force_redraw = true;
        g_ui.editing = false;
    } break;

    default:
        break;
    }
}

static void handle_page_sys_event(ui_event_t ev)
{
    switch (ev)
    {
        case EVT_ENC_LEFT:
        {
            if (!g_ui.editing)
            {
                s_sys_sel = (uint8_t)((s_sys_sel + 1) % 4);
            }
            else if (s_sys_sel == 0)
            {
                if (lcd_backlight > 0) lcd_backlight--;
                g_ui.force_redraw = true; // aggiorna subito il numero
            }
            else if (s_sys_sel == 1)
            {
                if (off_timeout_s > 0) off_timeout_s--;
                g_ui.force_redraw = true;
            }
            else if (s_sys_sel == 2)
            {
                if (proto_priority > 0) proto_priority--;
                g_ui.force_redraw = true;
            }
        }
        break;

        case EVT_ENC_RIGHT:
        {
            if (!g_ui.editing)
            {
                s_sys_sel = (uint8_t)((s_sys_sel + 1) % 4);
            }
            else if (s_sys_sel == 0)
            {
                if (lcd_backlight < 1) lcd_backlight++;
                g_ui.force_redraw = true; // aggiorna subito il numero
            }
            else if (s_sys_sel == 1)
            {
                if (off_timeout_s < 30) off_timeout_s++;
                g_ui.force_redraw = true;
            }
            else if (s_sys_sel == 2)
            {
                if (proto_priority < 5) proto_priority++;
                g_ui.force_redraw = true;
            }
        }
        break;

        case EVT_BTN_SHORT:
        {
            if (s_sys_sel == 0 || s_sys_sel == 1 || s_sys_sel == 2)
            {
                // Parametri editabili: entra/uscita edit con salvataggio
                const uint8_t row = (uint8_t)(2 + s_sys_sel); // 0->2, 1->3, 2->4

                if (!g_ui.editing)
                {
                    g_ui.editing = true;
                    LCD_EditMarker(row, true);
                    LCD_DrawToast("Edit: rotate, click=save");
                }
                else
                {
                    if (s_sys_sel == 0)
                    {
                        if (EE_WriteU8(EE_OFFS_LCD_BACKLIGHT, lcd_backlight) != HAL_OK) { LCD_DrawToast("EEPROM error"); return; }
                    }
                    else if (s_sys_sel == 1)
                    {
                        if (off_timeout_s > 30) off_timeout_s = 30;
                        if (EE_WriteU8(EE_OFFS_OFF_TIMEOUT_S, off_timeout_s) != HAL_OK) { LCD_DrawToast("EEPROM error"); return; }
                    }
                    else // s_sys_sel == 2
                    {
                        if (proto_priority > 5) proto_priority = 0;
                        if (EE_WriteU8(EE_OFFS_PROTO_PRIORITY, proto_priority) != HAL_OK) { LCD_DrawToast("EEPROM error"); return; }
                    }

                    g_ui.editing = false;
                    LCD_EditMarker(row, false);
                    LCD_DrawToast("Saved");
                }
            }
            else
            {
                // Init Memory
                HardFactoryReset(true);
                /* Un reset riuscito riavvia la MCU e non ritorna qui. */
                LCD_DrawToast("EEPROM error");
                g_ui.force_redraw = true;
            }
        }
        break;

        case EVT_BTN_LONG:
        case EVT_IDLE_TIMEOUT:
        {
            g_ui.state = UI_MENU;
            g_ui.force_redraw = true;
            g_ui.editing = false;
        }
        break;

        default: break;
    }
}

static void handle_page_unlock_event(ui_event_t ev)
{
    switch (ev)
    {
        case EVT_ENC_LEFT:
            if (s_unlock_digits[s_unlock_pos] == 0)
                s_unlock_digits[s_unlock_pos] = 9;
            else
                s_unlock_digits[s_unlock_pos]--;
            break;

        case EVT_ENC_RIGHT:
            s_unlock_digits[s_unlock_pos] =
                (uint8_t)((s_unlock_digits[s_unlock_pos] + 1) % 10);
            break;

        case EVT_BTN_SHORT:
        {
            if (s_unlock_pos < 3)
            {
                // passa al digit successivo
                s_unlock_pos++;
            }
            else
            {
                // verifica codice
                bool ok =
                    (s_unlock_digits[0] == UNLOCK_CODE_1) &&
                    (s_unlock_digits[1] == UNLOCK_CODE_2) &&
                    (s_unlock_digits[2] == UNLOCK_CODE_3) &&
                    (s_unlock_digits[3] == UNLOCK_CODE_4);

                // reset per la prossima volta
                s_unlock_pos = 0;
                s_unlock_digits[0] = s_unlock_digits[1] =
                s_unlock_digits[2] = s_unlock_digits[3] = 0;

                if (ok)
                {
                    LCD_DrawToast("Unlocked");
                    g_ui.state = UI_MENU;
                    g_ui.sel   = 0;
                    g_ui.force_redraw = true;
                }
                else
                {
                    LCD_DrawToast("Wrong code");
                    g_ui.state = UI_HOME;
                    g_ui.force_redraw = true;
                }
            }
        } break;

        case EVT_BTN_LONG:
        case EVT_IDLE_TIMEOUT:
        {
            // annulla e torna a home
            s_unlock_pos = 0;
            s_unlock_digits[0] = s_unlock_digits[1] =
            s_unlock_digits[2] = s_unlock_digits[3] = 0;
            g_ui.state = UI_HOME;
            g_ui.force_redraw = true;
        } break;

        default:
            break;
    }
}

static void handle_page_manual_confirm_event(ui_event_t ev)
{
    switch (ev)
    {
        case EVT_ENC_LEFT:
        case EVT_ENC_RIGHT:
            // Toggle tra NO (0) e YES (1)
            s_manual_confirm_sel ^= 1u;
            break;

        case EVT_BTN_SHORT:
            if (s_manual_confirm_sel == 1)
            {
                // YES -> entra in manual mode
                s_manual_sel = 0;
                Relay_ManualEnter();
                g_ui.state = UI_PAGE_MANUAL;
                g_ui.force_redraw = true;
            }
            else
            {
                // NO -> torna a home
                g_ui.state = UI_HOME;
                g_ui.force_redraw = true;
            }
            break;

        case EVT_BTN_LONG:
        case EVT_IDLE_TIMEOUT:
            // annulla
            g_ui.state = UI_HOME;
            g_ui.force_redraw = true;
            break;

        default:
            break;
    }
}

static void handle_page_manual_event(ui_event_t ev)
{
    switch (ev)
    {
        case EVT_ENC_LEFT:
            if (s_manual_sel == 0)
                s_manual_sel = (uint8_t)(N_RLY - 1);
            else
                s_manual_sel--;
            break;

        case EVT_ENC_RIGHT:
            s_manual_sel = (uint8_t)((s_manual_sel + 1) % N_RLY);
            break;

        case EVT_BTN_SHORT:
            // toggle relè selezionato (indice 0..N_RLY-1)
            Relay_ManualToggle(s_manual_sel);
            break;

        case EVT_BTN_LONG:
            // esci dalla modalità manuale SOLO con click lungo
            Relay_ManualExit();
            g_ui.state = UI_HOME;
            g_ui.force_redraw = true;
            break;

        default:
            // niente idle timeout in manuale
            break;
    }
}

/* ===================================================================================== */
/*                          Inizializzazione UI                                          */
/* ===================================================================================== */
void InitUserInterface(void)
{
    memset(&g_ui, 0, sizeof(g_ui));
    (void)EE_LoadConfig();             // Carica la configurazione

    // Leggi MAC da EEPROM (E48/E64/UID MCU)
    if (EE_GetLocalMAC48(g_mac) == HAL_OK)
    {
        g_mac_valid = true;
    }
    else
    {
        g_mac_valid = false;
    }

    g_ui.state = UI_HOME;
    g_ui.sel = 0;
    g_ui.editing = false;
    g_ui.idle_ms = 0;
    g_ui.force_redraw = true;

    LCD_ClearDisplay();
}

/* ===================================================================================== */
/*                               UI Task (10mS)                                          */
/* ===================================================================================== */
/* Click lungo HOME: richiesta persistente oppure accesso diretto NO_CODE. */
static void UI_RequestMenu(void)
{
    s_unlock_pos = 0;
    memset(s_unlock_digits, 0, sizeof s_unlock_digits);
    if (menu_code_required)
    {
        g_ui.state = UI_PAGE_UNLOCK;
    }
    else
    {
        g_ui.state = UI_MENU;
        g_ui.sel = 0;
    }
    g_ui.force_redraw = true;
}

void UserInterfaceTask(void)
{
    static ui_state_t prev = (ui_state_t)-1;

    // Idle timer
    g_ui.idle_ms += kUiTickMs;

    // Input --> evento
    ui_event_t ev = PollInputToEvent();
    if (ev != EVT_NONE)
    {
        g_ui.idle_ms = 0;          // attività utente
        LCD_Backlight_Wake();      // riaccendi e resetta timer BL
    }

    // Timeout di inattività --> genera evento dedicato
    // (MA non in HOME e non in MANUAL MODE)
    if (g_ui.idle_ms >= UI_IDLE_TIMEOUT_MS &&
        g_ui.state != UI_HOME &&
        g_ui.state != UI_PAGE_MANUAL)
    {
        ev = EVT_IDLE_TIMEOUT;
    }

    if (g_ui.state == UI_HOME && prev != UI_HOME)
    {
        g_ui.force_redraw = true;
        // ogni volta che rientri in HOME esci da manual mode
        Relay_ManualExit();
    }

    // Render pagina corrente (se richiesto) – solo NON-HOME
    if (g_ui.force_redraw && g_ui.state != UI_HOME)
    {
        LCD_ClearDisplay();
        g_ui.force_redraw = false;
    }

    switch (g_ui.state)
    {
        case UI_HOME:        render_home();        break;
        case UI_MENU:        render_menu();        break;
        case UI_PAGE_MODE:   render_page_mode();   break;
        case UI_PAGE_DMX:    render_page_dmx();    break;
        case UI_PAGE_NET:    render_page_net();    break;
        case UI_PAGE_ANET:   render_page_anet();   break;
        case UI_PAGE_SACN:   render_page_sacn();   break;
        case UI_PAGE_SYS:    render_page_sys();    break;
        case UI_PAGE_UNLOCK: render_page_unlock(); break;
        case UI_PAGE_MANUAL_CONFIRM: render_page_manual_confirm(); break;
        case UI_PAGE_MANUAL: render_page_manual(); break;
        default:             g_ui.state = UI_HOME; break;
    }

    // Gestione evento
    switch (g_ui.state)
    {
        case UI_HOME:
            if (ev == EVT_BTN_SHORT)
            {
                // Dalla HOME: click breve --> conferma ingresso in Manual mode
                s_manual_confirm_sel = 0; // default: NO
                g_ui.state = UI_PAGE_MANUAL_CONFIRM;
                g_ui.force_redraw = true;
            }
            else if (ev == EVT_BTN_LONG)
            {
                UI_RequestMenu();
            }
            break;

        case UI_MENU:        handle_menu_event(ev);        break;
        case UI_PAGE_MODE:   handle_page_mode_event(ev);   break;
        case UI_PAGE_DMX:    handle_page_dmx_event(ev);    break;
        case UI_PAGE_NET:    handle_page_net_event(ev);    break;
        case UI_PAGE_ANET:   handle_page_anet_event(ev);   break;
        case UI_PAGE_SACN:   handle_page_sacn_event(ev);   break;
        case UI_PAGE_SYS:    handle_page_sys_event(ev);    break;
        case UI_PAGE_UNLOCK: handle_page_unlock_event(ev); break;
        case UI_PAGE_MANUAL_CONFIRM: handle_page_manual_confirm_event(ev); break;
        case UI_PAGE_MANUAL: handle_page_manual_event(ev); break;
        default: break;
    }

    prev = g_ui.state;
}

void UI_OnNetRuntime(bool link_up, uint32_t ip_runtime_be)
{
    g_link_up = link_up;
    g_ip_runtime_be = ip_runtime_be;
}

void UI_OnModeRuntime(uint8_t mode)
{
    dmx_mode = mode;
}

/* Sola interrogazione: nessun cambiamento alla navigazione o ai parametri. */
bool UI_ConfigActive(void)
{
    return g_ui.state != UI_HOME;
}
