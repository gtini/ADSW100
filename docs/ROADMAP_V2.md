# ADSW100 v2.0.0 — roadmap incrementale

Stato al 14 novembre 2026: baseline 480 MHz/stabilizzazione e branding/migrazione collaudati dall’utente (`0b5fa6c`, `1e71467`). Incremento RDM/Node implementato e verificato con build ARM e test host; collaudo fisico ancora da eseguire. Le caselle software non attestano misure hardware. Nessun commit/tag automatico. Audit in [AUDIT.md](AUDIT.md), persistenza in [EEPROM_MIGRATION.md](EEPROM_MIGRATION.md).

## Correzione RDM Node / PID rete — 15 novembre 2026

- [x] Registrare conferma utente: RDM e Node Art-Net/sACN operativi su hardware; restano GET/SET Node e visualizzazione PID rete.
- [x] Confrontare DRV_STG4 (claim/rx_guard/presenza controller), URC01 (PHY/TC/discovery), RSH7 (framing/proxy).
- [x] Verificare porta unica half-duplex e documentare limite due controller sulla stessa linea; nessun taglio arbitrario DMX per rispondere.
- [x] Stati RX separati da TX: protezione da BREAK/SC fino a pacchetto completo e TC risposta, timeout di sicurezza e error recovery.
- [x] Guardie USART BUSY/RXNE/errori, pending→main protetto e nuova ricezione che impedisce vecchie risposte.
- [x] Cambio ruolo senza scartare GET immediatamente successivo al SET; ricontrollo in sezione critica.
- [x] Deadline main1800 µs e primo byte discovery direttamente in TDR dopo DE, come URC01.
- [x] Confermare che tutti i PID IP richiesti e i complementari obbligatori sono già annunciati; individuare assenza degli editor rete nel catalogo URC01, senza modificare il riferimento.
- [x] Correggere PID070A (SET Interface ID0, route assente e NACK point-to-point), verificare PDL/endianness/GET/SET/NACK di tutti i PID.
- [x] Applicare rete solo dopo TC ACK verificato, annullare lavoro se TX abortito; GET immediato coerente, staged fino ad APPLY.
- [x] Documentare limite selettore legacy DHCP/statico e sequenza STATIC=0, DHCP=1, APPLY, CURRENT per abilitare DHCP.
- [x] Dieci gruppi host PASS, incluse72 transazioni UART nei tre modi, cambio ruolo, errori/timeouts, ACK abortito e persistenza rete.
- [x] Build ARM150 unità senza warning/errori e aggiornamento AUDIT/EEPROM_MIGRATION.
- [ ] Collaudo di questo incremento su hardware: GET/SET ripetuti/consecutivi in IN e nei due Node, SET→GET senza attese aggiunte.
- [ ] Misurare DE→primo bit discovery≤4 µs, TC→DE, turnaround176..2000 µs e ripresa Node; stress display/Ethernet/NFC/USB/I²C.
- [ ] Verificare PID rete con controller E1.37-2 o richieste raw; il solo catalogo URC01 corrente non offre gli editor IPv4.
- [ ] Verificare recovery pacchetto interrotto, controller scollegato, contesa di avvio e persistenza reale dopo power-cut.

La gestione di presenza controller usa lo stesso principio DRV_STG4, adattato ad ADSW100: attività RX distinta da frame validato, lease controller2 s e proprietà della singola transazione fino a TC. Una cessione rilevata non rende la porta multi-master. Web, NFC e firmware dei riferimenti restano fuori ambito.

## Incremento corrente — RDM responder e DMX Node

