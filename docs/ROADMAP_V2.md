> Aggiornamento 17 novembre 2026: la roadmap NFC del 15 novembre sotto e'
> storica. Fingerprint, firme, catalogo binario e generazione durante la build
> sono rimossi. Codec SDCP2 con revisione manuale e CRC32, client URC01 RAW e
> catalogo JSON facoltativo incorporato in ST2FW v3 sono implementati e verificati
> su host/ARM. Resta il collaudo fisico. Vedere [contratto corrente](../protocol/nfc/README.md).

# ADSW100 v2.0.0 — roadmap incrementale

## Allineamento SDCP2 / ST2FW v3 — 22 novembre 2026

- [x] Confrontare DRV_STG4 e URC01 effettivi. Pubblicare SCHEMA_REF da 4 byte e mantenere le 15 istanze ADSW100 già assegnate a 17FA:0060 rev1.
- [x] Verificare Init esplicito su NDEF sperimentale proprio, cessione I²C a RDM, ripresa RF con readback e conservazione dei parametri legacy.
- [x] Consegnare JSON manuale riconosciuto dal parser streaming URC01 e dai due validatori del formato v3.
- [x] Eseguire test host sanitizer, regressioni stabilizzazione, build ARM Debug/Release e controllo MSP per DFU ROM.
- [x] Verificare ST2FW v3 con un BIN DRV originale e documentare il rifiuto del BIN ADSW100 privo di SDI1.
- [x] Acquisire il blocco NDEF incompleto e verificare il recupero conservativo con E8 sul dump esatto tramite test host e build ARM.
- [ ] Programmare sul banco la build di recupero, verificare SYNCED/A5 con E8 già pendente e rileggere lo SNAPSHOT da URC01.
- [ ] Collaudo hardware con URC01, transazioni RF, readback e persistenza. Seguire [NFC_PROTOCOL.md](NFC_PROTOCOL.md).
- [ ] Preparare il primo ST2FW ADSW100 con catalogo incorporato quando saranno definiti SDI1/SDC1, Target ID e layout applicativo.

## Aggiornamento banco: USB DFU / ROM9.1 — 22 novembre 2026

- [x] Identificare la procedura reale: CubeProgrammer USB DFU, BOOT/RESET con BOOT poi rilasciato; non handoff del launch CubeIDE.
- [x] Acquisire il nuovo blocco via SWD hot-plug: CPU in ROM, VTOR1FF09800, ciclo autobaud USART1, nessun fault Cortex; flash/vettori identificati.
- [x] Verificare AN2606 ROM9.1 e non conformità MSP a fine RAM; applicare gap32byte nel solo linker, senza reset/delay o modifica EEPROM.
- [x] Verificare vettori e margini heap/stack sulle build complete Debug/Release.
- [x] Preparare A/B della Release realmente programmata: stessi oggetti, baseline BIN identico, soltanto3parole `_estack` cambiate.
- [x] Eseguire test A/B corretto con DMX/Ethernet invariati: esito negativo, vettore corretto verificato su target, stesso blocco ROM/USART1. Non dichiarare risolto.
- [x] Isolare il blocco sul banco. L'utente conferma avvio ripetibile con DMX scollegato, anche con Ethernet collegata. Per il DFU ROM scollegare DMX prima della programmazione e riconnetterlo dopo l'avvio.
- [ ] Completare la verifica formale di persistenza impostazioni dopo aggiornamenti, reset hardware e riaccensione. Il numero di cicli riusciti non è stato registrato.
- [ ] Se ritorna Wait for boot nell'applicazione, usare boot?/i2c?/ee?; tenere distinta questa diagnosi dal blocco ROM.


## Avvio post-programmazione — 22 novembre 2026

