# EEPROM: compatibilità e migrazione Videlio

## Correzione hook salvataggi locali — 16 novembre 2026

Mappa0x00..0x37 e versioni invariate. L'Init tag pendente E8 non deve dipendere dalla disponibilità NFC per consentire un salvataggio del pannello. Un edit locale con valore cambiato annulla tale consenso, incrementa la revisione e prepara C3 prima di scrivere il parametro. Non inizializza il tag e non riapplica default. Un successivo Init Memory esplicito può autorizzarlo nuovamente.

C3 dopo reboot non viene finalizzato automaticamente. Una nuova azione locale verifica l'intera configurazione persistente e incrementa ancora la revisione prima del salvataggio; la pubblicazione NFC resta subordinata alla corrispondenza RAM/EEPROM. B6/D7 o metadati NF riconoscibili ma non validi restano conservativi: nessun recupero ambiguo o inizializzazione automatica. Gli errori I/O si propagano al pannello, senza rimuovere artificialmente l'asterisco.

Il flag diagnostico NFC OFF è solo RAM: nessun nuovo byte EEPROM. Gli hook metadati e i controlli restano attivi anche nella build OFF per non rendere nuovamente applicabile una richiesta obsoleta. Test host: nuovi salvataggi E8/C3 e24scenari di taglio durante invalidazione locale, oltre ai test legacy/REQUEST/Init. Persistenza e power-cut fisico ancora da verificare; procedura A/B in AUDIT.md.


## Verifica correzione I2C — 16 novembre 2026

Correzione limitata al trasporto/diagnostica NFC e agli IRQ I2C1. File eeprom.c/h, mappa0x00..0x37, stati metadata, versione, salvataggi e recovery verificati per hash identico a inizio intervento; nessuna nuova inizializzazione, migrazione o cambio indirizzo EEPROM. NfcBuild/Parse/Process e catalogo/SYNC1 invariati. Le regressioni legacy e power-cut host della fase2 restano PASS; nessuna nuova prova fisica EEPROM dichiarata.

## Fase 2 NFC implementata — 15 novembre 2026

Nessun offset legacy0x00..0x27 o versione struttura3 è stato spostato. Bootstrap NFC scrive solo i16byte autorizzati0x28..0x37; REQUEST valide cambiano esclusivamente i parametri richiesti tramite CFG/EE_SaveConfig. Init Memory esplicito continua a ripristinare i default come previsto, preservando l'identità quando richiesto, e ora accoda anche l'inizializzazione del tag.

| Offset | Formato corrente |
| --- | --- |
| 28..29 | ASCII NF |
| 2A | Versione metadati1, indipendente da schema/firmware |
| 2B | A5 commesso; B6 richiesta preparata; C3 salvataggio locale; D7 Init EEPROM preparato; E8 Init tag autorizzato |
| 2C..2F | Revisione dinamica configurazione BE32, zero invalido, wrap a1 |
| 30..33 | Ultima richiesta associata BE32 |
| 34..37 | CRC32 della richiesta, non CRC della configurazione |

Scrittura metadata: invalida stato, scrive/verifica corpo, sigilla per ultimo. Transizioni di solo stato usano un byte con lettura/readback. Nessun init per NF riconoscibile ma incoerente, I/O fallito o versione ignota. Byte non riconosciuti legacy non si interpretano come richieste; bootstrap solo con configurazione valida e tag vuoto, mai da un JSON/SNAPSHOT obsoleto.

`EE_Write` prepara C3 prima delle modifiche locali esportate, coprendo i vecchi setter diretti UI. Versione/brand/policy non generano revisioni perché non sono parametri NFC. Prima della nuova REQUEST si richiedono runtime e EEPROM coerenti, inclusa identità CDC commessa. Il reset completo verificato può commettere C3 prima del reboot. Il percorso Init utente usa `EE_InitWithNfc`: D7 prima dei default, E8 dopo verifica; il servizio NFC consuma E8 dopo verifica tag, mai applicando i vecchi valori JSON. UID/lock e tag estranei preservati.

