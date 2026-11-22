# ADSW100 — istruzioni di sviluppo

## Ambito e autorizzazione

- Ambito firmware precedentemente autorizzato: RDM responder, porta DMX IN / ArtNet Node / sACN Node, menu DMX e configurazione condivisa. Branding e migrazione precedenti validati dall’utente nel commit `1e71467`; baseline 480 MHz in `0b5fa6c`. Conservare parser/arbitraggio e temporizzazione relè. Web, bootloader, controller RDM e nuove funzioni NFC restano esclusi.
- Fase autorizzata: completamento SDCP2 nel solo ADSW100. Usare il driver cooperativo H7 e il contratto URC01/DRV_STG4 con revisione manuale e catalogo JSON in ST2FW v3. Conservare metadati 0x28..0x37, Init esplicito, priorità del pannello e dei salvataggi RDM. Il bootloader applicativo e le modifiche ai repository di riferimento restano fuori ambito.

- Non effettuare commit automaticamente e non modificare i firmware di riferimento.
- Conservare i commit congelati `1e71467`, `0b5fa6c` e `7cbf16e` (Initial commit v1.4.0): niente riscrittura della storia, cancellazione o sostituzione. Non creare il tag v2.0.0 prima della validazione finale.
- Eseguire commit incrementali, circoscritti e verificabili quando autorizzati; non includere modifiche estranee dell'utente. Documentare validazione e limiti di ogni incremento.

- Con USB DFU ROM scollegare DMX prima di entrare in DFU. Conservare il margine linker su _estack e la diagnostica di boot. IRQ I2C1 abilitati solo dopo init HAL completo e filtri. Un edit locale esplicito può annullare E8 e riaprire C3 dopo validazione della configurazione.

## Codice e organizzazione

- Commenti applicativi in italiano, stile Allman, nomi e indentazione coerenti. Documentare unità, range, proprietà dei buffer, contesto ISR/main e comportamento bloccante delle API significative.
- Nei file generati STM32CubeMX inserire codice manuale esclusivamente nelle sezioni USER CODE previste. Non spostare i marcatori. Verificare la diff dopo rigenerazione; i cambiamenti generati delle periferiche devono derivare dalla configurazione CubeMX approvata.
- Modifiche minime e locali; niente refactoring, rinominazioni o riformattazione non richiesti. Non riformattare HAL, CMSIS, lwIP o altre librerie vendor.
- Riutilizzare prima i moduli ADSW100 e il codice verificato RSH7, URC01 e DRV_STG4; verificare revisione, pin, MCU, timing e dipendenze prima del porting.
- Evitare proliferazione di file, wrapper, framework e astrazioni. Prima di creare moduli motivarne la necessità e verificarne l'inclusione nella build. Non eliminare automaticamente file piccoli o apparentemente duplicati.
- Aggiornare docs/AUDIT.md, docs/ROADMAP_V2.md e docs/EEPROM_MIGRATION.md quando cambiano implementazioni, decisioni o evidenze. Distinguere confermato, rischio, proposta e verifica hardware.

## EEPROM e comportamenti da preservare

