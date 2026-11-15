# Audit ADSW100 prima della v2.0.0

Data: 14 novembre 2026. Audit iniziale statico, seguito dagli incrementi e test documentati sotto. Base ADSW100: commit `7cbf16e`, firmware dichiarato 1.4.0; working tree inizialmente pulito. Nella prima analisi non erano state eseguite compilazioni o misure hardware. I risultati seguenti consolidano le evidenze raccolte, con rettifica esplicita del comportamento DMX richiesta dall'utente.

## Correzione transazioni RDM Node e PID rete — 15 novembre 2026

Base corrente `b0b9020` (RDM/Node e correzioni precedenti). L’utente conferma funzionamento e test hardware di RDM e Art-Net/sACN Node; segnala ancora GET intermittenti e SET non completati in Node e PID IP non visibili. Questa correzione è verificata da test host/build, **non ancora collaudata sulla scheda**. Le sezioni storiche successive mantengono le evidenze delle fasi precedenti.

### Cause e topologia

| Evidenza | File/funzioni | Conseguenza / correzione |
| --- | --- | --- |
| Lacuna confermata, alta | dmx_512.c NodeTryStart / ricevitore | Il TX controllava attività recente (30 ms), pacchetto pronto e lease controller, senza proprietà esplicita dal BREAK a fine transazione; mancavano BUSY USART e timeout del ricevitore. Aggiunta macchina RX distinta dalle fasi TX e acquisizione protetta; il solo silenzio non vale come completamento. La ripartenza prematura è un rischio del codice, non una causa esclusiva già provata dei sintomi sul banco. |
| Race confermata, alta | DMX512_Service / DMX512_SendPacket | Copia pending→main lasciava una finestra senza proprietario; un nuovo BREAK poteva essere perso/rimpiazzato dalla gestione della richiesta precedente. Ora PROCESSING conserva il bus, un nuovo RX prevale sulla vecchia risposta; verifica BUSY/RXNE/errori prima del claim. Cambio ruolo ricontrollato con IRQ mascherati e non impedisce il servizio del GET immediatamente successivo. |
| Applicazione anticipata confermata, alta | rdm.c RDM_Receive / INTERFACE_APPLY_CONFIGURATION, gateway, DNS | I setter potevano pubblicare le modifiche rete prima del TC ACK; ora lavoro transitorio RAM completato solo dopo TC verificato. Timeout TX annulla il lavoro e conserva i candidati per retry; un futuro TC DMX non può validare l’ACK abortito. |
| Formato errato confermato, medio | rdm.c PID 070A | SET gateway richiedeva erroneamente Interface ID=1. E1.37-2 richiede ID=0 con gateway; correzione anche per rimozione route (tutti zero) e GET senza route (ID0). Richiesta point-to-point non supportata: NACK ACTION_NOT_SUPPORTED. |
| Timing a rischio, da misurare | TIM7_IRQHandler, discovery | HAL GPIO e attesa IRQ TXE fra DE e primo byte potevano superare 4 µs. Come URC01, AF preparata prima di DE e primo byte scritto subito in TDR; restanti byte TXE, rilascio DE solo al TC. Nessun frame attivo viene interrotto per rispondere a un controller. |
| Presentazione controller confermata, medio | URC01/Core/Src/rdm_parameters.c | I PID 0700/03/05/06/09/0A/0B **erano già implementati e pubblicati** in ADSW100. Il catalogo standard URC01 esaminato non contiene i PID rete né i relativi editor strutturati; non basta annunciarli per far comparire un editor IPv4 su quel controller. Nessuna modifica a URC01; verificare con controller E1.37-2 o richieste raw correttamente formate. |

Hardware invariato: PB14 TX, PB15 RX, PD8 DE/RE, USART1 250 kbit/s 8N2. Porta unica half-duplex: durante TX il ricevitore USART è disabilitato. Non può garantire rilevamento/cattura dei primi byte di un controller che inizi durante il nostro frame. ADSW100 Node e URC01 controller sulla stessa coppia RS485 costituiscono due sorgenti, non una topologia RDM con proprietario unico. La cessione passiva riduce la contesa dopo rilevamento; non è arbitraggio multi-master.

Soluzione corretta per una linea con URC01 controller: ADSW100 in IN/responder. Per mantenere un’uscita Node continua e controllo RDM indipendente servono una porta separata o un gateway/controller proprietario della linea con gestione RDM coordinata. L’eventuale controllo via Ethernet è futuro, non implementato in questa fase. Nessuna risposta a costo di troncare il proprio frame DMX.

### Stati e completamento