B6 dopo reboot: se ID/CRC/base e configurazione completa coincidono, commette solo metadata e ripubblica ACK; altrimenti recovery necessario e nessuna nuova applicazione. C3 interrotto e NF corrotto bloccano la sincronizzazione invece di ripristinare impostazioni obsolete. EEPROM in place non atomica: possibile configurazione parziale preservata, da recuperare esplicitamente. NF corrotto non viene azzerato neppure come effetto collaterale di Init Memory; serve manutenzione verificata.

Host:68 tagli byte REQUEST multiparametro e24 Init,170 scenari I/O, replay/local save, preservazione legacy e mapping raw lwIP/BE verificati. Hardware: verificare brown-out reale, tempi pagina/ACK, riavvio in ogni fase, perdita bus e alimentazioni indipendenti MCU/tag. Nessun collaudo fisico dichiarato.

## NFC: intervallo autorizzato e mapping verificato, 15 novembre 2026

L’utente autorizza 16 byte 0x28..0x37; i percorsi EEPROM esaminati terminano a 0x27. I byte fisici possono essere indefiniti: non interpretarli come configurazione valida o autorizzazione a importare una vecchia richiesta. Nessun codice EEPROM è stato modificato nella fondazione offline.

| Offset proposto | Campo | Dimensione |
| --- | --- | ---: |
| 28..29 | Discriminante NF | 2 |
| 2A | Versione metadati | 1 |
| 2B | Stato di commit/recupero, codici ancora da validare | 1 |
| 2C..2F | Revisione configurazione SYNC | 4 |
| 30..33 | Ultima richiesta associata | 4 |
| 34..37 | CRC32 richiesta, non CRC della configurazione | 4 |

Mappa 00..27 e versione struttura3 invariate; niente slot aggiuntivi. Manufacturer ID, Model ID e Schema Revision manuale appartengono al firmware, non alla EEPROM. Non esiste piu un hash di schema. La sequenza fisica di commit e recupero non è ancora implementata o validata; la memoria in place non diventa atomica grazie ai soli 16 byte.

Il modello host [protocol/nfc](../protocol/nfc/README.md) verifica che EEPROM salva BE il numero raw lwIP little-endian: 192.168.1.20 corrisponde ai byte legacy 14 01 A8 C0, mentre NFC trasmette C0 A8 01 14. DHCP attivo conserva IP/mask salvati; DNS usa indici0/1, route canonica SET ID0. Test per tutti i prefissi0..32, rifiuto di mask non contigua e conservazione byte00..02/18..7F. È una prova host, non un nuovo writer hardware né una migrazione eseguita.

Data: 14 novembre 2026. Stato: baseline 480 MHz `0b5fa6c` e branding/migrazione struttura2 `1e71467` collaudati dall’utente. Nuovo incremento struttura3/ruolo DMX verificato con build e test host; collaudo fisico ancora da eseguire.

## Stato corrente: struttura 3 per il ruolo DMX

Le sezioni successive conservano l’analisi legacy e la precedente implementazione della struttura 2. Quella fase è stata collaudata positivamente dall’utente (`1e71467`). L’incremento RDM/Node porta la versione struttura a **3**, senza cambiare i 128 byte disponibili, il magic o il significato di alcun parametro legacy.

| Offset | Struttura 2 | Struttura 3 |
| --- | --- | --- |
| 0x00..01 | Magic A5 5A | Identico |
| 0x02 | Versione 02, pendente 00 | Versione 03, nuovo pendente F3 |
| 0x03..24 | Payload legacy e gap 1B | Identico, stessi offset/formati |
| 0x25 | D0/D1/D2 brand | Identico |
| 0x26 | A4/A5 menu code | Identico |
| 0x27 | Indefinito, non interpretare | B0 IN, B1 ArtNet Node, B2 sACN Node |
| 0x28..7F | Non usato | Preservato |
| Area EUI fisica | Solo lettura | Solo lettura, usata anche per UID RDM |