- [x] Confrontare RX/TX URC01/RSH7 e usare prioritariamente DRV_STG4 per direzione/finestre RS485.
- [x] Verificare dal progetto PB14 TX AF4, PB15 RX, PD8 direzione, USART1 250000 8N2; TIM7 libero, TIM6 relè preservato.
- [x] Driver unico RX DMX/RDM e TX interrupt TXE/TC, BREAK 176 µs/MAB 16 µs via TIM7, watchdog TX e rilascio DE al TC.
- [x] Responder discovery/mute/unmute, UID da EUI fisico senza fallback, GET/SET/ACK/NACK, DEVICE_INFO/label/IDENTIFY/address/personality.
- [x] DHCP/IP statico RDM attivati soltanto su INTERFACE_APPLY_CONFIGURATION, con validazione comune e GET configurato distinto da CURRENT_ADDRESS.
- [x] PID standard rete e proprietari 9100..9108 con PARAMETER_DESCRIPTION, range/PDL e nomi centralizzati; matrice in AUDIT.md.
- [x] API comuni CFG_GetValue/CFG_SetValue per pannello DMX, RDM e futuro web; nessuna configurazione universi duplicata.
- [x] DMX IN/ArtNet Node/sACN Node; OUT usa i dati già validati/arbitrati dai parser; nessun nuovo parser.
- [x] Pannello DMX Address/Mode/Universe, conferma e cancel, Universe inattivo in IN; Home mostra ruolo Node.
- [x] Node con finestre RX, ascolto iniziale e sospensione TX su controller rilevato; nessun controller RDM aggiuntivo.
- [x] Blackout dati alla scadenza sorgente, zeri oltre 50 ms senza aggiornamento main; relè coerenti con sorgente Node e OFF Time esistente.
- [x] EEPROM struttura3, solo ruolo a27 marcato; migrazione esplicita a IN con payload/identità preservati e versione commessa per ultima.
- [x] Test host: dieci gruppi PASS, inclusi RX dopo TX, RDM nella finestra Node, IN→Node→IN senza reboot, persistenza/fault I²C, UI e watchdog TX.
- [x] Build ARM 150 unità, zero errori/warning; aggiornare audit e specifica EEPROM.
- [ ] Misurare con oscilloscopio BREAK/MAB/44 µs, TC→DE, refresh/jitter e tempo sezioni critiche, sotto Ethernet/display/NFC/USB/I²C.
- [ ] Test discovery/GET/SET/NACK/IDENTIFY con controller reale nei tre brand; verificare latenza/timeout e save differito, inclusi guasti bus I²C.
- [ ] Verificare DE/RE e ascolto nelle pause Node; accensione controller durante TX, collisioni e sospensione/ripresa. Nessun ascolto simultaneo al proprio TX è fisicamente garantito.
- [ ] Test rete fisica Art-Net/sACN, perdita sorgente, riavvio e entrambi universi; regressione DMX IN/relè e OFF Time.
- [ ] Power-cut reali della migrazione v2→v3 e dei SET RDM aggregati; acquisire dump Videlio.
- [ ] Censire UID sotto manufacturer17FA e assegnare Model ID (placeholder FFFF) e registrare PID9100..9108 nel registro completo AXN prima del rilascio; OEM ancora non assegnato.
- [ ] Valutare con misure se serva salvataggio I²C a piccoli passi per non perdere richieste RDM: il nuovo TX è non bloccante, il save main resta sincrono.

Gate: questo incremento non implementa web, bootloader/updater DMX/USB o nuovi campi NFC. L’API trasmissione prepara il futuro riuso hardware dell’updater, senza affermare compatibilità del protocollo già realizzata. Label RDM volatile e semantica di apply rete corrente documentate nell’audit.

## Correzione dopo primo test RDM/Node

- [x] Analizzare segnalazione utente: DMX IN OK, URC01 senza discovery, reboot periodico in ArtNet Node.
- [x] Verificare dal log EUI-48 FC:0F:E7:16:45:70 e UID atteso 17FA:E7164570; OUI già ammesso, nessuna modifica dell’identità.
- [x] Correggere riarmo TIM7: UIE mascherato, URS=1, cancellazione SR/pending NVIC, guardia UIF/UIE e cancellazione watchdog TX al TC.
- [x] Cancellare errori UART FE/NE/ORE anche durante TX per evitare interrupt ripetuti senza avanzamento.
- [x] Aggiungere query CDC rdm? di sola lettura (abilitazione, UID, pacchetti validi e richieste tardive), indipendente dal lock fabbrica.
- [x] Dieci gruppi host PASS con regressioni IRQ spurio/errori TX e diagnostica; build ARM 150 unità.
- [ ] Riprogrammare correzione, confermare assenza reset a riposo e durante IN→Node→IN e sorgenti Art-Net/sACN.
- [ ] Discovery/GET/SET URC01 con UID 17FA:E7164570; rilevare rdm? prima/dopo ricerca e misurare risposta sul bus.
- [ ] Confermare origine del reset tramite flag/backup crash: il primo log contiene reboot ma non reset flags.

## Prima fase funzionale approvata — branding/EEPROM/menu

Baseline `0b5fa6c` validata su hardware dall'utente. Le caselle seguenti indicano implementazione e test software; nuovo collaudo hardware separato.

- [x] Tabella centralizzata tre brand, manufacturer e modelli; default nuovo AXN.
- [x] ESTA utente 0x17FA comune, integrato nel PollReply; nessun ID inventato.
- [x] Verificato OEM RSH7 0x2F3F specifico altro prodotto; non riutilizzato.
- [x] Versione struttura 2 a 0x02, brand a 0x25, policy menu a 0x26, discriminanti incorporati.
- [x] Migrazione legacy valida a Videlio/codice ON su conferma utente; payload invariato, versione commessa per ultima e read-back.
- [x] Factory init esplicito AXN/codice OFF, senza auto-init su errore.
- [x] Brand/menu_code solo CDC entro 30 s, applicazione al reboot; accesso pannello diretto o codice 1268 conservato.
- [x] Nove gruppi test host PASS, fault injection I²C e tutti i 256 valori versione legacy.
- [x] Build ARM 149 unità, zero errori/warning; regressioni software protocolli invariate.
- [ ] Collaudare display, dump legacy e power-cut fisico della nuova migrazione.
- [ ] Ottenere OEM ADSW100/NODIM01; il placeholder preesistente non è un'assegnazione.