| Stato RX | Ingresso / uscita verificabile |
| --- | --- |
| IDLE | Bus libero dalla transazione; restano guardie di linea e proprietà controller |
| START_CODE | BREAK FE con dato0; aspetta SC, non riavvia DMX durante MAB |
| RDM_RX | SC CC; accumulo massimo257 byte, fine esatta Message Length+2; checksum/PDL validati nel main |
| DMX_RX | SC0; protegge il frame fisico fino a 512 slot, prossimo BREAK o timeout |
| RDM_PENDING / PROCESSING | Pacchetto completo e copia main protetti; nessun TX autonomo, nessun salvataggio EEPROM |
| RDM_REPLY | Turnaround, BREAK/MAB o discovery, TXE; rilascio proprietà solo al vero TC dell’ultimo slot o watchdog guasto |
| OTHER_REPLY | Richiesta senza nostra risposta: ascolto di un altro responder; risposta normale completa o timeout di sicurezza |

La ricezione di errore UART/lunghezza impossibile libera lo stato; nuovo BREAK può iniziare un nuovo pacchetto. Watchdog RX: circa2,2 ms tra byte RDM (slot incluso), 35 ms dal SC per pacchetto, 2002 ms attesa SC per BREAK/MAB estesi, 1252 ms per DMX incompleto, 6 ms per risposta altrui non iniziata/completata. Sono limiti di recupero, **non finestre usate per decidere il normale completamento**. Il pacchetto pending non servito entro1,8 ms scade senza mantenere bloccato il Node indefinitamente.

Guardia E1.20: budget main1,8 ms, turnaround nominale200 µs dal timestamp RXNE (margine sul minimo176 µs e massimo2 ms); BREAK176 µs, MAB16 µs. La temporizzazione residua viene ricalcolata dopo copia TX. RXNE può precedere il secondo stop bit: il margine resta da misurare sulla scheda. Nessuna HAL_Delay o attesa bloccante nel coordinamento.

La presenza controller resta distinta dalla transazione: come DRV_STG4, attività RX recente rinvia il TX, DMX completo o RDM valido confermano un altro controller e rinnovano il lease di silenzio2 s. Il TC conclude la transazione; il lease evita di contendere immediatamente il bus al controller ancora attivo. Finestre Node2/20 ms, avvio2 s, cache50 ms e parser/arbitraggio Art-Net/sACN invariati. Il lease può sospendere il refresh autonomo: è cessione del bus a un’altra sorgente, non refresh garantito con due controller.

