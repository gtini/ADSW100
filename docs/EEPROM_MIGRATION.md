# EEPROM: compatibilità e migrazione Videlio

Data: 14 novembre 2026. Stato: prima fase funzionale implementata sulla baseline 480 MHz `0b5fa6c`, collaudata dall’utente. Migrazione e menu verificati con test software; collaudo fisico della nuova fase ancora da eseguire.

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

## Limiti della baseline prima di questa fase

- Nessun CRC, lunghezza, contatore generazione o copia di recovery.
- Il magic non garantisce integrità e non distingue le versioni 1.4.0/1.4.1.
- EE_ReadU8/U16/U32 nascondono errori I/O; EE_SaveConfig e EE_InitMemory ignorano gli esiti e restituiscono successo.
- EE_LoadConfig può riscrivere OFF Time e ordine protocolli se fuori range: vietare tale percorso prima dello snapshot legacy.
- Configurazione salvata campo per campo: power-cut può produrre una combinazione di vecchi e nuovi valori con magic valido.
- Il limite EE_SIZE consente indirizzi riservati EUI; va separato limite di lettura da limite di scrittura.
- NFC_Service al boot può salvare prima della migrazione: va coordinato con la nuova sequenza di avvio.

## Struttura 2 implementata: soltanto i tre campi autorizzati

Sono utilizzabili dall'applicazione 0x00..0x7F. Magic, indirizzi e codifiche dei parametri 0x03..0x24 restano identici. Gap 0x1B preservato. Nessun CRC, doppio slot, seriale, stringa o struttura persistente aggiuntiva.

| Offset | Campo | Codifica corrente |
|---|---|---|
| 0x02 | Versione struttura, byte già riservato | 0x02 completa; 0x00 scrittura metadati pendente |
| 0x25 | Brand con discriminante incorporato | 0xD0 AXN System/NODIM01; 0xD1 Videlio HMS/ADSW100; 0xD2 Generic/NODIM01 |
| 0x26 | Richiesta codice con discriminante incorporato | 0xA4 disabilitata, 0xA5 abilitata |

Il brand in RAM/API resta 0/1/2, la policy 0/1. I bit alti dei byte persistenti distinguono i campi nuovi da memoria legacy indefinita; non sono parametri o copie ulteriori. Il riconoscimento richiede insieme magic, parametri legacy validi, versione e campi marcati coerenti. Il solo byte 0x02 non classifica una EEPROM: i test provano tutte le sue 256 combinazioni legacy con area aggiunta non inizializzata.

## Policy delle unità legacy e default di fabbrica

La copia `/Users/gianlucatini/micro/Studio Due/ADSW100_VID_NO_CODE/Core/Src/user_interface.c` usa effettivamente UI_ENABLE_UNLOCK_CODE=0, ma **l'utente ha successivamente confermato che i dispositivi legacy installati richiedevano il codice**. Tale conferma prevale sulla precedente deduzione dalla copia NO_CODE. Non esiste un campo legacy che consenta di inferire questa variante dal dump.

La migrazione valida imposta pertanto **Brand1 Videlio e menu_code_required=1**, senza cambiare gli altri parametri. Nessuna riscrittura automatica di una policy già salvata in una struttura 2 valida.

| Brand | Default quando selezionato via CDC | Override CDC |
|---|---:|---|
| 0 AXN System | 0 | 0 oppure 1 nei primi 30 s |
| 1 Videlio HMS | 1 | 0 oppure 1 nei primi 30 s |
| 2 Generic | 0 | Sempre 0; richiesta 1 rifiutata |

La nuova inizializzazione esplicita crea AXN/codice OFF. Il comando `brand=` applica il default della tabella anche se seleziona lo stesso ID; `menu_code=` permette l'override ammesso. Le modifiche diventano attive soltanto al riavvio, come RSH7.

## Servizio CDC e protezione dal pannello

- `brand?` restituisce `brand=<id>` persistito; `menu_code?` restituisce `menu_code=<0|1>` persistito. Query disponibili anche dopo 30 s, senza migrazioni o scritture implicite.
- `brand=<0..2>` e `menu_code=<0|1>` accettati soltanto quando elaborati prima di HAL tick 30000. Successo `OK REBOOT`, sintassi/range/I²C non validi `ERROR`, scrittura fuori finestra `LOCKED`. CR, LF o CRLF; decimali senza segno/suffissi come RSH7. Inviare un comando per volta attendendo risposta.
- Il conteggio parte dal tick iniziale HAL del boot, non da apertura porta, enumerazione o primo comando. Anche un normale reset MCU inizia un nuovo boot, come nel riferimento RSH7; una riconnessione USB no. La chiusura latched impedisce riapertura al wrap del tick. Un comando completato o elaborato dopo la deadline è rifiutato; una transazione I²C iniziata prima può terminare poco dopo.
- `EE_SaveIdentity` salva soltanto su EEPROM, senza cambiare l'identità RAM attiva; query e comandi successivi leggono i metadati persistiti per non perdere il primo aggiornamento. Non ricarica DMX/mode/rete e non interrompe i protocolli.
- ISR CDC raccoglie soltanto byte; parsing e I²C avvengono nel main tramite USB_ServiceTask. Risposte persistenti, retry BUSY e priorità sui log; overflow/control byte invalidano l'intera riga fino a CR/LF.
- System Setup ha soltanto i quattro campi originali. Nessuna voce Brand/Menu code. Il click lungo HOME usa la policy attiva per accesso diretto o confronto del codice esistente.
- `EE_ResetUserMemory` e `HardFactoryReset(true)` usati dal pannello preservano versione/brand/policy persistiti. L'inizializzazione nuova `EE_InitMemory` resta nel percorso FGM CDC preesistente su EEPROM non inizializzata; non è disponibile per riprogrammare identità esistente dalla UI.

