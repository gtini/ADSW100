# ADSW100 v2.0.0 — roadmap incrementale

Stato al 14 novembre 2026: base clock/stabilizzazione implementata e verificata con build/test software; nuove funzionalità v2 ancora da sviluppare. Le caselle software chiuse attestano implementazione/verifica statica o host; ogni verifica hardware resta un gate separato. Audit in [AUDIT.md](AUDIT.md), persistenza in [EEPROM_MIGRATION.md](EEPROM_MIGRATION.md). L'utente ha successivamente autorizzato porting 480 MHz e stabilizzazione, senza commit/tag o nuove funzionalità. Rigenerazione CubeMX eseguita dall’utente; arresto superato e stabilizzazione ripresa.

## Fase 0 — baseline e decisioni

- [ ] Approvare piano e perimetro incrementale; conservare commit 7cbf16e e non creare tag finale.
- [ ] Registrare revisioni esatte dei riferimenti scelti; leggere il boot RSH7 dal riferimento Git che lo contiene, senza cambiare i checkout di riferimento.
- [ ] Build riproducibile della baseline e raccolta map, warning, stack/heap/flash/D2.
- [ ] Confermare BOM EEPROM/NFC, revisione STM32, schemi DMX DE/RE, watchdog/enable relè e specifica grafica DIN.
- [ ] Acquisire dump Videlio e approvare layout/migrazione prima di qualsiasi scrittura nuova.

Verifica: baseline compilabile e comportamenti attuali documentati. Rischio: copie locali non equivalenti a firmware installati.

## Fase 1 — relè, DMX e base tempi

- [x] Correggere servizio impulsi sospeso in manuale e relè esclusi dal limite canale 512.
- [x] Unificare comando manuale/automatico nel driver esistente senza HAL_Delay per impulsi.
- [x] Deadline monotone, impulso 40 ms e guardia 30 ms; completamento anche durante cambio modo/indirizzo.
- [x] Servizio impulsi spostato in ISR TIM6; test host su cambio modo/indirizzo e scadenze.
- [ ] Misurare bobine e RL_ENABLE con main bloccato e carico massimo.
- [x] Snapshot DMX completo con conteggio, timestamp e gestione frame corti/NE/ORE.
- [x] Preservare perdita DMX → livelli zero → OFF Time; verificare subentro sorgenti in Auto.
- [x] Definire DMX Address vicino al limite: footprint 12 e compatibilità indirizzi legacy, senza clamp persistente silenzioso.

Verifica: oscilloscopio bobine/enable, manuale durante impulso, inversioni rapide, centralina spenta, frame corti e carico rete/USB. Rischio: deadline nel solo main non limita impulso durante blocchi.

## Fase 2 — EEPROM e configurazione coerente

- [x] Implementare errori I/O espliciti e load senza scritture implicite.
- [x] Preservare la mappa legacy entro 128 byte; nessun nuovo campo, doppio slot o migrazione automatica. Migrazione Brand1 rinviata.
- [ ] Proteggere EUI e configurazioni esistenti; verificare errori I²C e limiti power-cut del formato legacy.
- [x] Validazione comune del candidato integrata in cfg_io/eeprom esistenti, con apply atomico e save controllato.
- [x] Correggere conversioni IP senza alterare byte/significato delle configurazioni installate.
- [x] Gestire DNS runtime, apply rete e fallimenti senza falsi successi.

Verifica: nessuna variazione impostazioni Videlio e nessuna scrittura su dati ambigui o errore I²C. Rischio principale: identificazione chip/area libera e byte order IP.

## Fase 3 — Art-Net, sACN e Auto

- [x] Rifiutare lunghezze/header/range illegali prima di copiare o pubblicare dati.
- [x] Art-Net: correggere PollReply, nomi/versione, indirizzi e capability reali.
- [x] Art-Net: sequence per sorgente, gestione due sorgenti/merge e timeout coerenti; delimitare supporto ArtSync.
- [x] sACN: correggere offset sequence, CID, priorità, soglia, pari priorità, timeout e stream termination.
- [x] sACN: preview, opzioni sync, PDU/DMP e start code; dichiarare esplicitamente ciò che non è supportato.
- [x] Limitare/riciclare database universi senza saturazione permanente.
- [x] Rendere coerenti join/leave, filtro multicast e recovery DHCP/link; gestire errori allocazione listener.
- [ ] Verificare Auto per tutte le sei priorità e transizioni tra protocolli, senza campioni residui indesiderati.