Confronti: DRV_STG4 `dmx.c` (Claim, rx_guard, ExpireReceiver, ControllerFrame, TC); URC01 `rdm_device_phy.c` (lock, deadline1800 µs, separazione TXE/TC, DE→TDR discovery); RSH7 `rdm.c` (buffer/framing e proxy, non importati router o scanner). Requisiti timing dal [testo E1.20-2010, §3.2](https://getdlight.com/media/kunena/attachments/42/ANSI_E1-20_2010.pdf); confronto con audit timing2025 già presente nel riferimento RSH7. Nessuna dichiarazione di certificazione complessiva o misura fisica dell’incremento.

### PID rete verificati e procedura

E1.37-2 pubblicata2015: [specifica primaria ESTA](https://tsp.esta.org/tsp/documents/docs/ESTA_E1-37-2_2015.pdf), §3.5, §4.1–4.12 e tabella A-1. I soli complementari condizionatamente obbligatori, LIST_INTERFACES e APPLY se si supportano SET DHCP/statico, sono già presenti. Confermati anche opzionali INTERFACE_LABEL0701 e HARDWARE_ADDRESS_TYPE1 0702. Non pubblicati Zeroconf/renew/release/hostname/domain che il firmware non implementa.

- 0700 GET PDL0 → lista ID interfaccia uint32 BE + tipo ARP uint16 BE; Ethernet ID1, tipo1.
- 0701/0702 GET ID4 → ID4 + label8 / MAC6.
- 0703 GET ID4 → ID4+bool; SET ID4+bool, valore0/1.
- 0705 GET ID4 → ID4+IPv4(4)+prefix(1)+stato DHCP(1), dal netif runtime.
- 0706 GET ID4 → ID4+IPv4(4)+prefix(1); SET identico9 byte, prefix0..32. In DHCP senza candidato, GET statico restituisce indirizzo/prefix zero; il vecchio IP ricordato nel payload legacy non viene cancellato implicitamente.
- 0709 SET ID4, ACK PDL0. DHCP/statico restano candidati RAM fino ad APPLY; pubblicazione con setter condivisi dopo il TC dell’ACK.
- 070A GET PDL0 → ID4+gateway4; SET ID0+gateway4, dopo TC. Route assente: entrambi zero. Ethernet point-to-point non disponibile (NACK000B); ID non valido NACK0006.
- 070B GET indice1 → indice+IPv4(4); SET5 byte, dopo TC. Due server legacy, indici0/1; terzo server non supportato, NACK0006.

Rifiuto PDL errato con FORMAT_ERROR0001; range/ID invalidi DATA_OUT_OF_RANGE0006; classe non supportata0005. IPv4 in ordine rete sul filo e rappresentazione lwIP/EEPROM legacy conservata; le sole copie aggiunte sono candidati e lavoro ACK transitori RAM, non una seconda configurazione persistente. Configurazione gateway/DNS riutilizza i valori statici del pannello; con DHCP attivo i parametri forniti dal lease restano gestiti dal client DHCP esistente.

L’unico selettore legacy DHCP/statico non rappresenta due preferenze indipendenti contemporaneamente: uno STATIC_ADDRESS non nullo seleziona statico dopo APPLY. Per passare a DHCP: SET STATIC_ADDRESS a0/prefix0, SET DHCP_MODE=1, SET APPLY, poi CURRENT_ADDRESS. APPLY con DHCP=1 e statico attivo non nullo restituisce ACTION_NOT_SUPPORTED000B conservando i candidati, senza reinterpretare EEPROM o inventare un flag nuovo. Questo è un limite esplicito del profilo implementato; non si dichiara copertura completa E1.37-2.

Dopo TC, il primo GET completa la pubblicazione pendente prima di leggerla (anche senza un giro intermedio RDM_Task). CURRENT_ADDRESS descrive comunque il netif: DHCP può richiedere tempo per il lease. Save EEPROM aggregato dopo250 ms di quiete e solo senza transazione RX/TX attiva; niente EEPROM prima dell’ACK, niente nuove scritture ISR. Il salvataggio legacy resta sincrono nel main e può ritardare una richiesta che inizi durante I²C; non è stato convertito in una nuova struttura asincrona. Power-cut prima del save può perdere l’ultima modifica.

### Prove effettive

`python3 tests/test_stabilization.py`: dieci gruppi PASS. Nel gruppo NODE_RDM: 72 GET/SET consecutivi via ISR UART nei tre modi (36 SET/36 GET), SET→GET immediato; sei transazioni aggiuntive SET ruolo→GET prima della commutazione fisica; DMX Node prima/dopo; BREAK/SC protetti, timer durante pacchetto pending, TC come completamento; timeout/inter-byte, lunghezza impossibile, checksum errato, UART ORE, pending scaduto e nuovo BREAK prima del servizio main. Verificati annuncio di tutti i PID rete, PDL/byte order/NACK, staged statico/DHCP, ACK prima dell’applicazione, ACK abortito/retry e round-trip EEPROM statico non predefinito e DHCP.

Build ARM completa: 150 unità C/ASM, nessun warning/errore; ELF/map/log in `/private/tmp/adsw100-rdm-transactions`. Comando `python3 tests/test_stabilization.py --arm-build /tmp/adsw100-rdm-transactions`. Simulazione non misura baud, jitter, slot, GPIO o simultaneità APB/NVIC. Restano prove hardware URC01 con GET/SET ripetuti, misura DE/RE/TC/turnaround e refresh, primo byte durante cessione bus, stress superloop/I²C e power-cut. Nessun commit/tag automatico, nessuna modifica ai firmware di riferimento.

## Incremento corrente — RDM e DMX Node (14 novembre 2026)

Questa sezione aggiorna le evidenze storiche riportate sotto. Branding/EEPROM precedenti collaudati dall’utente e congelati in `1e71467`. Incremento corrente compilato e verificato con simulazione host; non programmato né misurato su hardware dall’agente. Nessun commit/tag effettuato.

### Correzione successiva al primo collaudo RDM/Node

L’utente segnala DMX IN funzionante, mancata discovery da URC01 e reset ripetuti entrando in ArtNet Node. Il log conferma EUI-48 fisico `FC:0F:E7:16:45:70`: l’OUI è già ammesso e l’UID previsto è `17FA:E7164570`. Il log fornito non contiene Reset flags: **non è ancora confermato** se il riavvio sia IWDG, HardFault o altra causa.

Difetti confermati nel codice precedente, corretti in `dmx_512.c`:

- **Alto, TimerArm/TIM7_IRQHandler:** il riarmo scriveva EGR.UG con URS=0 e UIE rimasto abilitato dal periodo precedente. UG può richiedere un interrupt software; azzerare SR dopo UG non garantisce la cancellazione di una richiesta già pendente nel NVIC. Inoltre l’handler avanzava la macchina TX senza verificare UIF/UIE. Possibili scadenze anticipate, risposta discovery interrotta e catena di IRQ che affama il main. La documentazione ST inclusa in `Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_tim.h` (LL_TIM_SetUpdateSource) distingue esplicitamente UG/overflow con URS=0 da solo overflow con URS=1. Ora TimerStop/TimerArm mascherano UIE, usano URS, cancellano SR e pending NVIC con barriera; l’handler ignora richieste senza UIF/UIE. TC cancella anche il timer di sorveglianza precedente.
- **Alto, DMX512UARTInterrupt:** in TX il ramo scartava RDR e ritornava prima di cancellare FE/NE/ORE. Un errore latched può riattivare continuamente USART1 e impedire il refresh watchdog. Ora gli errori sono cancellati anche nel ramo TX, senza interpretare eco/disturbi come nuovi frame.

Questi difetti sono compatibili con i sintomi, ma il loro ruolo causale sulla scheda va confermato riprogrammando e ricontrollando reset/forme d’onda. Nessuna modifica a watchdog, clock, EEPROM, parser di rete o durata impulsi relè.

Aggiunta query CDC di sola lettura **`rdm?`**, disponibile anche dopo i 30 s di fabbrica: `rdm=1 uid=17FA:E7164570 rx=N late=N`. `rdm=0` segnala responder disabilitato; `rx` conta i pacchetti RDM completi e validati nel main, anche non destinati a questo UID; `late` conta quelli scartati oltre la finestra di servizio di 2 ms con responder abilitato e TX libero. Non sono contatori dei singoli byte UART, né una prova di ricezione sul controller. Query senza I²C o scritture EEPROM. Se `rx=0` dopo discovery, verificare percorso RX/framing; se `late` cresce, misurare latenza superloop; con `rx>0` e `late=0` verificare risposta fisica DE/TX e discovery.

Test aggiunti e superati: richiesta TIM7 spuria durante turnaround non avanza lo stato, URS attivo, timer fermato al TC, FE/NE/ORE durante TX cancellati senza aborto, diagnostica UID e timeout, query CDC dopo lock fabbrica. I dieci gruppi host restano PASS. La simulazione non emula integralmente il bus APB/NVIC: verifica le guardie e il recupero, non riproduce una misura degli impulsi hardware. Build ARM correttiva: `/private/tmp/adsw100-node-rdm-fix/ADSW100.elf`, 150 unità; collaudo fisico della correzione pendente.

### Architettura e riutilizzo

| File/funzioni | Implementazione corrente e provenienza |
| --- | --- |
| `dmx_512.c`: InitDMX512, DMX512UARTInterrupt, DMX512_Service | Unico proprietario USART1 RX/TX. Conservati staging/pending e snapshot DMX precedente. Aggiunta raccolta pacchetti RDM (massimo 257 byte), decodificati nel main. Nessun DMA concorrente. |
| `dmx_512.c`: TIM7_IRQHandler, DMX512_SendPacket, NodeTryStart | TX interrupt TXE/TC, BREAK GPIO 176 µs, MAB 16 µs, slot UART 250000 8N2 (44 µs), massimo 513 byte. TIM7 one-shot a 1 MHz dal clock APB1 timer 240 MHz; TIM6 e servizio relè invariati. Sorveglianza TX 30 ms: timeout rilascia DE e ripristina RX. Nessuna HAL_Delay nel nuovo driver TX. |
| `rdm.c/h`: RDM_Init, RDM_Receive, RDM_Task | Unico nuovo modulo runtime, necessario per separare PID/discovery dalla macchina hardware. Framing/discovery e UID ripresi concettualmente da RSH7/DRV_STG4; non importati router, controller o updater. Salvataggio EEPROM fuori ISR. |
| `cfg_io.c/h`: CFG_GetValue, CFG_SetValue | Accesso comune validato a indirizzo, ruolo porta, modalità legacy, due universi, trasporto/priorità, ordine Auto, display/OFF Time e rete. Valori IPv4 nella rappresentazione lwIP già esistente; protocolli RDM serializzati nell’ordine di rete. API predisposte per web senza nuova logica o campi duplicati. |
| `user_interface.c`: handle_page_dmx_event, UI_DmxParameter | Tre righe Address, Mode (IN/ArtNet Node/sACN Node), Universe. In IN Universe è inattivo. Editing su candidato locale: annullamento senza effetti; conferma valida e salva, errore EEPROM ripristina RAM. Navigazione generale e codice menu invariati. |
| `relay_driver.c`: selezione RelayTask | Node Art-Net/sACN seleziona per i 12 relè la stessa sorgente del TX, rispettando Address, soglie, OFF Time, impulsi e guardia esistenti. IN mantiene il selettore legacy Auto/Art-Net/sACN/DMX. |
| `eeprom.c`: EE_LoadIdentity, EE_LoadConfig, EE_StoreConfig | Struttura 3, solo ruolo marcato B0/B1/B2 a 0x27; migrazione da struttura 2 a IN. Dettagli e fault injection in EEPROM_MIGRATION.md. |
| `main.c`, `h_files.h` | Integrazione init RDM e servizio DMX/RDM nel superloop, dentro USER CODE nei file generati. Nessuna modifica IOC, linker o librerie vendor. |

Pin confermati **dal codice generato**, non dal cablaggio misurato: PB14 USART1_TX AF4, PB15 USART1_RX, PD8 RDM_DIR; basso RX, alto TX secondo implementazione. UART1 250 kbit/s, 8N2, clock periferica 120 MHz. TIM7 risultava libero nel progetto (vettore weak in startup); ora configurato dall’applicazione. La rigenerazione CubeMX non deve assegnarlo ad altri servizi.

Riferimento prioritario direzione/finestre: `FIRMWARE_DRV_STG4/Core/Src/dmx.c` (TXE/TC, BREAK/MAB, ascolto iniziale, finestre 2/20 ms, pausa su controller). RSH7: `RDM_SPLITTER_H7`, incluso ramo `origin/Uploading-firmware-da-DMX`; URC01: confronto driver e futura compatibilità hardware updater, **nessun protocollo update implementato**. L’hardware e i timer specifici dei riferimenti non sono stati copiati senza adattamento.

### Art-Net/sACN Node e assenza sorgente

Non sono stati duplicati o modificati i parser. Il TX legge esclusivamente ArtNet_GetSlotsView/sACN_GetSlotsView, quindi mantiene universo, merge, sequence, priorità e arbitraggio già presenti. I due universi EEPROM restano quelli originali, rispettivamente 0..32767 e 1..63999. La modalità sorgenti legacy mantiene significato e valore: il nuovo ruolo porta è indipendente.

Trasmissione SC=0 + 512 slot, coda mancante a zero. Cache pubblicata dal main e copiata stabilmente all’avvio frame. Sorgente non disponibile: zero al frame successivo dopo la scadenza prevista dai parser (Art-Net 3 s, sACN 2,5 s). Se il main smette di aggiornare la cache, TX autonomo passa a zero oltre 50 ms; non conserva livelli indefinitamente. I relè seguono la sorgente scelta con OFF Time esistente, senza tagliare gli impulsi durante le transizioni.

Da TC viene rilasciato DE; ascolto ordinario 2 ms, 20 ms ogni 32 trasmissioni. Periodo teorico ordinario circa 24,8 ms (~40 Hz), frame con finestra lunga circa 42,8 ms: **calcolo**, non misura. All’ingresso Node ascolto iniziale 2 s. Attività RX recente rinvia il TX di 30 ms; DMX qualificato temporalmente o pacchetto RDM valido osservato sospende TX autonomo per 2 s. Linea bassa/errori UART/byte pendenti impediscono un nuovo BREAK. Passaggio IN/OUT differito fino a fine TX (TC), poi pulizia buffer e cambio ruolo senza reset.

La RS485 è half-duplex: durante TX RE è disabilitato. Traffico RDM viene ricevuto/rilevato **nelle finestre RX**, anche in Node; i messaggi diretti al proprio UID usano lo stesso responder. Non c’è monitoraggio durante il proprio TX, controller RDM, discovery di altri dispositivi o garanzia anticollisione tra sorgenti asincrone. Due controller permanenti sullo stesso link non sono una topologia supportata; un altro controller rilevato ha precedenza tramite sospensione. Le finestre non degradano il refresh per il solo fatto di essere abilitate; traffico di un altro controller può sospenderlo intenzionalmente.

### RDM: identità, comandi e PID

Manufacturer ID comune **0x17FA**, già autorizzato per tutti i brand. UID = 17FA + ultimi quattro byte EUI-48 fisico, con whitelist dei 15 OUI verificati nel riferimento DRV_STG4 (ultimo byte OUI distinto). Preserva seriale e discriminante OUI, senza hash. Errore I²C/OUI non riconosciuto: RDM disabilitato, DMX/Node restano utilizzabili; nessun UID fittizio. Da censire unicità nell’intero parco AXN e BOM E48/E64.

Model ID numerico **0xFFFF è un placeholder non assegnato**, non un identificativo inventato presentato come registrato. Nei riferimenti esaminati compaiono RSH7 modello 0x0001, URC01 0x0100, DRV_STG4 0x005E, nel dominio storico 0x0206. Ottenere l’assegnazione ADSW100/NODIM01 dal registro completo manufacturer 17FA prima del rilascio. Model description/manufacturer label provengono da Brand_Current, non da stringhe replicate. OEM Art-Net non assegnato: nessuna sostituzione con quello RSH7.

Discovery UNIQUE_BRANCH/MUTE/UNMUTE, risposta discovery senza BREAK; risposte normali ACK/NACK con checksum, PDL, transaction e sub-device verificati. Root device; SET broadcast senza risposta, GET broadcast ignorato; sub-device inesistenti NACK. Turnaround pianificato a 200 µs dal timestamp ultimo byte (margine sul minimo), richieste già oltre 2 ms scartate prima di applicare SET. Questo protegge da risposte tardive, ma non garantisce che il main serva ogni richiesta sotto carico: misure obbligatorie.

| PID standard | Accesso e formato dati principali |
| --- | --- |
| 0x0050 SUPPORTED_PARAMETERS | GET, lista PID uint16 BE |
| 0x0051 PARAMETER_DESCRIPTION | GET con PID uint16; descrizione completa dei proprietari, tipo/range/default/unità |
| 0x0060 DEVICE_INFO | GET, 19 byte; protocollo 1.0, footprint 12, categoria power control 0x0601, una personality |
| 0x0080 / 0x0081 | GET model description / manufacturer label centralizzati |
| 0x0082 DEVICE_LABEL | GET/SET, massimo 32 caratteri ASCII; **volatile**, default modello al boot: nessun campo/stringa EEPROM aggiunto |
| 0x00C0 | GET software version label |
| 0x00E0 / 0x00E1 | Personality unica / descrizione; GET e SET=1 per E0 |
| 0x00F0 DMX_START_ADDRESS | GET/SET uint16 BE, 1..512 come legacy; indirizzi oltre 501 riducono gli slot disponibili ai 12 relè |
| 0x1000 IDENTIFY_DEVICE | GET/SET bool; lampeggio retroilluminazione, nessun azionamento dei relè |
| 0x0700 / 0x0701 / 0x0702 | Lista interfacce / label / MAC: Ethernet ID=1 uint32 BE, tipo hardware ARP Ethernet=1 |
| 0x0703 IPV4_DHCP_MODE | GET richiesta ID4, risposta ID4+bool; SET ID4+bool |
| 0x0705 IPV4_CURRENT_ADDRESS | GET ID4; risposta ID4+IPv4(4)+prefix(1)+stato DHCP(1) |
| 0x0706 IPV4_STATIC_ADDRESS | GET ID4; risposta/SET ID4+IPv4(4)+prefix 0..32; netmask derivata senza campo duplicato |
| 0x0709 INTERFACE_APPLY_CONFIGURATION | SET ID4; GET NACK unsupported command class |
| 0x070A IPV4_DEFAULT_ROUTE | GET vuoto, risposta/SET ID4+gateway IPv4(4) |
| 0x070B DNS_IPV4_NAME_SERVER | GET indice 0/1; risposta/SET indice+IPv4(4) |

PID proprietari assegnati internamente al prodotto, dopo ricerca nei riferimenti esaminati: 0x9100..0x9108 non riscontrati; non sono registrazioni ESTA. RSH7 0x8001..03 e DRV_STG4 0x82xx/0xE1xx non sono riutilizzati con significato diverso. Tutti GET (PDL0) e SET (dimensione indicata), numeri multibyte BE:

| PID | Parametro condiviso | Byte/range |
| --- | --- | --- |
| 0x9100 | DMX Mode/ruolo porta | 1: IN=0, ArtNet Node=1, sACN Node=2 |
| 0x9101 | Mode sorgenti legacy | 1: DMX=0, Art-Net=1, sACN=2, Auto=3; attivo in IN |
| 0x9102 | Art-Net Universe | 2: 0..32767 |
| 0x9103 | sACN Universe | 2: 1..63999 |
| 0x9104 | sACN Transport | 1: multicast=0, unicast=1 |
| 0x9105 | Soglia minima priorità sACN | 1: 0..200; non è una priorità TX o l’ordine Auto |
| 0x9106 | Ordine protocolli Auto | 1: 0..5, stessa permutazione legacy |
| 0x9107 | Display Auto OFF | 1: 0/1, inverso del campo legacy backlight |
| 0x9108 | Relay OFF Time | 1: 0..30 secondi, stessa semantica legacy |

SET applica i parametri condivisi attraverso CFG_SetValue/CFG_ValidateValue. Eccezione richiesta da E1.37-2: SET DHCP e indirizzo statico/netmask restano candidati solo RAM nel responder; GET restituisce il candidato, CURRENT_ADDRESS continua a leggere il netif attivo. Soltanto SET 0x0709 trasferisce i candidati attraverso il setter comune e ne richiede la persistenza. Non ci sono campi EEPROM o configurazioni web duplicati. I candidati non applicati si perdono al reboot. Lo stato DHCP corrente viene da dhcp_supplied_address, non dal solo flag desiderato. Il task rete esistente applica poi DHCP/statico/DNS e membership/universi. Non si dichiara implementazione completa o certificazione E1.37-2. Brand/policy menu non sono esposti da RDM.

Persistenza aggregata da RDM_Task dopo almeno 250 ms dall’ultimo cambiamento e ultimo pacchetto RDM osservato, fuori TX/ISR, retry in caso errore. ACK significa accettazione in RAM; power-cut prima del save può perdere la modifica. Il salvataggio I²C mantiene timeout/read-back preesistenti ed è **bloccante nel main**: TX/TIM6 continuano via IRQ, ma richieste RDM sopraggiunte durante I²C possono scadere. Traffico RDM continuo può posticipare il save. Device label e IDENTIFY non sono persistenti.

Riferimenti primari per identificativi/formati e vincolo half-duplex: [E1.20 ESTA](https://tsp.esta.org/tsp/documents/docs/E1-20RDM_2006.pdf), [E1.11 2024 ESTA](https://tsp.esta.org/tsp/documents/docs/ANSI%20E1.11%20-%202024.pdf), [specifica E1.37-2 pubblicata sul sito ESTA (bozza R202x)](https://tsp.esta.org/tsp/documents/docs/E1-37-2%20-%202015%20%28R202x%29.pdf), [enum RDM OLA](https://raw.githubusercontent.com/OpenLightingProject/ola/master/include/ola/rdm/RDMEnums.h). I sorgenti di riferimento non sostituiscono la misura di conformità.

### Criticità residue e classificazione

| Stato / gravità | File/funzione | Conseguenza e verifica/correzione |
| --- | --- | --- |
| Confermato, limite funzionale | rdm.c RDM_Task, eeprom.c EE_Write | I²C sincrono può ritardare responder. Nessuna promessa di ricezione RDM completa sotto ogni carico; misurare latenza e fault bus, valutare save a piccoli passi se necessario. TX Node e impulso relè restano in IRQ. |
| Rischio, alto | dmx_512.c ISR/SendPacket | Durata sezioni critiche e latenza main non misurate; verificare slot 44 µs e turnaround sotto display/NFC/USB/Ethernet. Richieste tardive vengono scartate. |
| Verifica HW, alto | PD8 DE/RE, PB14/PB15, TIM7 | Confermare cablaggio RE/DE, livelli e tempo rilascio bus al TC; misurare BREAK/MAB, baud/stop, refresh e assenza impulsi spurii in cambio GPIO/AF. |
| Limite fisico, alto | NodeTryStart/finestre RX | Nessun ascolto durante TX né arbitraggio multi-controller garantito. Testare sorgente esterna che si attiva durante Node; sospensione solo dopo rilevamento. |
| Da verificare, medio | rdm.c RDM_Init / rdm.h Model ID | Censire tutti gli UID/OUI fisici sotto 17FA e assegnare Model ID; con EUI non riconosciuto RDM resta disabilitato. |
| Limite confermato, medio | RDM_Task/DEVICE_LABEL | Save differito non atomico multi-parametro e label volatile. Documentare al controller e testare power-cut reali; non aggiungere CRC/slot/stringhe EEPROM non autorizzati. |

### Test effettivamente eseguiti

- `python3 tests/test_stabilization.py`: **10 gruppi PASS** (ART, SAC, RELAY, CFG, DMX, EE, BRAND_EE, UI, CDC, NODE_RDM).
- Nuovo harness compila i sorgenti reali EEPROM/config/driver/RDM/relè: discovery/mute/unmute; checksum, GET/SET, NACK PDL/range/PID/command class; richieste tardive e broadcast GET; IDENTIFY e label; descrizioni e round-trip dei nove PID proprietari; IPv4/prefix invalido/DNS, DHCP/IP statico non attivi fino ad APPLY, tipo ARP Ethernet e stato DHCP corrente; identità dei tre brand, UID valido/non riconosciuto.
- Simulati IN→ArtNet Node→sACN Node→IN senza reset, ricezione fisica DMX dopo TX, risposta RDM nella finestra Node e pausa su controller, TC/DE e timeout TX, livelli/blackout da sorgente assente e main fermo, selezione relè concorde col Node, migrazione v2→v3 con errori I²C/riavvio, persistenza e ruolo invalido, editing/conferma/cancel pannello.
- Build ARM diretta GCC CubeIDE, **150 unità C/ASM**, ELF/map/log in `/private/tmp/adsw100-node-rdm`; non build Eclipse né programmazione. Log compiler/linker senza warning/errori. Size ELF: text 199652, data 640, bss 71347 byte (non misura picco stack/heap). Riproduzione: `python3 tests/test_stabilization.py --arm-build /tmp/adsw100-node-rdm`.
- **Non eseguiti su hardware**: analizzatore DMX/RDM, discovery con controller reale, misura refresh/jitter/turnaround/DE/RE, traffico concorrente, power-cut EEPROM, display/relè/NFC/rete sotto stress. Nessuna dichiarazione di collaudo fisico dell’incremento.

## Prima fase funzionale: branding, EEPROM, accesso menu e CDC

Versione firmware lasciata 1.4.0 durante lo sviluppo, nessun rilascio/tag definitivo 2.0.0. Base congelata `0b5fa6c` (480 MHz e stabilizzazione), programmata e verificata dall'utente su DMX, Art-Net e sACN. La nuova fase ha build/test software, non ancora collaudo hardware.

- Tabella unica in cfg_io.c esposta da defs.h: AXN System/NODIM01, Videlio HMS/ADSW100, Generic/NODIM01. ESTA 0x17FA comune su istruzione utente. OEM RSH7 0x2F3F verificato specifico RSH7, non riutilizzato: placeholder 0xFFFF mantenuto. RDM/web restano futuri.
- Migrazione legacy valida ai soli tre metadati (versione 0x02, brand 0x25, policy 0x26), commit versione per ultimo/read-back. Legacy Videlio con codice ON, secondo conferma dell'utente sulle unità installate. La copia NO_CODE locale non è rappresentativa di quelle unità e non guida più la migrazione. Nuova init AXN/codice OFF. Generic vincolato a OFF.
- Brand/policy **solo CDC nei primi 30 s dal boot**. Sintassi RSH7 `brand?`, `brand=<id>` e forma equivalente ADSW100 `menu_code?`, `menu_code=<0|1>`. `brand=` applica default AXN 0, Videlio 1, Generic 0; AXN/Videlio permettono override. Risposte `OK REBOOT`, `ERROR`, `LOCKED`; query dopo scadenza consentite. Persistenza applicata al reboot, non cambio live.
- usb_debug.c: RX IRQ con riga atomica e scarto overflow; servizio main non bloccante, I²C fuori ISR, TX persistente con retry BUSY e priorità sui log. Deadline assoluta HAL tick <30000, latched dopo scadenza e indipendente da enumerazione/reconnect. Main integra USB_ServiceTask in USER CODE. Il provisioning FGM precedente resta separato; non è stato implementato un bootloader.
- user_interface.c: home dinamica con manufacturer/modello; System Setup ripristinato ai quattro campi originali senza possibilità di edit brand/codice. Il reset dal pannello preserva i metadati tramite EE_ResetUserMemory/HardFactoryReset(true), mentre l'init nuova è riservata al percorso CDC preesistente. Menu con codice invariato 1268 quando ON.
- EE_SaveIdentity non ricarica la configurazione applicativa né cambia RAM attiva: legge/salva i soli metadati. Nessun nuovo campo o slot, nessun CRC. Normal save e reset utente non sovrascrivono impostazioni di fabbrica. Specifica e limiti in [EEPROM_MIGRATION.md](EEPROM_MIGRATION.md).

Nove gruppi host: sei regressioni più BRAND_EE_TEST, UI_TEST, CDC_TEST. Coperti 256 valori legacy di versione, tuple non valide, conservazione byte per byte, errori I²C, migrazione idempotente, policy/default, reset senza alterazione identità, codice corretto/errato, comandi reali con mock USB, bordo 29999/30000 ms, frammentazione, overflow/NUL, BUSY, reconnect e wrap. Build ARM completa diretta: 149 unità, zero errori/warning. Comandi nel runner test. Nessuna certificazione fisica USB o power-cut: collaudare provisioning CDC a freddo, reset e pannello su hardware.

Parser DMX/Art-Net/sACN, merge/timeout, rete e relè non modificati; Art-Net cambia soltanto descrittori branding. NFC e IOC invariati. Nessun nuovo modulo runtime, commit o tag. Le sezioni seguenti sono lo storico e non annullano le decisioni correnti.

## Storico: rigenerazione e stabilizzazione della baseline

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
