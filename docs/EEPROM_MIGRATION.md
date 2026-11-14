# EEPROM: compatibilità e migrazione Videlio

Data: 14 novembre 2026. Stato: analisi e proposta, nessuna migrazione implementata. Le assegnazioni della futura struttura sono provvisorie fino alla verifica del componente e dei dump reali.

## Evidenze v1.4.0 e Videlio v1.4.1

Base: ADSW100 commit 7cbf16e, Core/Inc/defs.h e Core/Src/eeprom.c. Confronto diretto con `/Users/gianlucatini/micro/Studio Due/ADSW100_VID_NO_CODE`: eeprom.c identico byte per byte; defs.h differisce solo per FW_VERSION_PATCH da 0 a 1. Le mappe e le routine di persistenza sono quindi identiche nelle due copie confrontate. Questo non dimostra che ogni dispositivo installato contenga lo stesso firmware: acquisire dump e identificare versioni distribuite.

EE_SIZE=256, indirizzo I²C 7 bit 0x53, indirizzamento memoria a 8 bit. Codice scrive a pagine da 8 byte. Commenti identificano alternativamente 24AA02E64, 24AA02E48 e 24AA025E48: non sono prova del componente montato né della sua area scrivibile. URC01 usa una politica 0x00..0x7F e pagine 16 per il proprio componente; non copiarla senza controllo della BOM.

## Mappa legacy comune

| Indirizzi | Byte | Campo / formato |
|---|---:|---|
| 0x00..01 | 2 | EE_OFFS_INIT, magic 0xA55A, byte A5 5A |
| 0x02 | 1 | EE_OFFS_VERSION riservato; EE_CFG_VERSION=1 definito ma mai letto/scritto dalle routine |
| 0x03 | 1 | use_dhcp, 0 statico / 1 DHCP |
| 0x04..07 | 4 | IP |
| 0x08..0B | 4 | Netmask |
| 0x0C..0F | 4 | Gateway |
| 0x10..13 | 4 | DNS1 |
| 0x14..17 | 4 | DNS2 |
| 0x18..19 | 2 | DMX Address, 1..512 attuale |
| 0x1A | 1 | Mode: 0 DMX, 1 Art-Net, 2 sACN, 3 Auto |
| 0x1B | 1 | Nessun campo definito nelle copie esaminate; preservare |
| 0x1C..1D | 2 | Art-Net Port-Address 0..32767 |
| 0x1E..1F | 2 | sACN universe 1..63999 |
| 0x20 | 1 | sACN transport, 0 multicast / 1 unicast |
| 0x21 | 1 | sACN minimum priority, 0..200 |
| 0x22 | 1 | lcd_backlight: 0 auto-off, >0 sempre acceso nel servizio attuale |
| 0x23 | 1 | OFF Time, 0..30 secondi |
| 0x24 | 1 | proto_priority, 0..5: DAs, DsA, sDA, sAD, ADs, AsD |
| 0x25..F7 | — | Nessun campo applicativo definito qui; non significa area fisicamente scrivibile |
| 0xF8..FF | 8 | Area EUI64 riservata secondo defs.h |
| 0xFA..FF | 6 | Lettura preferita EUI48 secondo eeprom.c; sovrapposta all'intervallo precedente |

U16/U32 sono serializzati dagli helper byte più significativo per primo. Per IP si serializza il valore uint32 utilizzato dal programma: NON convertire automaticamente i dump come quattro ottetti testuali. lwIP usa ordine di rete in memoria, mentre cfg_io.c usa shift numerici: incongruenza da ricostruire su dump/IP visibile. La migrazione deve inizialmente preservare byte e significato effettivo della variante, evitando una doppia inversione.

Default EE_InitMemory: DHCP=1; IP/mask/gateway/DNS=0; DMX address=1; mode=2 sACN; Art-Net=0; sACN=1 multicast; priorità sACN=0; backlight=0; OFF Time=30; ordine protocolli=0. Non applicare questi default globalmente durante migrazione di una configurazione valida.

## Limiti attuali

- Nessun CRC, lunghezza, contatore generazione o copia di recovery.
- Il magic non garantisce integrità e non distingue le versioni 1.4.0/1.4.1.
- EE_ReadU8/U16/U32 nascondono errori I/O; EE_SaveConfig e EE_InitMemory ignorano gli esiti e restituiscono successo.
- EE_LoadConfig può riscrivere OFF Time e ordine protocolli se fuori range: vietare tale percorso prima dello snapshot legacy.
- Configurazione salvata campo per campo: power-cut può produrre una combinazione di vecchi e nuovi valori con magic valido.
- Il limite EE_SIZE consente indirizzi riservati EUI; va separato limite di lettura da limite di scrittura.
- NFC_Service al boot può salvare prima della migrazione: va coordinato con la nuova sequenza di avvio.