Verifica: pacchetti noti/malformati, riordino/wrap/duplicati, due sorgenti, cambio universo, link flap e packet capture. Rischio: trattare il semplice last-packet-wins come merge conforme.

## Fase 4 — clock STM32 a 480 MHz

- [x] Adattare IOC e generazione da ADSW100_AXN: VOS0, PLL1, bus e latenza flash.
- [x] TIM6 PSC239/ARR999 con clock timer 240 MHz; verificare frequenza reale.
- [x] Configurazione generata I²C TIMINGR, UART DMX, SPI LCD e USB HSI48 controllata staticamente; RDM resta futuro.
- [ ] Misurare su hardware UART, I²C, SPI, USB, RMII/MDIO e timeout.
- [ ] Verificare revisione silicio, VDD, consumi/temperatura e watchdog.
- [x] Controllare diff generata e preservare codice manuale nelle sezioni USER CODE.

Verifica: USB enumerazione/trasferimenti, I²C, DMX e rete sotto carico; confronto timing con baseline. Rischio: PLL1Q AXN a 64 MHz incompatibile con USB se selettore non aggiornato.

## Fase 5 — branding e identificativi

- [ ] Tabella unica: Brand0 AXN System/NODIM01; Brand1 Videlio HMS/ADSW100; Brand2 Generic/NODIM01.
- [ ] Testi pannello, web, RDM e Art-Net coerenti con tabella e limiti stringhe.
- [ ] Preferenza richiesta codice menu abilitabile/disabilitabile; preservare comportamento legacy verificato.
- [ ] Verificare Manufacturer ESTA e OEM Art-Net realmente assegnati per ogni brand; nessun ID inventato.
- [ ] Censire INI e ID successivi: 94 DRV_STG4 e 95 URC01 risultano occupati nell'uploader; 96 resta candidato, non assegnazione.
- [ ] Distinguere ID uploader legacy, Product/Target ST2FW, Model ID RDM e UID; registrare decisioni approvate.

Verifica: tutte le tre identità e fallback su brand invalido senza convertire automaticamente EEPROM corrotte. Rischio: copiare numerazione brand diversa di RSH7.

## Fase 6 — RDM e NFC

- [ ] Portare responder e validazione/discovery dai riferimenti, mantenendo il driver DMX ADSW100 ove possibile.
- [ ] Confermare hardware bidirezionale DE/RE, timing risposte e assenza collisioni UID nel dominio manufacturer.
- [ ] Matrice PID: DMX Address standard 0x00F0 (GET/SET, 2 byte); verificare standard/assegnazioni per tutti gli altri parametri.
- [ ] Definire formati, range, GET/SET, PDL, NACK e descrizioni per IP, Netmask, Gateway, DNS, Auto/DMX/Art-Net/sACN, Priority, Display Auto OFF, OFF Time in secondi.
- [ ] Distinguere Priority sACN dall'ordine dei protocolli Auto ed esporre entrambi dove previsto.
- [ ] Non riutilizzare PID DRV_STG4 0x8200/01 o RSH7 0x8001/02/03 con significati diversi senza verifica formale.
- [ ] Identificare l'implementazione NFC DRV_STG4 effettivamente disponibile; nel checkout esaminato non è stata confermata.
- [ ] NFC: schema versionato completo, magic/device binding, range e applicazione solo dopo migrazione valida.
- [ ] Gestire conflitti RF/I²C e sincronizzazione modifiche pannello/RDM/NFC/web senza sovrascritture da dati NFC obsoleti.

Verifica: round-trip di ogni campo su tutti i canali, permessi e persistenza; discovery RDM e timing su hardware. Rischio: portare stack NCI URC01 su NTAG come se fosse lo stesso hardware.

## Fase 7 — web server e grafica