- [x] Ricostruire reset/startup C/main/periferiche/configurazione/controllo INIT e attesa token; nessun bootloader separato o ST2 implementato nei sorgenti esaminati.
- [x] Identificare difetto certo: errore EE_ReadU16 scambiato per magic invalido; distinguere I/O da contenuto senza bypass protezioni o scritture.
- [x] Aggiungere boot? cached e query CDC read-only anche durante blocco/provisioning; conservare token FGM/MAC, timeout e comportamento del boot valido.
- [x] Test reali del controllo boot a -O0/-Os con fault injection HAL, token e conservazione128byte; CDC e regressioni precedenti.
- [x] Build ARM Debug/Release dalle configurazioni esistenti; progetto e protocolli invariati.
- [x] Prima acquisizione CDC read-only dopo avvio riuscito sul banco: A55A, cfg0, EUI-48, AXN, nessuna scrittura EEPROM osservata; non è una riproduzione del guasto.
- [ ] Acquisire boot?/i2c?/ee? nello stato di guasto, prima di reset o power cycle; identificare la causa effettiva di bus/lettura o magic non valido.
- [ ] Implementare solo la recovery giustificata dall'acquisizione. Non assumere che la distinzione del messaggio risolva l'avvio automatico.
- [ ] Dimostrare aggiornamento→avvio automatico per almeno5cicli, NRST e power-on, con impostazioni/brand preservati; qualificare Debug e Release su hardware.


## Priorità: regressione I2C / salvataggi e riavvio — 16 novembre 2026

- [x] Sospendere nuove funzioni NFC e confrontare hook/ownership, EEPROM, RTC, main e IRQ prima/dopo integrazione.
- [x] Riprodurre e correggere blocco salvataggi locali con Init tag E8 e dopo reboot in C3; preservare invalidazione delle basi obsolete e readback.
- [x] Verificare propagazione errore al pannello: l'asterisco resta correttamente quando il salvataggio fallisce; nessun cambio UI.
- [x] Spostare IRQ enable a fine HAL init/filtri; test dei blocchi USER CODE con evento residuo simulato prima di HAL pronto.
- [x] Predisporre build ON/OFF con flag runtime, CDC volatile nfc=0/1, i2c? ed ee? non distruttivi; nessun bypass delle protezioni EEPROM.
- [x] Eseguire regressioni host, fault injection e build ARM complete delle due varianti; protocolli e catalogo protetti invariati.
- [ ] Hardware OFF/ON: persistenza Backlight e OFF Time0, asterisco, RTC, I2C e reset flags; seguire procedura in AUDIT.md senza Init Memory.
- [ ] Hardware: ripetere Run after programming, distinguendo NRST/software/power-on. Acquisire PC/registri senza reset se non riparte; correlare prima di dichiarare risolta la causa.
- [ ] Hardware: verificare errori bus/RF e ripresa, poi regressione sotto traffico DMX/RDM/rete. Riprendere evoluzione NFC solo dopo isolamento della regressione.


## Correzione I2C NFC — 16 novembre 2026

- [x] Confronto diretto con Git0578567: indirizzo55 fisso, ChipHeader e sequenza STOP/READ/rilascio;02 è l'indirizzo precedentemente osservato su ADSW.
- [x] Correggere timeout spurio dopo STOP: test80ms fallisce con la condizione precedente, passa con timeout30ms per comando HAL effettivo.
- [x] Distinguere indirizzo confermato/candidato/HAL, pin dopo conferma, rotazione solo su NACK reale e conferma UID/SAK/ATQA; gestire preloadH7 count0.
- [x] Allineare IRQ MSP al riferimento, aggiungere contatori e fotografia pre-reset con `nfc_i2c?`; niente comando di rilascio a device non NTAG.
- [x] Build ARM150unità, NFC ASan/UBSan -O2, fondazione14gruppi e stabilizzazione10gruppi PASS; EEPROM/SDCP/SYNC1/catalogo e altri servizi verificati invariati.
- [ ] Programmare nuova build e confermare addr02/55 reale, scansioni/capacità e timeout stabili. Il vecchio log38/38 senza NACK non basta ad attribuire ogni guasto al caso STOP/main.
- [ ] Se persiste I2C_ERROR acquisire `nfc?` e `nfc_i2c?` per distinguere IRQ/NACK/linea bloccata; verificare anche con campo RF assente.

## NFC SDCP2 — fase 2 ADSW100, 15 novembre 2026

