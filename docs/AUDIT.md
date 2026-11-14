# Audit ADSW100 prima della v2.0.0

Data: 14 novembre 2026. Audit statico, senza modifica o esecuzione del firmware. Base ADSW100: commit `7cbf16e`, firmware dichiarato 1.4.0; working tree inizialmente pulito. Nessuna compilazione o misura hardware eseguita. I risultati seguenti consolidano le evidenze raccolte, con rettifica esplicita del comportamento DMX richiesta dall'utente.

## Stato corrente dopo rigenerazione e stabilizzazione

L'utente ha eseguito la rigenerazione CubeMX. La precedente condizione di arresto è superata. Le sezioni storiche sotto descrivono i difetti della baseline, non lo stato attuale di ciascun file. Nessun commit/tag creato, versione firmware lasciata 1.4.0: questa è una base di sviluppo, non il rilascio 2.0.0.

### Correzioni implementate

| Evidenza iniziale | Stato corrente e file |
|---|---|
| A01, A02, impulsi manuali bloccanti | `relay_driver.c/.h`: macchina a stati unica per tutte le 12 uscite; manuale pubblica un desiderato. `main.c` chiama il servizio impulsi in TIM6; 40 ms impulso, 30 ms guardia, attesa OFF 0..30 s, inversioni solo dopo guardia. Cambio indirizzo non esclude più attuatori dal servizio. Stato noto distinto dal desiderato, senza feedback contatti inventato. |
| A04, A05 | `dmx_512.c/.h`: staging ISR e pending completi; main copia con IRQ sospesi per la sola sezione limitata, azzera coda frame corto e applica timeout monotono 1000 ms. `lcd.c` non cancella più il buffer condiviso. Relè e timeout dati non dipendono più dal conteggio di chiamate main. NE/ORE invalidano il frame fino al prossimo BREAK candidato. |
| A03, A14 | `eeprom.c/.h`: lettura configurazione completa prima della pubblicazione RAM, propagazione esiti HAL, scritture entro 128 byte con read-back, save senza falsi successi, nessuna riparazione persistente dal load. Gap 0x1B e versione 0x02 preservati; nessun nuovo campo. Fallback per estensioni storiche OFF/priorità fuori range solo in RAM. `user_interface.c` segnala errori salvataggio anziché mostrare Saved; factory reset interrompe il percorso se EEPROM fallisce. |
| A06, A07 | `sacn.c/.h`: offset sequence corretto; validazione PDU/DMP/count/universo/priorità, tracking di quattro CID, scarto preview, sequence per sorgente, timeout 2500 ms, stream termination, soglia minima e HTP a priorità massima. `artnet.c/.h`: due sorgenti IP/Physical, sequence, merge HTP, record sorgente 10 s e disponibilità applicativa 3 s, validazione lunghezze. Entrambi limitano i dati all'universo configurato e invalidano gli stati al cambio universo/stop. |
| A15 | `artnet.c`: PollReply corretto per porta, ESTA Lo/Hi, Net/Sub, SwOut, Style, stato DHCP/15 bit, terminazione nomi e report con contatore. OEM/ESTA placeholder non trasformati in assegnazioni inventate. |
| A08, A09 | `cfg_io.c/.h`, `nfc_i2c.c`: unico percorso boot con magic, versione 1, schema JSON, chiavi uniche, tipi/range e IPv4 verificati; applicazione completa del candidato prima di save. IPv4 tramite rappresentazione raw lwIP, senza convertire i byte EEPROM. Formato JSON legacy conservato; Auto usa ancora il campo universe come indirizzo DMX nel bridge legacy. |
| A10 | `usb_debug.c`: buffer CDC statico non riutilizzato finché TxState è attivo; niente spin BUSY. Diagnostica durante BUSY viene scartata esplicitamente, non accodata. |
| A11, A12, A13 | `network.c`, `lwipopts.h`, `user_interface.c`: percorso TX installato sul netif con cache clean ed errori HAL restituiti a lwIP, attesa limitata 2 ms; filtro MAC accetta multicast e delega appartenenza a lwIP IGMP, evitando lo slot perfect unico. Reset cache lascia anche i gruppi; apply idempotente con retry; DNS abilitato e aggiornato a runtime in statico. Start listener restituisce successo reale e viene ritentato su errore PCB/bind. |
| Integrazione CubeMX | `stm32h7xx_hal_msp.c`, `ethernetif.c`, linker: ripristinati MSP Ethernet e init LAN9303 nelle sezioni USER CODE; gestione MAC interno 100/full nel servizio rete. Accettati i nuovi nomi .RxDescripSection/.TxDescripSection in D2; nessuna sostituzione manuale del codice clock generato. |