- Protezione assoluta della configurazione legacy Videlio: nessuna scrittura prima di lettura completa, verifica I/O e validazione; mai inizializzare automaticamente su errore bus o versione sconosciuta.
- EEPROM: soltanto 128 byte applicativi (0x00..0x7F). Mappa legacy invariata. Metadati autorizzati: versione struttura (0x02), brand (0x25), richiesta codice (0x26) e ruolo porta DMX (0x27). L’utente ha successivamente autorizzato solo i 16 byte 0x28..0x37 per metadati dinamici NFC (discriminante/versione/stato, revisione, ultima richiesta, CRC richiesta), implementati e provati con fault injection host, da validare a power-cut fisico; non sono spazio per identità e schema statici. Nessun cambiamento della mappa0x00..0x27: modifiche solo su REQUEST valida tramite CFG/salvataggio comune, o Init Memory esplicito già previsto. Discriminanti incorporati; versione struttura 3. Le strutture precedenti migrano esplicitamente a IN, senza interpretare il byte libero 0x27 né cambiare la modalità sorgenti legacy. Niente doppio slot, CRC della configurazione o strutture complesse; il CRC della richiesta NFC autorizzato non protegge l’intera configurazione.
- Migrare soltanto configurazioni legacy con magic e parametri validi a Brand1 Videlio, codice abilitato: l'utente conferma che i dispositivi legacy richiedevano il codice. La copia locale NO_CODE non rappresenta la policy delle unità installate. Preservare ogni altro byte; versione commessa per ultima e read-back.
- Brand e policy menu non sono modificabili da UI. Solo CDC di fabbrica nei primi 30000 ms da HAL_Init, con deadline assoluta non riaperta da USB/reconnect/wrap. Usare sintassi RSH7 `brand?`, `brand=<id>`, `menu_code?`, `menu_code=<0|1>`; scritture persistenti applicate al prossimo boot (`OK REBOOT`).
- Default per comando brand/init: AXN codice 0, Videlio codice 1, Generic sempre 0 (rifiutare Generic/1). AXN e Videlio permettono override CDC nella stessa finestra. Il reset da pannello preserva i metadati già salvati, anche se diversi dalla RAM fino al reboot.
- ESTA `0x17FA` AXN Systems per tutti i brand, confermato dall'utente. OEM RSH7 `0x2F3F` è specifico RSH7 e non è assegnato a questo prodotto: non riutilizzarlo. Nomi e modelli appartengono alla tabella unica in cfg_io.c, esposta da defs.h.
- L'azzeramento dei livelli alla perdita DMX è intenzionale: preservare spegnimento relè con OFF Time. In Auto verificare anche il subentro delle altre sorgenti disponibili.
- Non inventare Manufacturer ID, OEM Art-Net o ID Uploader. I nuovi PID proprietari ADSW100 0x9100..0x9108 sono assegnazioni interne di questa implementazione, documentate in AUDIT.md dopo ricerca conflitti nei riferimenti; non presentarle come registrazioni ESTA. Model ID RDM 96 (0x0060), sotto17FA, e codice uploader/Product ID96 assegnati separatamente dall’utente il15/11/2026 ad ADSW100/NODIM01 per tutti i brand; registrati nell’INI Builder e nel registro. Target/layout boot non ancora assegnati; non inventare profili PIC o chiavi legacy. Consultare registri, INI, sorgenti e assegnazioni successive; distinguere UID, Model ID, Product ID e Target ID.

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

## NFC SDCP2 e Init Memory

- Specifica wire e catalogo in `protocol/nfc/README.md`; firmware e script host devono passare gli stessi vettori. Le etichette e i descrittori vivono nel JSON manuale, la tabella C contiene soltanto il contratto wire.
- EEPROM metadata NF/versione1: stato0x2B A5 commesso, B6 richiesta preparata, C3 modifica locale, D7 Init EEPROM preparato, E8 Init tag autorizzato. Revision/request/CRC richiesta BE32 a2C/30/34. La revisione schema resta in flash.
- Nessun ripristino da JSON al boot. Init Memory utente usa `EE_InitWithNfc`, verifica i default prima di rendere persistente E8; dopo reboot il servizio sostituisce solo NDEF propri riconosciuti o area vuota. Consenso consumato dopo readback tag. Preservare contenuti estranei, UID, indirizzo I2C e lock; CC nuovo solo con consenso Init, probe2k e controlli del riferimento DRV.
- Nessun autosave/inizializzazione per metadati NF corrotti. Stato preparato dopo reboot: finalizzare solo se richiesta e configurazione completa già coincidono; altrimenti richiedere manutenzione, senza riapplicare la richiesta. Non promettere atomicità della EEPROM legacy in place.
- NFC cede I2C al pannello durante la configurazione; EEPROM e RTC rispettano la riserva STOP/READ del tag. Driver IT non bloccante; i commit EEPROM conservano le API sincrone preesistenti. Misurare sul dispositivo la latenza e la convivenza con RDM/rete.