- [x] Prima implementazione Snapshot C con dati correnti da EEPROM verificata, CFG e identità centralizzata. Il formato attuale è descritto nella sezione più recente.
- [x] NDEF SR/non-SR e selettori; nessun descrittore nel tag, nessun nuovo PID o modifica al catalogo approvato.
- [x] Driver IT STOP/READ e rilascio lock adattati da DRV, IRQ USER CODE e servizio cooperativo nel superloop.
- [x] REQUEST/ACK, revisioni, CRC richiesta, deduplicazione e controllo base, estensioni opache conservate.
- [x] Metadata0x28..0x37; invalidazione preventiva delle basi dopo salvataggi locali, nessuna migrazione/inizializzazione distruttiva.
- [x] Init Memory integra il tag: consenso persistente dopo default EEPROM verificati, sostituzione solo NDEF propri/vuoto senza import JSON, provisioning CC vergine2k con guardie DRV, consumo dopo readback.
- [x] Comando diagnostico CDC `nfc?`; pannello prioritario sul bus, riserva rispettata da EEPROM/RTC.
- [x] Host ASan/UBSan,18 vettori ADSW,21 transazioni complete,170 scenari I/O,68 tagli byte REQUEST e24 Init; fondazione14 gruppi e stabilizzazione10 gruppi PASS.
- [x] Build ARM150unità; hash invariati DMX/RDM/Art-Net/sACN/relè/hold30s, nessuna modifica URC/DRV e nessun commit/tag.
- [ ] Hardware: inizializzazione reale CC/tag e UID/lock, leggere snapshot con reader SDCP2, REQUEST/ACK e persistenza dopo reboot.
- [ ] Hardware: RF mantenuto/rimosso, EEPROM/NFC power-cut reali e recovery conservativo; tag estranei/illeggibili non cancellati.
- [ ] Hardware: convivenza col traffico DMX/RDM/Node/rete, watchdog e RTC durante il commit EEPROM sincrono preesistente.
- [ ] Collaudo client URC01 sul banco e distribuzione del catalogo nel primo ST2FW ADSW100.

## Identificatori assegnati96

- [x] Utente autorizza Model ID RDM96/0x0060 sotto17FA per tutti i brand e distinta assegnazione uploader/Product ID futuro96.
- [x] Sostituire placeholder in rdm.h e registrare costante uploader in defs.h, senza cambiare versione firmware, UID, OEM o EEPROM.
- [x] Registrare prenotazione96 nel MakeFWF.ini Builder, registro e TSV; nessun profilo PIC/Key/Target inventato.
- [x] DEVICE_INFO nei tre brand restituisce00 60; dieci gruppi host e build ARM150 unita' PASS; catalogo legacy94 prodotti invariato al parser.
- [ ] Verificare Model ID96 su controller dopo programmazione; Target/layout e bootloader restano una fase successiva.

## Esito corrente — ADSW100 positivo, URC01 IPv4 ripristinato

- [x] Registrare conferma hardware utente «Funziona bene» per la soluzione ADSW10030 s con URC01 precedente; non estendere la conferma a controller o casi non descritti.
- [x] Su richiesta utente, ripristinare URC01 pre-Node preservando formattazione IPv4 e relative regressioni; verificare corrispondenza dei cinque file firmware/test con snapshot precedente.
- [x] Eliminare da URC01 ascolto passivo prima del TX, possesso bus aggiuntivo e keepalive editor; aggiornare documentazione dello stato corrente.
- [x] Compilare Release URC01 ripristinato e verificare Manager/E1.37-2, controller/discovery, PHY controller/responder e streaming: PASS; stringhe IPv4 presenti nell’ELF.
- [ ] Completare matrice hardware dettagliata Art-Net/sACN, pause/timeout e altri controller reali prima del rilascio finale.

## Decisione corrente — cessione autonoma ADSW100 RDM30 s

- [x] Utente autorizza30 s senza necessita' delle modifiche Node URC01; test con URC01 precedente, presentazione IPv4 preservata.
- [x] Stato RAM RDM distinto da DMX2 s, rinnovato solo dopo pacchetto valido; scadenza monotona anche in IN e conservazione attraverso cambio ruolo.
- [x] Riutilizzare protezione immediata BREAK/SC/RX e guardie TC/linea: nessun taglio frame, nessun parser ISR aggiuntivo.
- [x] CDC hold residuo in ms; nessun campo EEPROM/PID/menu aggiunto.
- [x] Host entrambi Node: pausa10 s senza keepalive, SET/GET, rinnovo29 s, CRC invalido, scadenza durante RX, role change, errori UART, pin/BUSY e tick wrap PASS.
- [x] Dieci gruppi regressione PASS; build ARM150 unita' PASS, ELF distinto adsw100-rdm-hold30.
- [ ] Programmare ADSW100 e collaudare con URC01 precedente, editor10–20 s, SET/GET/Mode, cessazione del traffico e ripresa dopo30 s, entrambi Node e misura DE.
- [ ] Qualificare altri controller reali; non garantire ricezione durante il proprio TX o dopo pause superiori a30 s.
- [x] Utente richiede il ripristino delle modifiche Node URC01: rimosse, resta IPv4 validato.