**Mode legacy a 0x1A rimane 0=DMX, 1=Art-Net, 2=sACN, 3=Auto.** Il ruolo porta 0x27 è un parametro indipendente: IN mantiene l’arbitraggio/relè preesistenti; Node sceglie una delle due sorgenti di rete già configurate per DMX OUT e relè. Non duplicare universi: 0x1C..1D Art-Net e 0x1E..1F sACN restano gli unici campi. Address resta quello dei 12 relè, non un offset del frame DMX OUT (trasmette tutti i 512 slot).

La successiva correzione dei reset Node e degli interrupt TX/RDM non cambia mappa, versione struttura, migrazione o contenuto EEPROM. La nuova query CDC `rdm?` è esclusivamente diagnostica e non effettua accessi I²C né scritture di configurazione. Non cancellare EEPROM per provare questa correzione.

Aggiornamento15 novembre2026: la correzione RDM Node/PID rete non cambia versione3, mappa o migrazione. APPLY e SET gateway/DNS pubblicano ora i parametri comuni solo dopo il TC ACK; save aggregato dopo250 ms senza transazione RX/TX. Nessun nuovo parametro EEPROM. Statico e DHCP usano il solo selettore legacy: per abilitare DHCP da statico, inviare STATIC_ADDRESS=0/prefix0, DHCP_MODE=1, APPLY; la combinazione statica non nulla/DHCP attivo è rifiutata con NACK000B senza perdere i candidati. Verificati round-trip EEPROM di IP/mask/gateway/DNS non predefiniti e DHCP, restano power-cut fisici. Dettagli in AUDIT.md.

Debug durata RDM (15 novembre2026): gestione MUTE/UN_MUTE, snapshot RX e diagnostica CDC non cambiano mappa/versione3 o migrazione EEPROM. Nessun timer di auto-default e nessuna nuova scrittura persistente introdotti. I nuovi contatori e il secondo snapshot sono soltanto RAM.

### Migrazione e recovery attuali

1. EE_LoadConfig legge 0x00..26, verifica esito I²C, magic e payload/range prima di ogni eventuale scrittura. Classificazione attraverso versione **insieme** ai tag brand/policy; nessuna deduzione dal solo byte 02.
2. Struttura 2 con tupla identità valida, oppure precedente stato pendente 00 con tupla completa: preserva brand e policy, scrive **B0 a 27**, verifica read-back, infine versione 03. Il vecchio byte 27 viene ignorato anche se per caso vale B1/B2. Nessun vecchio dispositivo diventa trasmettitore accidentalmente.
3. Legacy valido privo di identità marcata: scrive/verifica ruolo IN, poi migra Brand1/codice ON con marcatore pendente F3 e commit finale 03. Il payload originale resta intatto. Gli stessi criteri legacy preesistenti restano applicati; versione futura con tag coerenti viene rifiutata.
4. Struttura 3 completa: accetta soltanto ruolo B0..B2. B3 o byte non marcato sono errori, senza normalizzazione/default o pubblicazione RAM parziale. Una rilettura dopo migrazione completata non scrive.
5. Nuovi aggiornamenti della sola identità CDC usano F3, così il recovery non li scambia con una migrazione v2 e non azzera un ruolo OUT esistente. Coppia completa con F3: completa versione03, poi verifica il ruolo prima di pubblicare. Tupla parziale incoerente con versione2/3 è rifiutata. Interruzione analogica che corrompe un byte in un valore ignoto resta un errore conservativo, non una ricostruzione garantita.
6. Init esplicito/reset impostano IN. Init nuova imposta AXN/codice OFF; reset utente conserva identità/policy come prima. Magic commesso per ultimo nell’inizializzazione. Il load su EEPROM vergine o bus guasto **non inizializza automaticamente**.
7. Salvataggi ordinari preservano byte 02/25/26 e gap1B; possono aggiornare soltanto il nuovo ruolo e i parametri applicativi esistenti. Brand/policy CDC restano protetti, anche prima del riavvio.