Riferimenti verificati: RSH7 usb_debug.c usa `brand?`, `brand=`, deadline assoluta 30000 e risposta `OK REBOOT` senza cambio dell'identità attiva. URC01 espone comandi diagnostici `hw ...` in hw_api.c e non è stato trovato un setter CDC brand/menu equivalente; per questi parametri è stata quindi scelta la sintassi RSH7. `menu_code` è l'estensione ADSW100 nella medesima forma.

## Sequenza implementata

1. EE_LoadConfig legge 0x00..0x26 in un accesso verificato. Valida magic e gli stessi range della stabilizzazione prima di qualunque scrittura; errori I²C, magic invalido o valori non riconosciuti non causano inizializzazione.
2. Versione 2 e coppia marcata valida: carica senza scrivere. Brand/policy sono pubblicati in RAM insieme agli altri parametri soltanto al successo.
3. Coppia marcata e versione pendente 0: verifica range, completa soltanto il byte versione e poi pubblica. Coppia marcata con versione futura sconosciuta oppure brand 3 è rifiutata senza scritture.
4. Configurazione legacy riconosciuta senza una tupla nuova valida: prepara Brand1/codice ON. EE_StoreIdentity scrive versione pendente, i due campi e infine versione 2. Nella transizione verso Generic scrive prima codice OFF e poi brand; negli altri casi scrive prima brand. In questo modo nessuna coppia pendente prodotta dal firmware è Generic/ON. Ogni write ha ACK polling/read-back. Nessun parametro legacy, gap o altro byte libero viene riscritto durante la migrazione.
5. Se la migrazione si interrompe prima della coppia completa, il successivo load riconosce ancora il legacy e ripete solo i metadati; dopo la coppia completa riprende il commit. Un errore non pubblica una configurazione RAM parziale.
6. EE_SaveIdentity valida i candidati, rilegge la configurazione valida e aggiorna soltanto i metadati per il prossimo boot. Un save identico non scrive. I normali save di configurazione preservano identità/versione e continuano a scrivere solo i parametri legacy modificati.
7. EE_InitMemory resta esplicita: invalida magic, scrive default e metadati AXN/codice OFF, commette magic per ultimo. Il reset utente usa invece EE_ResetUserMemory e conserva i metadati. Non è chiamata automaticamente dal load; un'interruzione del reset lascia una configurazione non inizializzata invece di spacciarla per valida.

## Limiti espliciti

- Senza CRC non si rileva ogni corruzione che produca un altro valore lecito. Byte legacy liberi arbitrari che coincidessero esattamente con una tupla nuova valida sono indistinguibili da un vero formato 2; non presentare i discriminanti come autenticazione o prova matematica di provenienza.
- Una tupla nuova parzialmente incoerente con versione 2 e almeno un campo marcato è rifiutata, non normalizzata a un altro brand. Nessuna conversione automatica di versioni future riconosciute.
- Fault injection simula errori restituiti da I²C e interruzioni tra operazioni, non tutte le possibili alterazioni analogiche di un byte durante power-cut. Una versione strappata/sconosciuta con campi marcati viene conservata e segnalata, non forzata a default.
- Una modifica esplicita di brand e policy non è una transazione atomica a due campi sotto power-cut: in stato pendente si recupera la coppia marcata presente. Le due impostazioni sono indipendenti; il payload legacy rimane intatto. Lo stesso limite multi-campo della configurazione legacy resta valido.
- I fallback OFF Time/ordine protocolli storicamente assenti restano solo RAM; la migrazione conserva anche i relativi byte originali. Non converte IPv4, DNS o altri parametri.
- Niente rilettura o ricostruzione di identità da NFC: il formato NFC resta quello della baseline.

## Verifiche eseguite e collaudo residuo

- [x] Build ARM completa con GCC CubeIDE, 149 unità C/ASM, zero errori/warning.
- [x] Nuova EEPROM non inizializzata non provoca write nel load; init esplicito AXN/codice OFF.
- [x] Tutti i 256 valori del vecchio byte versione migrano se magic/layout sono validi e metadati assenti.
- [x] Byte per byte preservati 0x00..0x7F eccetto i soli tre offset autorizzati; EUI non scritta.
- [x] Conservati DMX 407, Auto, entrambi universi, priorità, OFF Time e dati rete non predefiniti.
- [x] Migrazione ripetuta non scrive; errori in ogni operazione I/O e nei read-back recuperati al load successivo senza perdita payload; provate anche tutte le transizioni tra le cinque coppie brand/policy ammesse.
- [x] Tre brand e policy ammesse salvati/riletti; Generic/1 rifiutato; brand 3/255, policy 2 e versione futura riconosciuta rifiutati.
- [x] Accesso menu diretto o con codice corretto/errato, assenza edit UI e reset utente senza modifica identità.
- [x] Servizio CDC: default per brand, override ammessi, errori, scadenza 29999/30000 ms, frammentazione/overflow, BUSY, reconnect e wrap.
- [ ] Dump reali delle unità installate e power-cut fisico nelle fasi di migrazione/reset.
- [ ] Confermare BOM/page size, pannello e rendering dei tre brand sul display reale.

Test riproducibili: `python3 tests/test_stabilization.py`; build: `python3 tests/test_stabilization.py --arm-build /tmp/adsw100-branding`. Nessun commit/tag automatico.