## Sessione Node con cessione bus — implementazione autorizzata

- [x] Registrare autorizzazione utente alla sospensione DMX OUT durante configurazione RDM.
- [x] URC01: GET DEVICE_INFO ogni500 ms nell’editor, stessa coda, nessun SET implicito/cache mutata; chiusura interrompe il mantenimento.
- [x] URC01 PHY: ascolto silenzio30 ms o frame Node completo +gap100 us; BUSY/pin e timeout500 ms impediscono TX forzati; possesso750 ms solo dopo risposta verificata.
- [x] Riutilizzare buffer TX e cessione ADSW1002 s, nessun nuovo PID/EEPROM; due soli sorgenti URC01 funzionali.
- [x] Build Release, PHY/Manager ASan/UBSan, controller/discovery, responder PHY e streaming PASS.
- [x] ADSW100 host:10 minuti per ciascun Node con keepalive500 ms, SET/GET, timeout, ripresa frame completo e ritorno IN PASS.
- [ ] Programmare URC01 e collaudare acquisizione con Node gia' attivo, keypad oltre5 minuti, SET ripetuti e Mode IN/OUT, chiusura/scollegamento e ripresa DMX.
- [ ] Ripetere con Art-Net e sACN e misurare DE/BREAK/MAB/TC: nessuna qualifica multimasters generica dai soli test host.

## Node: acquisizione bus e pausa operatore

- [x] Acquisire prova dopo TIM7: DMX OUT circa40 frame/s senza fault, SET Mode→IN fallito, nessuna nuova richiesta completa (RX749), ruolo1/1 e28 byte UART aggiuntivi.
- [x] Verificare lease Node2 s e assenza polling dopo lettura completa dell’editor URC01; distinguere l’acquisizione bus dalla protezione di una transazione già ricevuta.
- [x] Verificare che Invalid response URC01 includa primo Start Code diverso da0xCC, compatibile anche con DMX ordinario durante attesa RDM; payload specifico ancora da catturare.
- [x] Utente autorizza sospensione DMX OUT durante configurazione coordinata; implementazione e limiti nella sezione corrente sopra.
- [ ] Verificare separatamente acquisizione iniziale senza collisioni, GET/SET durante sessione, uscita/ripresa DMX e controller scollegato. Non promettere arbitraggio multi-master con la sola finestra RX.

## Guasto hardware isolato — TIM7 turnaround

- [x] Raccogliere guasto sulla build diagnostica: TX6/bus6 persistenti, TIM7 D/1/0, byte/FE crescenti ma pacchetti e TC fermi; USART RX/IRQ abilitati, muted0.
- [x] Riprodurre TimerArm(1)→ARR0 con mock aderente al blocco del contatore STM32: fallimento a199 µs con gli stessi stati TX e timer.
- [x] Imporre minimo2 µs (ARR≥1), senza cambiare BREAK/MAB, lease o refresh;16 soglie temporali normal/discovery e dieci gruppi host PASS.
- [x] Confrontare DRV_STG4 HEAD93eefde e working tree locale: entrambi usano DWT per queste fasi, non il one-shot ARR nullo; riferimento invariato.
- [x] Compilare correzione TIM7:150 unità ARM PASS, artefatto distinto in /private/tmp/adsw100-rdm-timer-fix.
- [ ] Programmare la nuova correzione TIM7; collaudo hardware10–30 minuti con discovery/GET/SET e ritorno a DMX Send senza reset.
- [ ] Verificare anche Node Art-Net/sACN nei limiti della topologia half-duplex; nessuna modifica ai parser.

## Debug durata e presentazione — 15 novembre 2026