### Clock verificato staticamente

Codice generato: VOS0, PLL HSE 25 MHz M5/N192/P2, SYSCLK 480 MHz, HCLK 240 MHz e APB 120 MHz. TIM6 240 MHz /240 /1000 = 1 kHz; UART1 250000 8N2, UART2 115200; I²C TIMINGR 0x307075B1; SPI1 PLLQ 64 MHz /4 =16 Mbit/s; USB seleziona HSI48. MX_RTC_Init e i marcatori applicativi main/IRQ sono preservati. Frequenze e forme d'onda sono calcolate/ispezionate, non misurate.

### Verifiche software eseguite

Comandi riproducibili dalla root:

```sh
python3 tests/test_stabilization.py
python3 tests/test_stabilization.py --arm-build /tmp/adsw100-validation
```

Sei harness host compilano le implementazioni reali con HAL/lwIP simulati: Art-Net (merge, duplicati, universo estraneo, timeout, PollReply e lunghezze), sACN (CID/priorità/soglia/preview/terminazione/sequence/wrap/timeout), relè (manuale durante impulso, indirizzo limite, guardia, OFF Time, wrap tick), JSON/IPv4 (round-trip, versione/range, rifiuto parziale e overflow numeri), DMX (pubblicazione completa/coda corta/ORE/perdita), EEPROM (limiti EUI, read/write fault, preservazione riservati, save/load). Tutti PASS. Non sono test di rete fisica, USB o durata ISR.

Build completa diretta GCC ARM 14.3.1 installato con CubeIDE: 149 unità C/ASM, ELF e map linkati con linker ADSW100, zero errori e zero warning. Il runner usa gli include Debug della .cproject; non è stata eseguita una build tramite GUI/headless Eclipse. ELF/map/log in `/private/tmp/adsw100-validation`. Size: text 185868, data 640, bss 69339 byte; non è una misura del picco stack/heap. Map D2: RX descriptor 0x30000000 (0x60), TX 0x30000060 (0x60), RX pool 0x300000C0 (0x4983), fine pool 0x30004A43 prima dell'heap lwIP a 0x30006000.

Controllo rispetto alla fotografia successiva alla rigenerazione: modifiche manuali main/MSP/ethernetif confinate nei marcatori USER CODE; nessuna modifica manuale HAL/CMSIS/lwIP vendor. Il grande diff delle librerie deriva dalla rigenerazione dell'utente e non è stato annullato o riformattato. Unico nuovo file di codice: runner test; nessun nuovo modulo runtime.

### Limiti e prove ancora necessarie

- Misurare bobine/guardia/OFF Time e RL_ENABLE/monostabile sotto carico e durante blocchi I²C/USB/rete. Il software conclude gli impulsi finché TIM6 e la base HAL restano serviti; non sostituisce protezioni hardware in caso di CPU/IRQ bloccati. Verificare potenza per 12 impulsi simultanei.
- Misurare durata della sezione critica DMX (copia 512 byte) rispetto allo slot 44 us; FE con dato zero è candidato BREAK, non qualifica temporale completa BREAK/MAB. Testare rumore/ORE e centralina spenta, compreso Auto con altre sorgenti valide.
- Art-Net resta subset DMX/Poll, senza ArtSync/ArtAddress/ArtIpProg; sACN non implementa sync/PAP/discovery: Force_Synchronization viene scartato e dati non forzati sono trattati asincroni. Quattro CID massimi: a tabella piena entra soltanto una nuova sorgente con priorità maggiore della minima presente. Verificare interoperabilità reale, non dichiarare conformità completa E1.31/Art-Net.
- Il normale scheduler mantiene il flag booleano per UI/diagnostica e può perdere tick sotto blocchi; timeout dati e impulsi sono indipendenti, non tutti i task secondari. Restano accessi I²C/SPI e operazioni esplicite startup/reset bloccanti, senza nuovo RTOS.
- EEPROM resta legacy senza CRC/transazione: un power-cut durante più scritture può lasciare dati parziali. Nessuna migrazione branding, doppio slot o nuovo campo. Verificare chip/page size effettivo e dump Videlio; pagina conservativa da 8 byte, valida soltanto dopo conferma della BOM.
- NFC: stack fisico, CC/lock/readdress e factory reset best effort restano da collaudare; l'applicazione JSON è verificata, non l'intera catena RF/NDEF. Formato legacy non include DNS/display/OFF Time. Non c'è conversione automatica di tag scritti con IPv4 invertito dalle versioni precedenti: controllare dump noti.
- USB: buffer persistente verificato staticamente, enumerazione e traffico reale da provare. Ethernet: validare LAN9303, link flap, DHCP/DNS, multicast e cache/offload su hardware; il vecchio low_level_output generato resta presente, ma dopo Network_InitAfterLwip il netif usa il percorso TX controllato.
- RDM, branding, web, bootloader, nuovi PID/ID e aggiornamento DMX/USB restano fasi successive non introdotte in questo incremento. Le registrazioni ESTA/OEM e l'ID candidato 96 restano non assegnati.