L’ordine delle scritture protegge i parametri legacy durante questa migrazione; non rende atomico il salvataggio di un insieme di parametri. Nessun CRC/doppio slot. Il byte F3 è un discriminante del nuovo protocollo di aggiornamento, non un checksum. Una configurazione senza tag resta soggetta al riconoscimento legacy già validato nella fase precedente.

### Persistenza e prove dell’incremento

Pannello DMX e PID RDM 9100/9102/9103 usano gli stessi parametri e gli stessi controlli CFG_GetValue/CFG_SetValue. Conferma pannello salva con verifica errore; cancel non applica. RDM mantiene i candidati DHCP/IP statico/netmask solo in RAM fino a INTERFACE_APPLY_CONFIGURATION, quindi usa il setter comune. Aggrega i SET applicati e salva dopo 250 ms di quiete, con retry I²C: ACK conferma RAM, non un commit sincrono EEPROM. Label RDM è volatile; nessuna stringa aggiunta alla mappa. NFC legacy invariato e nessuna nuova codifica NFC introdotta.

- [x] Test host migrazione v2→v3, anche con byte27=B2 precedente, preservando ogni altro byte salvo versione e ruolo.
- [x] Fault injection per gli accessi I²C e read-back della migrazione; riavvio/rilettura idempotente.
- [x] Persistenza ruolo sACN Node e modalità sorgenti legacy Auto, ritorno IN; rifiuto B3.
- [x] Regressioni precedenti di identità, Generic/codice, default e 256 valori byte versione legacy.
- [x] Test UI su conferma/cancel ruolo e modifica dei due universi, senza campi duplicati.
- [x] Build ARM 150 unità e dieci gruppi host PASS; comandi in AUDIT.md.
- [ ] Power-cut fisico durante ruolo→versione, CDC pendente F3 e salvataggio aggregato RDM.
- [ ] Dump installati e riavvio hardware con Mode e ciascun universo, mantenendo identità e tutti i parametri Videlio.

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

Il rinvio RDM con USART BUSY (15 novembre 2026) conserva soltanto lo snapshot RAM fino a linea libera o deadline esistente; nessuna variazione della mappa, versione o politica di scrittura EEPROM.

La correzione TIM7 ARR nullo (15 novembre 2026) modifica solo il minimo one-shot da1 a2 µs; nessuna variazione EEPROM, migrazione, default o salvataggio.

Sessione RDM Node autorizzata il15 novembre2026: usa soltanto stati RAM, GET standard di mantenimento URC01 e lease ADSW100 esistente. Nessun nuovo campo, PID persistente, migrazione o modifica ai128 byte applicativi.

Decisione corrente RDM Node30 s (15 novembre2026): cessione e timestamp risiedono soltanto in RAM e si azzerano al boot; attraversano i cambi ruolo senza modificare la mappa EEPROM. Versione, indirizzi, payload legacy e procedure di migrazione restano invariati. Il valore30 s e' una costante del driver, non un nuovo parametro persistente.

Ripristino URC01 pre-Node richiesto dopo conferma hardware positiva ADSW10030 s: nessuna modifica alle configurazioni persistenti di ADSW100 o alle procedure EEPROM; il rollback riguarda solo logica controller/PHY e relativi test URC01, mantenendo IPv4.

Assegnazione ID96 (15 novembre2026): Model ID RDM e codice uploader/Product futuro sono costanti firmware, non campi EEPROM. Nessuna migrazione, scrittura di configurazione o modifica dei seriali UID; struttura, brand e impostazioni legacy preservati.