- [x] Analizzare prima l’IN: nessun timer RDM a240 s trovato; divisori main limitati, RX/DE e muted verificati staticamente.
- [x] Prima delle modifiche:10 minuti quieti +10 minuti discovery periodici passano;70210 richieste superano TN8 bit/65536 senza blocco. Non riprodotto un guasto puramente temporale a240 s.
- [x] Riprodurre perdita UN_MUTE con nuovo BREAK prima del main e con deadline risposta scaduta; controllo discovery precede ora la sola eleggibilità dell’ACK.
- [x] Riprodurre UN_MUTE scartato dal pending occupato; due snapshot statici FIFO e contatore overflow, senza PID ISR.
- [x] Conservare comando completo oltre la scadenza della sola proprietà RX; niente auto-unmute periodico, ACK tardivi o reset generici USART.
- [x] Aggiungere CDC rdm? esteso e rdm_phy? con stati, registri e contatori, nessuna stampa ISR.
- [x] Host ASan/UBSan: quiete20 minuti, discovery periodico10 minuti,volume70210,TN/HAL tick/DWT wrap, code, CRC/errori, late unicast e recovery senza reset.
- [x] Test Node distinti: richiesta in proprio TX non ricevuta; richieste in RX protette fino a TC per Art-Net/sACN Node. Nessun aumento finestre/lease o modifica parser.
- [x] URC01 presentazione DHCP/IP/gateway/DNS leggibile e formato editor esplicito; codec/payload invariati.
- [x] Riprodurre consumo irreversibile del pending con USART BUSY transitorio; rinviare senza attese e senza cambiare deadline.100 GET/SET/DUB, timeout e recovery PASS.
- [ ] Confermare su hardware il nesso tra BUSY/UN_MUTE e la scomparsa; acquisire diagnostica estesa dopo programmazione della nuova build.
- [x] Registrare nuova evidenza utente: cessano anche DMX IN e lampeggio LED, pannello attivo; muted non spiega l’arresto della porta.
- [x] Host:614928 ms,15000 frame DMX512 e200 discovery alternati, RX abilitata e livelli pubblicati fino al termine.
- [x] Ritorno a DMX Send senza reset: utente conferma LED spento e nessuna risposta; CDC operativa, rx RDM=6219. Guasto DMX non spiegato dal solo muted.
- [x] Estendere snapshot con NVIC USART1 enable/pending, pin RX e ruolo applicato/configurato, senza recovery distruttiva dello stato.
- [ ] Individuare la causa fisica dell’arresto RX con snapshot al guasto; le correzioni UN_MUTE non sono ancora la soluzione dimostrata della perdita DMX/RDM.
- [x] Acquisire CDC sul firmware già caricato: oltre10 minuti in Manager, rx cresce fino a2365 e late=0; elaborazione RX attiva. Utente conferma poi scomparsa dal Manager, CDC rx=6219: discovery/risposte non dimostrate dalla sola crescita rx; ritorno DMX da confermare.
- [ ] Collaudo IN10–30 minuti senza traffico e con Manager automatico; collegare URC01 dopo5 minuti, registrare rdm?/rdm_phy? prima/dopo240 s e dopo la perdita.
- [ ] Separare volume di richieste, latenza main e stato USART/DE tramite contatori; confermare recovery dopo controller restart senza reset ADSW100.
- [ ] Validare Node soltanto in topologia con ownership coordinata: nessuna garanzia multi-controller sulla stessa coppia.
- [x] Presentazione IPv4 sul display URC01 confermata dall’utente dopo aggiornamento firmware.
- [ ] Completare collaudo digitazione dei campi IPv4 sul keypad e SET/readback reali.

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
- [x] Model ID96/0x0060 sotto17FA assegnato dall’utente e implementato.
- [ ] Censire UID sotto17FA e consolidare PID9100..9108 nel registro completo AXN prima del rilascio; OEM ancora non assegnato.
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
- [x] Censire INI e ID successivi:94 DRV_STG4 e95 URC01 occupati;96 assegnato ad ADSW100/NODIM01 e registrato.
- [x] Registrare distintamente Model ID RDM96 e codice uploader/Product futuro96; UID invariato.
- [ ] Definire Target/layout ST2FW nella fase bootloader.

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