## Aggiornamento: avvio porting e punto di arresto

L'utente ha autorizzato porting 480 MHz e stabilizzazione senza nuove funzionalità, commit o tag. Ha confermato 128 byte EEPROM applicativi: preservare la mappa legacy, nessuna conversione/inizializzazione automatica, nessun nuovo campo o doppio slot. La migrazione branding è rinviata; la precedente proposta va letta alla luce di [EEPROM_MIGRATION.md](EEPROM_MIGRATION.md).

Preparato ADSW100.ioc dal file effettivo ADSW100_AXN/ADSW100_AXN.ioc, modificando soltanto ProjectManager.ProjectFileName e ProjectManager.ProjectName per mantenere ADSW100. Nessun percorso assoluto nuovo introdotto. Il confronto rileva anche variazioni non-clock: CubeMX 6.16→6.17, FW H7 1.12.1→1.13.0, lwIP 2.1.2→2.2.1, ICMP e status callback espliciti, ordine init differente e RTC assente da functionlistsort AXN (pur restando configurato nell'IOC). Verificare che MX_RTC_Init e tutto il codice applicativo restino presenti dopo rigenerazione.

CubeMX e pacchetti H7 1.12.1/1.13.0 presenti localmente. Tentativo di avvio del launcher installato con -h: uscita 134; diagnostica di accesso negato al log /Users/gianlucatini/.stm32cubemx/STM32CubeMX.log. Il test di avvio non certifica la possibilità di generare né una incompatibilità generale dell'installazione; non è stata eseguita alcuna rigenerazione valida. In accordo con l'istruzione esplicita dell'utente lo sviluppo si arresta a questo passaggio, per rigenerazione a cura dell'utente.

**Implementato:** soltanto sostituzione IOC e aggiornamento Markdown. **Verifiche statiche:** confronto completo IOC e nomi progetto, controlli sulle modifiche. **Test software:** nessuna compilazione/test funzionale; avvio CubeMX fallito. **Hardware:** nessun nuovo test. I problemi A01..A15 restano aperti; i sorgenti mantengono il clock precedente finché non vengono rigenerati. Non usare questa combinazione IOC/sorgenti come firmware 480 MHz validato.

## 1. Perimetro e progetti effettivamente trovati

| Riferimento | Percorso verificato | Note |
|---|---|---|
| ADSW100 | `/Users/gianlucatini/Dropbox/Firmware_Git/ADSW100` | Oggetto dell'audit |
| RSH7 | `/Users/gianlucatini/Dropbox/Firmware_Git/RDM_SPLITTER_H7` | Checkout non comprende Core/Boot; il codice boot è presente nel riferimento Git locale `origin/Uploading-firmware-da-DMX` |
| URC01 | `/Users/gianlucatini/Dropbox/Firmware_Git/URC01` | Protocolli, uploader, RDM/NFC e registro identificativi |
| DRV_STG4 | `/Users/gianlucatini/Dropbox/Firmware_Git/FIRMWARE_DRV_STG4` | Responder RDM STM32G4; nel checkout ispezionato non è stato individuato un modulo NFC dedicato |
| AXN 480 MHz | `/Users/gianlucatini/micro/Studio Due/ADSW100_AXN` | Confronto sorgenti clock/periferiche |
| ADSW100 legacy | `/Users/gianlucatini/micro/Studio Due/ADSW100` | Copia di confronto |
| Videlio 1.4.1 | `/Users/gianlucatini/micro/Studio Due/ADSW100_VID_NO_CODE` | defs.h dichiara 1.4.1; eeprom.c identico alla base |
| Altre copie | `/Users/gianlucatini/micro/Studio Due/RDM_SPLITTER_H7`, `/Users/gianlucatini/micro/Studio Due/DRV_STG4` | Percorsi trovati; non assumere struttura/revisione equivalenti ai repository |

## 2. Architettura attuale

STM32H743VITx, HAL STM32Cube, superloop cooperativo senza RTOS. `main.c` abilita MPU e cache, inizializza GPIO, I²C1, SPI1, USART1/2, Ethernet/lwIP, USB CDC, TIM6, watchdog e RTC. TIM6 pubblica un flag booleano a 1 ms; il main esegue `task_1ms()`, `MX_LWIP_Process()` e `NET_PollApply()`.

`task_1ms()` aggiorna enable/watchdog hardware relè, `RelayTask`, encoder, rete, UI ogni 10 chiamate e attività periodiche. DMX entra su USART1 a 250 kbaud, 8N2 con ISR dedicata, senza DMA DMX. `dmx_512.c` scrive 512 slot condivisi; `relay_driver.c` seleziona DMX/Art-Net/sACN e pilota 12 relè bistabili tramite bobine ON/OFF. Non è stato individuato un trasmettitore DMX applicativo operativo: configurare UART TX_RX non dimostra un DMX OUT funzionante.

Art-Net e sACN usano API UDP raw lwIP, callback eseguite dal main, database da 16 universi ciascuno. Ethernet usa DMA e memoria D2; descrittori/pool hanno sezioni linker dedicate. Configurazione globale persistita su EEPROM I²C, pannello LCD/encoder, NFC NDEF/JSON e diagnostica USB. Non esistono ancora nel firmware ADSW100 un responder RDM completo, un web server applicativo o un vero bootloader di aggiornamento: `Startup_SimpleUnlock()` è un gate di provisioning, non un updater.

## 3. Criticità ordinate per gravità

C = confermato dal codice; R = rischio potenziale; H = da misurare/verificare su hardware. Gravità riferita alle conseguenze possibili, non a incidenti osservati.

| ID / gravità / stato | File e funzione | Evidenza e conseguenza | Correzione proposta |
|---|---|---|---|
| A01 Critica C/H | `Core/Src/relay_driver.c`, `RelayTask`, `Relay_ManualEnter` | In manuale RelayTask ritorna prima di avanzare/terminare impulsi automatici già avviati. Possibile bobina lasciata comandata; protezione esterna e durata reale da misurare | Separare completamento impulsi dalla selezione del comando; servire sempre il motore relè |
| A02 Alta C | stesso file, `RelayTask` | Il ciclo termina a j=512 e quindi non serve tutti i relè con indirizzi oltre 501; il cambio indirizzo può escludere anche un impulso attivo | Servire sempre 12 attuatori, azzerare livelli oltre footprint e definire range indirizzo |
| A03 Alta C | `Core/Src/eeprom.c`, `EE_LoadConfig`, `EE_SaveConfig`, `EE_InitMemory` | Helper ignorano errori, letture fallite diventano zero, save/init restituiscono HAL_OK anche dopo errori; nessun CRC/transazione | Snapshot verificato, propagazione errori, read-back, commit atomico e copie |
| A04 Alta C | `Core/Src/main.c`, callback TIM6 e superloop; relè/UI/EEPROM | Flag booleano accorpa tick; impulsi, OFF Time, timeout DMX e watchdog applicativi dipendono dal numero di chiamate | Deadline su tempo monotono; servizio impulsi indipendente da task lenti |
| A05 Alta C | `Core/Src/dmx_512.c`, `DMX512UARTInterrupt`; `lcd.c`, `EncoderTask`; `relay_driver.c`, `RelayTask` | ISR aggiorna buffer letto/cancellato dal main, senza pubblicazione frame atomica; conteggio frame non usato dai relè e frame corti lasciano coda precedente | Buffer RX e snapshot completo con conteggio e timestamp; invalidare coda corta |
| A06 Alta C | `Core/Src/sacn.c`, `sacn_rx_cb`, `sACN_OnDataPacket` | Sequence letta da fr[72] invece di fr[73]; sequence non filtrata, CID e priorità non arbitrati, soglia sacn_min_prio inutilizzata | Parser rigoroso e stato per sorgente, selezione priorità, sequence/timeout corretti |
| A07 Alta C | `Core/Src/artnet.c`, `ArtNet_FindUniverse`; `sacn.c`, `sACN_FindUniverse` | 16 entry mai liberate; Art-Net alloca anche universi estranei e sACN multicast non filtra universo configurato | Limitare agli universi utili e riciclare/reset controllato; impedire saturazione permanente |
| A08 Alta C | `Core/Src/nfc_i2c.c`, `NFC_Service`; `main.c`, startup | NFC_Service chiamata al boot applica JSON senza verificare magic/versione/range; routine alternativa NFC_BootApplyIfValid verifica magic ma non viene chiamata dal main | Un solo percorso validato, nessuna scrittura prima della migrazione EEPROM |
| A09 Alta C | `Core/Src/cfg_io.c`, conversioni IP, parser e `apply_config_vars` | Costruisce uint32 da ottetti come valore numerico big-endian ma rete assegna direttamente a ip4_addr_t.addr: incoerenza su MCU little-endian; troncamenti numerici e apply parziale | Usare conversioni lwIP esplicite; validare intero candidato prima di applicarlo |
| A10 Alta R | `Core/Src/usb_debug.c`, `USB_Printf`/`USB_Send` | CDC usa puntatore al buffer locale senza attendere completamento ultimo invio; possibile uso dopo ritorno/riutilizzo stack | Buffer TX persistente con ownership fino a completamento |
| A11 Media C | `LWIP/Target/ethernetif.c`, `low_level_output` | HAL_ETH_Transmit può bloccare fino a 20 ms; risultato ignorato e ritorno ERR_OK | Propagare errori e ridurre blocchi compatibilmente con driver verificato |
| A12 Media C/R | `Core/Src/network.c`, filtro IGMP, `link_cb`; `user_interface.c`, `NET_ApplyFromConfig` | Un solo slot MAC multicast sovrascritto; stato join/cache e gruppi lwIP possono divergere nei restart; errori join ignorati aggiornando lo snapshot | Contabilità gruppi/filtro coerente, retry su fallimento; verificare link flap e DHCP |
| A13 Media C | `Core/Src/network.c`, `NetworkTask` | Snapshot runtime confronta DHCP/IP/mask/gateway, non DNS: modifiche DNS non vengono applicate da questo percorso | Applicare anche DNS e verificare LWIP_DNS effettivo nella build |
| A14 Media C | `Core/Src/eeprom.c`, `EE_LoadConfig`; `Core/Inc/defs.h` | Versione definita ma non letta/scritta; load riscrive default off/priorità senza verificare origine; bounds non proteggono area EUI | Migrazione esplicita prima del normale load; limiti scrittura per componente |
| A15 Media C | `Core/Src/artnet.c`, `send_artpoll_reply` | Campi PollReply errati o statici, descritti sotto | Ricostruire layout e informazioni reali |

Non è stato dimostrato un overflow diretto nei normali parser UDP: le copie sono limitate e i payload verificati rispetto al buffer. Restano da provare pacchetti malformati, overflow aritmetico del parser JSON e tutti i percorsi NFC. Non qualificare queste verifiche come vulnerabilità già riprodotte.

## 4. Art-Net approfondito

`artnet_udp_recv` verifica ID, opcode, header minimo DMX e protocol version >=14; Port-Address calcolato correttamente da Net/SubUni. Lunghezza dati viene però troncata a 512 prima della verifica: un valore dichiarato illegale >512 può diventare accettabile; non verifica minimo 2 e parità della lunghezza. Rifiutare lunghezze illegali prima di usare i dati.

`ArtNet_OnDataPacket` ignora IP/porta sorgente e sovrascrive sempre l'universo. Sequence viene memorizzata senza ordinamento o scarto duplicati; manca merge/sorgenti multiple. Definire stato per sorgente e politica coerente con Art-Net (sequence 0 disabilita ordinamento). Non assegnare ad Art-Net la priorità numerica sACN.

`ArtNet_GetSlotsView` invalida dopo 3000 ms tramite HAL_GetTick: timeout applicativo distinto dalla gestione dei timeout delle sorgenti di merge. Stop/restart listener non svuota il database: possibile riutilizzo di campioni non ancora scaduti. ArtSync e altri opcode non sono implementati: delimitare il supporto dichiarato.

PollReply: porta a offset 14 scritta big-endian invece di little-endian; ESTA a offset 24 predisposta big-endian anziché Lo/Hi; NetSwitch/SubSwitch fissi a zero; SwIn scritto a 190 invece di 186 e SwOut a 194 invece di 190; Style scritto a 203 invece di 200 e sovrascritto dalla copia MAC 201..206. Firmware indicato 1.0, NodeReport statico, OEM 0xFFFF e ESTA 0x0000 sono placeholder, non assegnazioni. Il bit Status1[2] indica boot ROM, non supporto indirizzamento 15 bit. Verificare inoltre Status2, terminazione dei nomi, richieste ArtPoll minime/targeted e risposte differite. Riferimento: [specifica ufficiale Art-Net](https://art-net.org.uk/downloads/art-net.pdf), sezioni ArtPollReply/ArtDmx/Data Merging.

## 5. sACN approfondito e modalità Auto

`sacn_rx_cb` controlla preambolo, ACN PID, vettori root/framing e DMP, property count e presenza payload, accettando soltanto start code 0. Non controlla flags/length delle PDU, primo indirizzo e incremento DMP, range universo 1..63999, priorità 0..200; count oltre 513 viene troncato se presente nel buffer anziché rifiutato. Sequence si trova a offset assoluto 111 (fr[73]), mentre il codice legge 110. Ignora CID, options preview/stream terminated/force synchronization e sync address. Soglia sacn_min_prio non applicata; sorgente a priorità bassa può sostituire una alta e mantenere vivo l'universo.

Multicast 239.255.Uhi.Ulo corretto; unicast filtra l'universo, multicast no. Listener non vincola realmente il trasporto: la selezione controlla join/filtro universo, non la destinazione UDP. Timeout vista 2500 ms sul solo universo, non sulle singole sorgenti. Proposta: tracking CID e sequence per sorgente, timeout/terminazione, priorità e merge a pari priorità, scarto preview; delimitare supporto sync/PAP senza dichiararlo implementato. Il riferimento [ETC sACN source loss](https://etclabs.github.io/sACNDocs/2.0.2/source_loss_behavior.html) conferma la necessità di ragionare sulle sorgenti e sulla finestra 2,5 s; non viene proposto di importare la libreria.

`Relay_SelectAutoProto` sceglie sempre il primo disponibile secondo una delle sei permutazioni `proto_priority`; il ritorno di una sorgente preferita prevarica immediatamente quella attiva. In assenza di sorgenti mantiene il nome dell'ultima. Disponibilità rete basata su slot non scaduti, senza validazione priorità sACN; DMX su dmx_timeout. Separare priorità tra protocolli e priorità sACN. Verificare ingresso/uscita Auto, cambio universo, perdita link e ritorno sorgente evitando transienti. Rete e relè sono nello stesso main: non presumere race ISR tra callback UDP e RelayTask; la race confermata riguarda DMX fisico.

## 6. DMX, relè e operazioni bloccanti

**Comportamento richiesto dall'utente:** alla perdita DMX l'azzeramento del buffer è voluto affinché, a centralina spenta, i relè si spengano rispettando OFF Time. Non è un bug funzionale. In Auto una sorgente alternativa valida può subentrare secondo la priorità prevista. Il problema è la sincronizzazione del buffer e la base tempi, non lo spegnimento.

Il timeout di 1000 decrementi viene aggiornato da EncoderTask; sotto blocchi non equivale necessariamente a 1000 ms. ISR pubblica il frame al BREAK successivo, scrive slot in diretta, usa FE come BREAK e non risincronizza completamente dopo NE/ORE. Misurare BREAK/MAB e recupero errori; lettura RDR prima della classificazione flag merita verifica. Nessun DMA DMX attuale da correggere; DMA eventuale solo se utile a RDM/update, con ownership e cache definiti.

Blocchi nel driver relè: `Relay_PulseOn` e `Relay_PulseOff` usano HAL_Delay(40) e HAL_Delay(30), chiamati da `Relay_ManualToggle`. Automatico già a stati, ma dipende dalle chiamate e si ferma in manuale. Altri blocchi che ritardano il servizio: I²C EEPROM con timeout 100 ms e ACK polling fino a 50 tentativi, salvataggi UI, accessi LCD/I²C/NFC, diagnostica USB con budget di tentativo 2 ms per chiamata, TX Ethernet fino a 20 ms, startup/hardware scan/provisioning e HardFactoryReset. Non sono tutti bug: distinguere attese startup da blocchi durante funzionamento.

Proposta locale in relay_driver.c: stato stabile e desiderato separati da fase (idle/attesa OFF/impulso ON o OFF/guardia), deadline wrap-safe da tempo monotono. Servire tutte le bobine indipendentemente da sorgente/manuale/indirizzo. Manuale cambia soltanto il desiderato; completare impulso in corso e guardia prima di un'inversione; vietare bobine opposte contemporanee. Conservare impulso 40 ms, guardia 30 ms e OFF Time 0..30 s salvo futura decisione. Se il main resta bloccante, terminazione impulso tramite timer/ISR minima o altra soluzione verificata: le sole deadline nel main non garantiscono una durata massima. Misurare RL_ENABLE e monostabile esterno, corrente e limiti impulsi simultanei. Stato mostrato in UI deve distinguere desiderato/stabile, senza fingere feedback fisico assente.

## 7. EEPROM, NFC, organizzazione e networking

Vedere [analisi EEPROM](EEPROM_MIGRATION.md) per mappa, confronto byte per byte e migrazione. Lettura MAC e verifiche EUI sono duplicate tra main.c ed eeprom.c; evitare un nuovo terzo percorso. Il fallback MAC locale non prova unicità dell'UID RDM.

NFC_Service applica una volta al boot, poi esporterebbe modifiche periodiche, ma la chiamata periodica in task_1ms è commentata. Network_InitAfterLwip ricarica EEPROM dopo NFC: gli errori di save potrebbero riportare valori precedenti senza diagnosi affidabile. Formattazione/reset NFC best effort può riportare successo dopo scritture fallite. Non modificare CC/lock/readdress prima di confermare chip e protocollo. JSON attuale confonde universe e DMX address secondo modalità, non conserva esplicitamente entrambi gli universi in Auto; mancano DNS, display e OFF Time. Versione JSON emessa ma non validata. Necessario candidato completo condiviso da pannello/RDM/NFC/web, integrato nei servizi esistenti.

NET_ApplyFromConfig risiede in user_interface.c pur essendo funzione di rete; annotare accoppiamento senza imporre refactoring. Header user_interface_old.h, include ripetuti e rami Auto ormai non raggiungibili sono debito minore, non autorizzazione a cancellare codice. Verificare su map la separazione heap lwIP a 0x30006000 da pool/descrittori e il budget D2. TX DMA richiede coerenza cache anche se i descrittori sono non cacheable: verificare memoria effettiva dei payload. lwIP ha checksum software disabilitati; validare l'offload RX/TX effettivo. Non dichiarare overflow di memoria senza map o riproduzione.

## 8. Clock: confronto ADSW100_AXN

| Parametro | ADSW100 attuale | ADSW100_AXN |
|---|---|---|
| Alimentazione / VOS | LDO / scale3 | LDO / scale0 |
| HSE dichiarato | 25 MHz | Configurazione di riferimento da confermare sulla scheda |
| PLL1 M/N/P/Q/R | 5/48/2/5/2 | 5/192/2/15/2 |
| SYSCLK | HSI 64 MHz, PLL non selezionato | PLL 480 MHz con HSE 25 MHz |
| AHB | 64 MHz | 240 MHz |
| APB1/APB2 | 32 MHz | 120 MHz |
| APB3/APB4 | 64 MHz | 120 MHz |
| Flash latency | 1 | 4 |
| TIM6 PSC / ARR | 63 / 999 | 239 / 999 |
| I²C1 TIMINGR | 0x00707CBB | 0x307075B1 |
| SPI1 | PLL1Q / 4 | PLL1Q / 4 |
| USB | PLL1Q | HSI48 abilitato nel clock principale; verificare selettore USB nella variante |

Con HSE 25 MHz, PLL1Q attuale =48 MHz; AXN =64 MHz, quindi SPI passa indicativamente da 12 a 16 MHz e USB non può restare su PLL1Q. UART deriva da PCLK e va verificato baud reale; timer APB raddoppiato deve restare 1 ms. Non copiare soltanto SystemClock_Config: aggiornare IOC e periferiche in modo coordinato. Verificare revisione silicio H743, VDD, regolatore, limiti datasheet, USB 48 MHz, I²C rise time, SPI LCD, RMII/MDIO e watchdog. Il progetto funzionante è riferimento, non collaudo della scheda ADSW100 installata.

## 9. Matrice di riuso, risorse e identificativi

| Funzione | Origine | Riuso / vincoli |
|---|---|---|
| Branding e descrittori | RSH7 defs.h/rdm.c | Tabella esistente, ma numerazione brand diversa: ADSW100 richiede 0 AXN System/NODIM01, 1 Videlio HMS/ADSW100, 2 Generic/NODIM01 |
| Web e SVG | RSH7 web_server.c, Web, Web_Graphics | Riutilizzare home/configuration, binding stato, connettori/indicatori; rifare geometria DIN 12 uscite sulla specifica verificata |
| RDM | DRV_STG4 rdm.c e RSH7 rdm.c | Validazione, discovery, risposte differite e UID; adattare UART/pin/tempi H7, senza importare router completo |
| NFC | ADSW100 nfc_i2c.c/cfg_io.c; URC01 nfc_* | Riutilizzare elementi compatibili; NFC DRV_STG4 non confermato nel checkout, stack NCI URC01 non equivale a NTAG I²C |
| USB/DMX update | RSH7 riferimento Git origin/Uploading-firmware-da-DMX; URC01 legacy_st2_*, firmware transfer | Riutilizzare formati/CRC/handshake e recovery, distinguendo ST2 legacy da ST2FWv2 |
| 480 MHz | ADSW100_AXN | Porting coordinato clock/periferiche e collaudo |

RSH7 usa 0x0206 per tutti i brand della tabella RDM (Studio Due/AXN/generic), DRV_STG4 dichiara lo stesso manufacturer. Questo non certifica un'assegnazione autonoma AXN o Videlio. Registro locale URC01 docs/STUDIO_DUE_IDENTIFICATORI.md indica RSH7 Model ID 1 e URC01 0x0100; è parziale e datato. Accesso al registro ESTA online tentato ma risposta 403: registrazioni AXN/Videlio e OEM Art-Net restano da verificare. Non usare placeholder ADSW100 come ID assegnati.

MakeFWF.ini in `/Users/gianlucatini/micro/Studio Due/UPLOADER/MakeFWF_4_0/ProgWin` arriva a ID93. Tuttavia uploader Python mostra Product94 DRV_STG4 e Product95 URC01: 94 non è libero. 96 è solo candidato successivo nel censimento locale, senza assegnazione; occorre verificare il dominio legacy, eventuali buchi riservati e copie INI prima di dichiarare il primo ID realmente disponibile. Nessun ID viene assegnato in questa fase.

RDM DRV_STG4: PID standard DMX_START_ADDRESS 0x00F0 GET/SET a 2 byte; custom 0x8200 riguarda frequenza PWM e 0x8201 smooth (1 byte), non rete o modalità ADSW100. RSH7 0x8001/02/03 riguardano router/DS/frame forwarding. Non riusarli con significati incompatibili. IP/mask/gateway/DNS, mode, priorità, display auto-off e OFF Time richiedono verifica PID standard applicabili e/o assegnazioni manufacturer documentate, PDL, range, GET/SET/NACK e PARAMETER_DESCRIPTION.

Risorse da qualificare: flash H743 2 MiB, RAM e budget misurati da map (nessun margine effettivo certificato), heap/pbuf/socket raw per HTTP, flash asset SVG, UART bidirezionale e controllo DE/RE per RDM/DMX update, I²C/NTAG con accesso RF concorrente, timer a microsecondi e buffer pacchetti. Il boot RSH7 riserva 0x08000000..0x08040000, app da 0x08040000 con vettori +0x400 e commit +0x40; è un modello, non ancora layout ADSW100 approvato. Servono recovery dopo power-cut e uscite relè sicure durante flash. Ethernet update soltanto predisposizione, nessuna implementazione ora.

## 10. Esito e verifiche mancanti

Priorità: sicurezza impulsi, snapshot DMX e tempo reale, EEPROM senza perdita, parser/sorgenti rete, poi nuove funzioni. Dettagli incrementali in [ROADMAP_V2.md](ROADMAP_V2.md). Restano misure hardware, layout reale modulo/SVG, chip EEPROM/NFC, pin RDM, registrazioni e censimento ID definitivo. Nessun sorgente, configurazione, tag o commit di riferimento è stato modificato dall'audit.