## Decisione corrente: mappa legacy e 128 byte

La richiesta di porting/stabilizzazione successiva all'audit conferma **128 byte realmente utilizzabili dall'applicazione, 0x00..0x7F**. Restano invariati indirizzi, codifica e parametri delle versioni 1.4.0 e Videlio 1.4.1. Non aggiungere campi, doppio slot, CRC persistenti o strutture nuove in questa fase. Le proposte iniziali di formato v2 a due slot e relativo commit sono ritirate per questa fase e non costituiscono una specifica implementativa.

La migrazione branding è rinviata: nessuna conversione o inizializzazione automatica delle configurazioni esistenti. Il firmware installato deve continuare a leggere la mappa legacy; non usare il byte versione oggi indefinito per innescare conversioni.

Correzioni autorizzate per la ripresa dello sviluppo:

1. Propagare gli errori HAL nelle letture/scritture e nei load/save, senza dichiarare successo dopo un errore I²C.
2. Leggere e validare un candidato completo prima di sostituire la configurazione RAM; errore bus non equivale a EEPROM vuota.
3. Proteggere tutte le scritture applicative entro 0x00..0x7F con controllo di lunghezza senza overflow; consentire letture EUI separate. Non modificare indirizzi esistenti.
4. Eliminare riparazioni persistenti implicite dal load; mantenere originale su valori dubbi. Nessuna scrittura automatica guidata da versioni mancanti o sconosciute.
5. Coordinare NFC affinché dati non validi non vengano applicati o salvati. Confermare l'interpretazione degli IP prima di intervenire sulle conversioni.
6. Mantenere factory reset soltanto esplicito, con errori verificabili e senza scrivere l'area EUI.

La sola correzione degli errori I/O non rende atomico un salvataggio multi-campo: il rischio di configurazione parziale dopo power-cut resta aperto e va documentato. Non introdurre una soluzione persistente diversa da quella autorizzata per eliminarlo. Per la futura migrazione Brand1 occorrerà un nuovo piano entro i vincoli di memoria, con snapshot dell'originale e validazione prima di qualsiasi scrittura; nessuna assegnazione di indirizzi v2 è approvata.

Stato dopo rigenerazione: correzioni EEPROM implementate in eeprom.c/.h. Lettura snapshot in un solo accesso prima della pubblicazione RAM; esiti HAL propagati; scritture protette entro 0x00..0x7F e verificate con read-back; helper di scrittura restituiscono stato. Il save preserva byte versione/gap e scrive soltanto i byte cambiati; non aggiunge campi. Load senza riparazioni persistenti: OFF Time/ordine protocolli storicamente assenti usano fallback solo RAM. Il pannello segnala errori di save; NFC non dichiara aggiornamento riuscito dopo fallimento. Nessuna migrazione o inizializzazione automatica.

Test host EEPROM PASS: limite 128 byte/EUI, letture fallite senza pubblicazione RAM, scritture fallite senza falso successo, nessuna scrittura durante load, conservazione 0x02/0x1B e round-trip configurazione. Test software non sostituisce power-cut su hardware. Rimangono letture scalar legacy con ritorno zero su errore, usate dalla diagnostica/provisioning: il load configurazione non le usa più. Il fallimento di un save può lasciare EEPROM parziale e RAM già modificata: viene segnalato, non mascherato con un rollback persistente potenzialmente distruttivo.

## Verifiche prima del rilascio

- [ ] BOM, sigla chip, pagine, protezioni, area EUI e capacità su revisioni hardware installate.
- [ ] Dump reali 1.4.0 e Videlio 1.4.1, inclusi NO_CODE, con configurazione pannello/IP nota.
- [ ] Conferma preferenza codice menu della variante Videlio; nessuna deduzione dal nome cartella.
- [ ] Ricostruzione byte order IP/DNS e round-trip senza variazioni di indirizzi.
- [x] Protezione scritture 0x00..0x7F e campi legacy invariati verificati con test host; collaudo fisico separato.
- [ ] Stabilizzazione conserva DMX, mode, entrambi universi, trasporto, priorità, rete completa, display e OFF Time; migrazione rinviata.
- [ ] Verificare reboot reali senza conversioni/scritture; test host load senza scrittura già superato.
- [ ] Errore I²C in ogni fase e power-cut a ogni pagina: documentare i limiti di recupero del formato legacy senza copie.
- [ ] Versione assente/sconosciuta, letture corte e valori fuori range non provocano conversione o inizializzazione.
- [ ] NFC/pannello/RDM/web non salvano prima del riconoscimento/migrazione.
- [ ] Factory reset e downgrade hanno politiche esplicite che preservano identità/EUI e backup legacy.