- [ ] Riutilizzare HTTP raw/asset e impostazione RSH7, senza framework nuovo.
- [ ] Home con brand/modello dinamico, rete, sorgente, diagnostica e stato 12 relè.
- [ ] SVG modulo DIN 12 uscite, connettori e indicatori da specifiche/foto/disegni verificati, riusando elementi RSH7.
- [ ] Configuration: DMX, rete completa DNS inclusi, Art-Net, sACN, Auto/priorità, display e OFF Time.
- [ ] Validazione comune, gestione errori e applicazione configurazione senza divergenze dal pannello.
- [ ] Quantificare flash asset, heap/pbuf e latenza servizio relè con HTTP concorrente.

Verifica: pagine/render SVG e configurazione reale su dispositivo, limiti stringhe/richieste e memoria. Rischio: geometria/connettori non ancora specificati abbastanza per dichiarare grafica fedele.

## Fase 8 — bootloader e firmware update

- [ ] Riutilizzare boot H743 RSH7, verificando revisione e dipendenze build/linker/finalizzatore.
- [ ] Definire layout ADSW100, descriptor, commit, CRC, vettori e protezione area boot; layout RSH7 è solo riferimento.
- [ ] USB CDC: protocollo, identity, richiesta boot, trasferimento, validazione e recovery.
- [ ] DMX tramite URC01: scegliere protocollo compatibile verificato e distinguere ST2 legacy da ST2FWv2.
- [ ] Assegnare ID realmente libero dopo censimento e aggiornarne i registri/INI autorizzati; nessuna assegnazione in questa fase.
- [ ] Garantire relè/bobine sicuri durante boot/flash e recupero dopo power-cut o immagine errata.
- [ ] Predisporre punti di estensione Ethernet senza implementare aggiornamento Ethernet.
- [ ] Test immagine altro prodotto/target, CRC errato, trasferimento interrotto e downgrade secondo politica approvata.

Verifica: flash/recovery USB e DMX da URC01 su hardware, senza cancellare boot valido né EEPROM. Rischio: firmware e linker attuali occupano la flash dalla base, il solo comando USB non crea un bootloader.

## Fase 9 — validazione e rilascio

- [ ] Build pulita, warning riesaminati, map e budget misurati.
- [ ] Regressione completa Videlio, migrazione/power-cut e impostazioni su tutti i canali.
- [ ] Soak test DMX/Art-Net/sACN/HTTP/USB/NFC concorrenti, watchdog e link instabile.
- [ ] Misure impulsi/guardia/OFF Time e gestione manuale sotto massimo carico.
- [ ] Documentazione aggiornata con decisioni effettive, limiti e risultati, senza presentare test pianificati come superati.
- [ ] Commit incrementali verificabili e revisione finale approvata.
- [ ] Solo dopo validazione finale: versione/tag v2.0.0 e artefatti riproducibili.

## Incremento corrente — porting prima della stabilizzazione

- [x] Verificare percorso effettivo ADSW100_AXN e confrontare IOC.
- [x] Sostituire ADSW100.ioc con quello AXN, mantenendo nome progetto e file ADSW100.
- [x] Documentare 128 byte EEPROM e ritiro proposta doppio slot nella fase corrente.
- [x] Rigenerazione CubeMX eseguita dall’utente; nessuna riscrittura manuale del clock generato.
- [x] USER CODE e MX_RTC_Init preservati; MSP/LAN9303 custom ripristinati nelle sezioni previste.
- [x] Build diretta ARM GCC: 149 unità, zero errori/warning; timing verificato staticamente.
- [x] Sei gruppi test host PASS: Art-Net, sACN, relè, configurazione, DMX, EEPROM.
- [ ] Collaudo fisico 480 MHz e regressione Videlio.
- [x] Correzioni relè/DMX/protocolli/EEPROM/NFC/USB implementate dopo CubeMX; dettaglio e limiti in AUDIT.md.

Codice generato effettivamente a 480 MHz e correzioni applicative presenti. Verifiche software superate; nessuna certificazione hardware o rilascio definitivo. Nessun commit o tag creato.
