# ADSW100 — istruzioni di sviluppo

## Ambito e autorizzazione

- È autorizzato il porting a 480 MHz e la stabilizzazione richiesti dall'utente. Prima rigenerare affidabilmente con CubeMX; se non possibile, fermarsi e lasciare la rigenerazione all'utente. Non sostituire manualmente il codice generato. Nessuna nuova funzionalità v2 in questa fase.
- Non effettuare commit automaticamente e non modificare i firmware di riferimento.
- Conservare il commit congelato `7cbf16e` (Initial commit v1.4.0): niente riscrittura della storia, cancellazione o sostituzione. Non creare il tag v2.0.0 prima della validazione finale.
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
- EEPROM: soltanto 128 byte applicativi (0x00..0x7F), come confermato dall'utente. Preservare mappa legacy e area EUI/protetta. Nessun nuovo campo, doppio slot, conversione o inizializzazione automatica in questa fase; correggere soltanto errori I/O e protezione delle scritture quando si riprende lo sviluppo.
- Migrazione branding rinviata alla fase successiva. Le precedenti proposte a doppio slot sono superate dalla decisione di preservare la mappa nei soli 128 byte applicativi. Nessun riordino o reinterpretazione silenziosa dei campi legacy.
- L'azzeramento dei livelli alla perdita DMX è intenzionale: preservare spegnimento relè con OFF Time. In Auto verificare anche il subentro delle altre sorgenti disponibili.
- Non inventare Manufacturer ID, OEM Art-Net, PID o ID Uploader. Consultare registri, INI, sorgenti e assegnazioni successive; distinguere UID, Model ID, Product ID e Target ID.

## Verifiche

- Per cambiamenti funzionali approvati: compilare dopo ogni incremento significativo ed eseguire controlli proporzionati; firmware critico richiede misure hardware prima del rilascio.
- Verificare perdita sorgente, transizioni manuale/Auto, durata impulsi, power-cut EEPROM, pacchetti malformati, errori I/O e recovery aggiornamento.
- Non dichiarare collaudato ciò che è stato soltanto ispezionato staticamente.

## Riferimenti

Regole adattate da `/Users/gianlucatini/Dropbox/Firmware_Git/RDM_SPLITTER_H7/AGENTS.md` e `/Users/gianlucatini/Dropbox/Firmware_Git/URC01/AGENTS.md`, oltre al riferimento RSH7 `origin/Uploading-firmware-da-DMX:AGENTS.md`. Le specifiche URC01 per prototipo senza migrazione e per la sua EEPROM non si applicano automaticamente ad ADSW100 commercializzato.
