# ADSW100 — istruzioni di sviluppo

## Ambito e autorizzazione

- Fase autorizzata corrente: RDM responder, porta DMX IN / ArtNet Node / sACN Node, menu DMX e configurazione condivisa. Branding e migrazione precedenti validati dall’utente nel commit `1e71467`; baseline 480 MHz in `0b5fa6c`. Conservare parser/arbitraggio e temporizzazione relè. Web, bootloader, controller RDM e nuove funzioni NFC restano esclusi.
- Non effettuare commit automaticamente e non modificare i firmware di riferimento.
- Conservare i commit congelati `1e71467`, `0b5fa6c` e `7cbf16e` (Initial commit v1.4.0): niente riscrittura della storia, cancellazione o sostituzione. Non creare il tag v2.0.0 prima della validazione finale.
- Eseguire commit incrementali, circoscritti e verificabili quando autorizzati; non includere modifiche estranee dell'utente. Documentare validazione e limiti di ogni incremento.

## Codice e organizzazione

- Commenti applicativi in italiano, stile Allman, nomi e indentazione coerenti. Documentare unità, range, proprietà dei buffer, contesto ISR/main e comportamento bloccante delle API significative.
- Nei file generati STM32CubeMX inserire codice manuale esclusivamente nelle sezioni USER CODE previste. Non spostare i marcatori. Verificare la diff dopo rigenerazione; i cambiamenti generati delle periferiche devono derivare dalla configurazione CubeMX approvata.
- Modifiche minime e locali; niente refactoring, rinominazioni o riformattazione non richiesti. Non riformattare HAL, CMSIS, lwIP o altre librerie vendor.
- Riutilizzare prima i moduli ADSW100 e il codice verificato RSH7, URC01 e DRV_STG4; verificare revisione, pin, MCU, timing e dipendenze prima del porting.
- Evitare proliferazione di file, wrapper, framework e astrazioni. Prima di creare moduli motivarne la necessità e verificarne l'inclusione nella build. Non eliminare automaticamente file piccoli o apparentemente duplicati.
- Aggiornare docs/AUDIT.md, docs/ROADMAP_V2.md e docs/EEPROM_MIGRATION.md quando cambiano implementazioni, decisioni o evidenze. Distinguere confermato, rischio, proposta e verifica hardware.

## EEPROM e comportamenti da preservare

- Protezione assoluta della configurazione legacy Videlio: nessuna scrittura prima di lettura completa, verifica I/O e validazione; mai inizializzare automaticamente su errore bus o versione sconosciuta.
- EEPROM: soltanto 128 byte applicativi (0x00..0x7F). Mappa legacy invariata. Metadati autorizzati: versione struttura (0x02), brand (0x25), richiesta codice (0x26) e solo nuovo ruolo porta DMX (0x27). Discriminanti incorporati; versione struttura 3. Le strutture precedenti migrano esplicitamente a IN, senza interpretare il byte libero 0x27 né cambiare la modalità sorgenti legacy. Niente doppio slot, CRC o strutture complesse.
- Migrare soltanto configurazioni legacy con magic e parametri validi a Brand1 Videlio, codice abilitato: l'utente conferma che i dispositivi legacy richiedevano il codice. La copia locale NO_CODE non rappresenta la policy delle unità installate. Preservare ogni altro byte; versione commessa per ultima e read-back.
- Brand e policy menu non sono modificabili da UI. Solo CDC di fabbrica nei primi 30000 ms da HAL_Init, con deadline assoluta non riaperta da USB/reconnect/wrap. Usare sintassi RSH7 `brand?`, `brand=<id>`, `menu_code?`, `menu_code=<0|1>`; scritture persistenti applicate al prossimo boot (`OK REBOOT`).
- Default per comando brand/init: AXN codice 0, Videlio codice 1, Generic sempre 0 (rifiutare Generic/1). AXN e Videlio permettono override CDC nella stessa finestra. Il reset da pannello preserva i metadati già salvati, anche se diversi dalla RAM fino al reboot.
- ESTA `0x17FA` AXN Systems per tutti i brand, confermato dall'utente. OEM RSH7 `0x2F3F` è specifico RSH7 e non è assegnato a questo prodotto: non riutilizzarlo. Nomi e modelli appartengono alla tabella unica in cfg_io.c, esposta da defs.h.
- L'azzeramento dei livelli alla perdita DMX è intenzionale: preservare spegnimento relè con OFF Time. In Auto verificare anche il subentro delle altre sorgenti disponibili.
- Non inventare Manufacturer ID, OEM Art-Net o ID Uploader. I nuovi PID proprietari ADSW100 0x9100..0x9108 sono assegnazioni interne di questa implementazione, documentate in AUDIT.md dopo ricerca conflitti nei riferimenti; non presentarle come registrazioni ESTA. Model ID ancora non assegnato (placeholder dichiarato 0xFFFF): ottenere assegnazione dal registro completo AXN prima del rilascio. Consultare registri, INI, sorgenti e assegnazioni successive; distinguere UID, Model ID, Product ID e Target ID.

## Verifiche

- Per cambiamenti funzionali approvati: compilare dopo ogni incremento significativo ed eseguire controlli proporzionati; firmware critico richiede misure hardware prima del rilascio.
- Verificare perdita sorgente, transizioni manuale/Auto, durata impulsi, power-cut EEPROM, pacchetti malformati, errori I/O e recovery aggiornamento.
- Non dichiarare collaudato ciò che è stato soltanto ispezionato staticamente.

## Riferimenti

Regole adattate da `/Users/gianlucatini/Dropbox/Firmware_Git/RDM_SPLITTER_H7/AGENTS.md` e `/Users/gianlucatini/Dropbox/Firmware_Git/URC01/AGENTS.md`, oltre al riferimento RSH7 `origin/Uploading-firmware-da-DMX:AGENTS.md`. Le specifiche URC01 per prototipo senza migrazione e per la sua EEPROM non si applicano automaticamente ad ADSW100 commercializzato.

## Porta RS485 e configurazione comune

- Unico proprietario USART1 in dmx_512.c: PB14 TX, PB15 RX, PD8 direzione. RX DMX/RDM, TXE/TC, TIM7 BREAK/MAB; TIM6 resta riservato ai relè. Non aggiungere driver concorrenti o chiamate HAL TX bloccanti.
- Il modulo rdm.c/h separa decodifica PID/discovery dal driver esistente; non importare controller/router/updater dei riferimenti.
- Pannello DMX e RDM usano CFG_GetValue/CFG_SetValue; il futuro web dovrà usare le stesse API e gli stessi universi persistenti. Brand/policy restano esclusivamente CDC.
- Separare prove host, build ARM e misure fisiche. Il rilevamento RDM Node avviene solo in RX: nessuna garanzia di ascolto simultaneo al proprio TX o di arbitraggio multimasters.