Default CDC `brand=`: AXN codice OFF, Videlio ON, Generic sempre OFF. AXN/Videlio ammettono override nella medesima finestra. UI senza voci identità; reset utente preserva i valori persistiti. Query `brand?`/`menu_code?` sempre disponibili. RSH7 fornisce la sintassi `brand=`/`OK REBOOT`; `menu_code=` segue la stessa forma.

La fase branding è conclusa e collaudata dall’utente. La nuova autorizzazione include RDM/Node e il solo ulteriore byte ruolo porta; web, bootloader, controller RDM e nuove funzioni NFC restano rinviati. Nessun doppio slot/CRC.

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
- [x] Mappa legacy preservata entro 128 byte; aggiunti soltanto i due byte identità e utilizzata la versione riservata, come autorizzato nella prima fase funzionale. Nessun doppio slot/CRC.
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

- [x] Tabella unica: Brand0 AXN System/NODIM01; Brand1 Videlio HMS/ADSW100; Brand2 Generic/NODIM01.
- [x] Testi pannello e Art-Net dalla tabella unica; API pronta per RDM/web futuri.
- [ ] Integrare i testi in RDM/web quando verranno implementati.
- [x] Preferenza persistente codice menu, migrazione legacy Videlio con codice ON e provisioning CDC verificati.
- [ ] Verificare Manufacturer ESTA e OEM Art-Net realmente assegnati per ogni brand; nessun ID inventato.
- [ ] Censire INI e ID successivi: 94 DRV_STG4 e 95 URC01 risultano occupati nell'uploader; 96 resta candidato, non assegnazione.
- [ ] Distinguere ID uploader legacy, Product/Target ST2FW, Model ID RDM e UID; registrare decisioni approvate.

Verifica: tutte le tre identità e fallback su brand invalido senza convertire automaticamente EEPROM corrotte. Rischio: copiare numerazione brand diversa di RSH7.

## Fase 6 — RDM e NFC

- [x] Portare responder e validazione/discovery dai riferimenti, mantenendo il driver DMX ADSW100 ove possibile (incremento RDM/Node; hardware pendente).
- [ ] Confermare hardware bidirezionale DE/RE, timing risposte e assenza collisioni UID nel dominio manufacturer.
- [x] Matrice PID documentata in AUDIT.md: DMX Address standard 0x00F0 (GET/SET, 2 byte); verificare standard/assegnazioni per tutti gli altri parametri.
- [x] Definire formati, range, GET/SET, PDL, NACK e descrizioni per IP, Netmask, Gateway, DNS, Auto/DMX/Art-Net/sACN, Priority, Display Auto OFF, OFF Time in secondi.
- [x] Distinguere Priority sACN dall'ordine dei protocolli Auto ed esporre entrambi dove previsto.
- [x] Non riutilizzare PID DRV_STG4 0x8200/01 o RSH7 0x8001/02/03 con significati diversi senza verifica formale.
- [ ] Identificare l'implementazione NFC DRV_STG4 effettivamente disponibile; nel checkout esaminato non è stata confermata.
- [ ] NFC: schema versionato completo, magic/device binding, range e applicazione solo dopo migrazione valida.
- [ ] Gestire conflitti RF/I²C e sincronizzazione modifiche pannello/RDM/NFC/web senza sovrascritture da dati NFC obsoleti.

Verifica: round-trip di ogni campo su tutti i canali, permessi e persistenza; discovery RDM e timing su hardware. Rischio: portare stack NCI URC01 su NTAG come se fosse lo stesso hardware.

## Fase 7 — web server e grafica

- [ ] Riutilizzare HTTP raw/asset e impostazione RSH7, senza framework nuovo.
- [ ] Home con brand/modello dinamico, rete, sorgente, diagnostica e stato 12 relè.
- [ ] SVG modulo DIN 12 uscite, connettori e indicatori da specifiche/foto/disegni verificati, riusando elementi RSH7.
- [ ] Configuration: Address, Mode IN/ArtNet Node/sACN Node, Universe sul protocollo scelto con range e campi condivisi CFG_GetValue/CFG_SetValue; rete completa DNS inclusi, Art-Net, sACN, Auto/priorità, display e OFF Time.
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
