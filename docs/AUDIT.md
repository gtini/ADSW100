# Audit ADSW100 prima della v2.0.0

## Diagnosi sul banco dopo Init Memory

Il log utente riporta `FOREIGN_OR_INVALID`, indirizzo confermato 02, capacità
888, revisione 4, stato C3 e 150 scansioni. Lo stato osservato è un edit locale,
mentre l'intento attivo per ricostruire il tag è E8. Il test host riproduce
lo stesso esito con un documento sperimentale proprio se un salvataggio
avviene dopo Init e prima della pubblicazione. Il tag e i nuovi valori
persistenti sono preservati. Un Init successivo in questo test, senza edit
prima di SYNCED, pubblica correttamente il nuovo SNAPSHOT. L'utente ha poi confermato di avere modificato soltanto lo spegnimento
automatico del display appena terminato Init Memory. La CDC ha mostrato
`ee writes=4 addr=22 len=1 rc=0 reserved=0 transaction=0 local=1 meta_rc=1 meta=E8`
mentre `nfc?` riportava C3 e revisione 4. L'offset 22 è il byte backlight.
Questo conferma che il salvataggio è partito da E8 ed è riuscito, revocando
l'intento prima della pubblicazione. Il contenuto effettivo del tag e la
riuscita della successiva pubblicazione restano da verificare.

La nuova diagnostica `tag=` in `nfc?` distingue documento precedente proprio
da contenuto non riconosciuto e da SNAPSHOT/REQUEST del contratto corrente.
L'esito proviene dall'ultima scansione stabile. La diagnostica non effettua
scritture e l'autorizzazione alla rigenerazione resta quella esistente.
La lettura CDC è stata eseguita dopo aver chiuso il terminale. Il riavvio
del listener sACN appartiene al servizio di rete e viene analizzato
separatamente da questo esito del parser NFC.

Seconda prova senza modifiche dopo Init. La CDC riporta `INIT_FOREIGN_OR_EDIT`,
`rev=5 state=E8`, 72–77 scansioni, `ee writes=0` dopo il riavvio,
`magic=A55A cfg=0`, 2 NACK storici e zero timeout. Il precedente passaggio
a C3 è escluso in questa prova. Resta da distinguere riconoscimento del
contenuto e confronto fra RAM ed EEPROM. La nuova build Release diagnostica
(`/private/tmp/adsw100-nfc-initdiag/Release/ADSW100.hex`) separa
`INIT_UNRECOGNIZED` e `INIT_CONFIG_MISMATCH` e mostra il punto di rifiuto
dell'analisi NDEF in `detail=`. Entrambi i percorsi testati su host senza
scritture al tag, build ARM senza warning. La CDC URC01 era occupata e
la sonda non risultava collegata. Nessuna nuova programmazione eseguita.

Acquisizione successiva con ST-LINK in hot-plug. Il vettore in flash
`2407FFE0 0800B9F9` corrisponde alla Release locale. I due buffer
`nfc_memory` e `nfc_previous` coincidono per tutti gli 888 byte User.
Il CC è `E1 10 EF 00`. Tutti i byte User sono zero salvo il blocco
all'offset `0x30`, che contiene `03 CA D2 25 A2` seguito da
`application`. Il TLV dichiara 202 byte. MIME dichiarato 37 byte e
payload dichiarato 162 byte, ma i byte successivi al frammento e il
terminatore atteso sono zero. L'assenza del terminatore fa fallire
`NfcOwnedContent` prima del confronto RAM ed EEPROM. La lettura conferma
lo stato di NDEF troncato, senza dimostrare quale operazione passata
l'abbia prodotto. Il valore coincide con l'anomalia MIME già riportata
durante una precedente lettura.

Dump conservato in `/private/tmp/adsw100-nfc-bank/ntag-user-sector0.bin`,
CRC32 `7FEA539C`. E8 rimane pendente. La riparazione proposta consiste
nel cancellare soltanto il blocco User I²C `0x04` verificato, dopo consenso
esplicito, e lasciare che il servizio pubblichi il nuovo SNAPSHOT dalla
configurazione verificata. Nessun tentativo di riscrittura è stato eseguito.

### Correzione Init per il frammento verificato

L'operatore ha autorizzato la sostituzione del frammento acquisito con il
documento applicativo. `NfcInitKnownFragment` è invocata soltanto nel percorso
Init con consenso E8 persistente, dopo doppia scansione e controlli hardware.
Richiede indirizzo confermabile 02, CC E110EF00, i 16 byte esatti
all'offset User 30h e zero in tutti gli altri byte User. Riconosce soltanto
questa forma e non ne importa valori. Il writer esistente pubblica lo
SNAPSHOT dalla configurazione verificata, controlla tutti i blocchi riletti
e consuma E8 solo dopo il readback. Il riavvio post-flash può riprendere
il consenso E8 già presente, senza un nuovo Init Memory.

Il riferimento DRV_STG4, `Core/Src/nfc.c:InitDocumentAllowed` e
`docs/NFC_INIT_RIGENERAZIONE.md`, conferma la sequenza di consenso, guardie
RF e readback. Il percorso ordinario DRV richiede il MIME completo oppure
una prova nel journal. ADSW100 conserva il proprio driver e non introduce
un journal per questo frammento, la cui riscrittura è stata autorizzata
in modo specifico.

Test host sul dump esatto con E8, senza E8, con un byte diverso o dati
aggiuntivi, CC differente e AUTH0 attivo. In presenza RF il writer aspetta;
dopo successo restano identici i 40 byte legacy, il blocco CC e i blocchi
a partire da 38h. Il riavvio a consenso consumato non riscrive. Build ARM
Debug/Release senza warning e regressioni host. Artefatto Release
`/private/tmp/adsw100-nfc-init-repair/Release/ADSW100.hex`. Collaudo hardware
pendente e nessun flash eseguito dall'agente.

## Completamento SDCP2 e catalogo ST2FW v3 — 22 novembre 2026

**Riferimenti effettivi.** La copia Dropbox di URC01 è a `b7f9af0`. Il checkout
`~/micro/Studio Due/URC_100` è a `84c3eb2` e contiene `7c42965`, il codec
SDCP2 e il parser catalogo più recenti. Per DRV è stato letto il checkout
`~/micro/Studio Due/DRV_STG4` a `b3ba66f`, comprese le modifiche locali in
`Core/Src/nfc.c` e i documenti NFC di Init e revisione. Il riferimento
Hardware H7 resta il driver I²C ADSW100. I tre progetti di riferimento sono
stati consultati in sola lettura.

**Incompatibilità confermata.** ADSW100 emetteva SCHEMA_REF da 36 byte, mentre
DRV e il parser URC attuale usano solo la revisione BE32. `NfcBuild` e
`NfcParse` ora usano la sezione da 4 byte. L'identità 17FA:0060/rev1 resta
quella del catalogo ADSW100 già riconosciuto da URC01. Il contratto delle
15 istanze resta uguale. Il formato sperimentale viene respinto dal parser
delle richieste. Un Init Memory esplicito può sostituire un documento proprio
verificato per identità e CRC, incluso quello sperimentale o il JSON legacy
proprio. L'Init non importa valori dal tag. Tag estranei e protezioni restano
preservati. SNAPSHOT base 165 byte SDCP, 201 byte NDEF/TLV e 208 byte I²C allineati.

**Correzioni del servizio.** `NFC_Service` termina uno scambio pendente e
cede la porta al pannello e a RDM quando quest'ultimo deve salvare. La
pubblicazione si sospende se torna il campo RF. Nella stessa accensione
riprende solo dopo aver ripetuto le guardie NTAG/CC e confrontato l'intera
finestra User con l'immagine attesa, inclusi i blocchi già pubblicati.
Prima di ogni scrittura la revisione EEPROM viene nuovamente verificata.
I filler FF possono essere azzerati nei blocchi User completi, come richiesto
dal parser URC01 che esige padding a zero. Il blocco misto 38h, che contiene
anche lock e AUTH0, è escluso dalle scritture. Se la coda nel blocco misto
richiede modifiche, il servizio restituisce CAPACITY_ERROR. La finestra comune
è al massimo 888 byte letti e 880 byte scritti. Indirizzi 7 bit, riconoscimento
NTAG, sessione STOP/READ, rilascio di I2C_LOCKED, IRQ e timeout preesistenti.
Per la mappa fisica si veda [NXP NT3H2111/2211](https://www.nxp.com/docs/en/data-sheet/NT3H2111_2211.pdf).

**Persistenza e limiti.** Snapshot da EEPROM verificata, cfg condivisa,
preflight ACK, metadati B6 prima della scrittura, verifica dell'intero
candidato e commit prima dell'ACK. Il rollback RAM in errore conserva
anche la luminosità legacy raw. Una richiesta obsoleta non sovrascrive
l'edit locale. Il bootstrap di un tag vuoto legge la configurazione già
valida e non esegue Init Memory. Dopo power-cut durante pubblicazione,
un NDEF parziale di provenienza ambigua viene lasciato intatto. Rispetto
a DRV non si introduce un journal ausiliario sul tag. L'atomicità fisica
delle scritture EEPROM in-place non è garantita.

**Catalogo e packaging.** Il JSON ordinato è in
[protocol/nfc/catalogs/ADSW100.json](../protocol/nfc/catalogs/ADSW100.json).
L'unità secondi e il prefisso IPv4 usano i campi già condivisi con il
validatore Builder. Il modulo formato v3 di URC e quello RSH7 usato dal
Builder accettano il catalogo. Con un'immagine DRV originale e il relativo
JSON, entrambi verificano header da 96 byte, offset contigui, dimensioni,
CRC header/BIN/JSON, identità e preservazione byte-per-byte del BIN
anche con catalogo assente. Sul BIN ADSW100 entrambi restituiscono
«Descriptor magic errata», perché il firmware non implementa SDI1/SDC1.
La UI locale del Builder esaminato espone ancora prepare(source, model);
il modulo formato v3 ha già il parametro catalogo. Non è stato generato
alcun ST2FW ADSW100 programmabile. URC01 usa il catalogo su SD dal
contenitore ST2FW e al momento può leggere ADSW100 in RAW.

**Verifiche.** Suite ADSW ASan/UBSan con 19 vettori, snapshot, commit singoli
e multipli, 21 transazioni complete, ACK e replay, RF intermittente,
tag protetto/estraneo/legacy, timeout e guasti I²C, 170 iniezioni di errore,
68 tagli al byte per REQUEST e 24 per Init. Verificati reload dei valori
multi incluso backlight, OFF Time e rete, byte legacy non interessati e
cessione del bus a RDM. Il test indipendente usa `sdcp2.c`,
`nfc_catalog.c` e `crc32.c` reali di URC01 con ASan/UBSan. Confermati
parsing streaming, 15 istanze, REQUEST identica e ACK prodotto da ADSW.
Stabilizzazione host con test traffico Node, DMX, RDM, rete, relè, RTC
mock, USB e boot superati. Debug e Release ARM complete da 150 unità,
senza warning, vettore MSP 2407FFE0 verificato. Versione firmware invariata.
Nessun test RF o aggiornamento eseguito sul dispositivo in questa fase.

I passi di collaudo e gli artefatti sono in [NFC_PROTOCOL.md](NFC_PROTOCOL.md).

## DFU ROM9.1: guasto acquisito e workaround stack — 22 novembre 2026

**Aggiornamento rispetto all'analisi iniziale sotto:** l'utente programma con CubeProgrammer via **USB DFU del ROM ST**, non tramite il launch CubeIDE/SWD. Entra premendo BOOT, RESET con BOOT premuto, poi rilascia BOOT. Nessun bootloader custom è presente nel repository, ma il ROM ST fa parte del percorso reale. Il primo avvio è riuscito; una successiva programmazione, con DMX ed Ethernet collegati rispetto al tentativo precedente, lascia display vecchio, status LED fermo e nessuna CDC ADSW. Non si tratta in quella cattura della schermata applicativa Wait for boot.

### Evidenze hardware, senza flash/reset dell'agente

ST-LINK V2 collegata successivamente, target STM32H7 RevV, BL0x91. Connessione esplicita HOTPLUG. La lettura core comporta arresto transitorio; la CLI ripristina poi l'esecuzione, come mostra il successivo score. Nessun comando reset, go, erase, program, option-byte write o modifica EEPROM.

```text
PC=1FF0A594 (seconda cattura1FF0A480)  LR=1FF125AB
VTOR=1FF09800  MSP=24004470  R5=40011000
PRIMASK=0 BASEPRI=0 FAULTMASK=0 CONTROL=0
CFSR=0 HFSR=0
Flash08000000:24080000 0800B831 080098DD 080098DF
RCC.RSR=00460000 FLASH.BOOT_CUR=1FF00800
```

Il core è nel ROM, non nel firmware ADSW100. I56byte startup_diag dell'immagine effettiva a24003BAC sono zero. Questo dimostra lo stato attuale; non ricostruisce da solo tutti i passaggi precedenti al blocco. Flash/vettori corrispondono alla Release locale osservata, dimensione132024byte; copia ELF/HEX/MAP e hash conservati in `observed-release/` della cartella diagnostica Debug.

La lettura ROM attorno a PC conferma il ciclo a1FF0A592: lettura `[R5+0x1C]`, test bit15 e attesa. R5=USART1base40011000: si attende ABRF nel registro ISR, cioè completamento autobaud. I registri ROM USART1 mostrano CR1=140D, CR2=00500000 (autobaud abilitato), non la configurazione DMX dell'applicazione. Il traffico esterno DMX arriva proprio su PB15/USART1 e costituisce una condizione da isolare; Ethernet non è stata identificata come causa. Non si modifica il driver DMX per correggere codice che sta eseguendo in ROM.

### Difetto di compatibilità dell'immagine e correzione minima

[AN2606 Rev71, tabella146, pagina348](https://www.st.com/resource/en/application_note/an2606-stm32-microcontroller-system-memory-boot-mode-stmicroelectronics.pdf) documenta per ROM9.1 un problema del salto all'applicazione: MSP deve essere strettamente inferiore a fine RAM meno16byte. Il vettore letto sulla scheda è invece24080000, esattamente fine RAM_D1. È una non conformità verificata, indipendente dalle ipotesi su EEPROM/NFC.

Corretto soltanto il simbolo `_estack` in `STM32H743VITX_FLASH.ld`: fineRAM−32, cioè **2407FFE0**, allineato8 e strettamente sotto2407FFF0. Nessuna modifica di lunghezza RAM, heap/stack minimi, clock, reset o temporizzazione; newlib `_sbrk` usa già `_estack` per il limite heap. Nessuna modifica della mappa EEPROM o delle protezioni. Conservare il workaround se CubeIDE rigenera il linker; il nuovo controllo del vettore segnala la regressione.

Per evitare un confronto che cambi ottimizzazione o codice, gli stessi oggetti della Release realmente programmata sono stati rilinkati: con il vecchio linker il BIN coincide byte-per-byte con quello osservato. Con il linker corretto la lunghezza resta132024byte e cambiano **solo tre parole**: offset0,9A00,B86C, ciascuna da24080000 a2407FFE0 (vettore MSP e riferimenti al simbolo). Nessun opcode o parametro applicativo diverso.

Artefatto A/B: `/private/tmp/adsw100-boot-20261122-stackfix-release-ab/ADSW100.hex`, con ELF/BIN/MAP e verification.json. SHA-256 BIN `db9643d67a1e48eab47ae20428caae41513ccdf25013e18c4b47764b798bb761`. Nessun file della Release utente sovrascritto dall'agente.

### Test A/B con stack corretto: esito negativo, stesso blocco ROM

L'utente ha programmato manualmente lo HEX A/B con DMX ed Ethernet presenti e riferisce ancora blocco. Acquisizione successiva conferma in flash MSP2407FFE0/Reset0800B831: non è stato caricato per errore il vecchio vettore. VTOR resta1FF09800, startup_diag zero, PC1FF0A57E, R5USART1, MSPROM24004470; fault Cortex non evidenziati. Log `hardware-failure-01.log` nella cartella A/B. Il workaround documentato elimina una non conformità ma **non risolve da solo il guasto attuale** e non va presentato come causa unica dimostrata.

Scollegare soltanto DMX dopo il blocco non ripristina l'avvio (risposta utente). Ciò non esclude un protocollo/stato già selezionato dal ROM: è necessaria una nuova sequenza DFU con i collegamenti predisposti prima. Avviata matrice stesso HEX: nessun cavo dati, sola Ethernet, solo DMX; fermarsi al primo blocco e acquisire. Una lettura GPIO tentata durante il successivo riassetto banco non ha acquisito dati (ST-LINK riportava Vref0/no target); non è stata usata per dedurre lo stato di PB15 o un nuovo guasto firmware.

### Esito della prova sui collegamenti

L'utente conferma che l'avvio automatico riesce sempre nelle prove eseguite con l'ingresso DMX scollegato, anche con Ethernet collegata. Il blocco si presenta quando è presente un segnale DMX durante la programmazione USB DFU. Questo esito, insieme alla cattura del ciclo di attesa autobaud su USART1, identifica il traffico DMX sull'ingresso condiviso PB15 come condizione che innesca il blocco nel bootloader ROM su questo banco.

Per aggiornare tramite CubeProgrammer, scollegare il DMX prima di entrare in USB DFU e ricollegarlo dopo l'avvio dell'applicazione. Ethernet può restare collegata. Il margine sullo stack resta una correzione di compatibilità con il ROM 9.1 e da solo non consente l'aggiornamento con DMX attivo. L'aggiornamento con DMX attivo richiede un percorso di aggiornamento che eviti l'interferenza con il ROM.

### Verifica e limiti

Build complete Debug/Release150unità, senza warning, nelle cartelle `adsw100-boot-20261122-stackfix-debug` e `adsw100-boot-20261122-stackfix-release` sotto /private/tmp. Verificato sul vettore ELF realmente linkato: MSP2407FFE0, allineamento8, soglia ROM rispettata, Reset_Handler Thumb corretto, heap/stack minimi entro il limite. I test host su magic/I/O/token, CDC e regressioni dei protocolli passano; non simulano il ROM ST.

Il fatto che il ROM acquisito stia aspettando autobaud e il difetto documentato del valore MSP sono **due evidenze da non confondere con una causalità già provata per tutti i sintomi**. La correzione elimina la non conformità dell'immagine; deve ancora dimostrare sul banco l'uscita DFU affidabile e non attribuisce retroattivamente a ROM9.1 ogni precedente Wait for boot.

Richiesto test manuale dello HEX A/B mantenendo gli stessi DMX/Ethernet e procedura BOOT/RESET. Se parte: ripetere5aggiornamenti senza power cycle, poi reset hardware e cold boot, verificando brand/parametri. Se si blocca: preservare stato e acquisire core/CDC; poi separare DMX presente/assente mantenendo Ethernet e immagine uguali. Un impedimento nel ROM prima dell'applicazione non si corregge con ritardi, reset o override del magic dentro main.

Riferimenti procedurali: [UM2237, HOTPLUG/coreReg](https://dev.st.com/stm32cube-docs/prog/2.23.0/en/docs/markup/CubeProg_Command_Lines.html) e [AN3156, uscita DFU](https://www.st.com/resource/en/application_note/an3156-how-to-use-usb-dfu-protocol-in-bootloader-on-stm32-mcus-stmicroelectronics.pdf). Hotplug non applica il reset di default mostrato nell'intestazione CLI; coreReg arresta per la lettura. I log raw e i due brevi dump ROM sono nella cartella diagnostica Debug.


## Avvio automatico dopo programmazione — 22 novembre 2026

Baseline esaminata: commit utente `7d184e5`, che conserva esplicitamente il problema dopo programmazione. **Causa fisica del blocco ancora non confermata**: inizialmente nessuna CDC visibile; successivamente eseguita acquisizione CDC read-only riportata sotto. Nessun flash, reset remoto o comando di scrittura sul dispositivo. Confermato e corretto il difetto che interpretava una lettura I/O fallita come richiesta di provisioning. Questa correzione e la diagnostica non equivalgono alla dimostrazione del riavvio automatico corretto.

### Percorso reale e protezione presente

1. Linker `STM32H743VITX_FLASH.ld`: vettori/app a0x08000000, nessuno spazio riservato a un bootloader separato. `startup_stm32h743vitx.s:Reset_Handler` imposta MSP, chiama ExitRun0Mode/SystemInit, copia .data, azzera .bss, chiama costruttori e main. `USER_VECT_TAB_ADDRESS` non è abilitato. Nei sorgenti non è implementato un handoff da un bootloader residente.
2. main configura MPU/cache, HAL e clock480MHz, periferiche inclusi I2C1/USB/IWDG/lwIP, quindi TIM6 e attesa preesistente1s con refresh watchdog, log reset, LCD, RTC esterno, DMX/relè/UI, hardware_check.
3. hardware_check esegue scan bus, dump EEPROM, ping53, lettura EUI-48/EUI-64, MAC (fallback UID se necessario già esistente) e EE_LoadConfig. Quest'ultima valida e pubblica i parametri e gestisce le migrazioni già previste; nessuna modifica a tali funzioni. Il fallimento anticipato del ping salta il caricamento: ora boot? lo distingue da un caricamento tentato/fallito.
4. Startup_SimpleUnlock legge INIT a00..01, BE16 A55A. Con magic valido ritorna subito. Prima della correzione EE_ReadU16 restituiva0 anche su errore HAL: il chiamante perdeva la distinzione fra EEPROM illeggibile e valore effettivo diverso da A55A.
5. Soltanto con lettura riuscita e magic diverso il codice preesistente mostra Wait for boot, costruisce token esatto `FGM` + MAC esadecimale e attende30s. Solo il token corretto chiama HardFactoryReset(false). Token errato o nessun token non avviano l'app; dopo30s la schermata resta e il watchdog viene alimentato per sempre. Nessun bootloader/DFU è implementato in questa attesa: il commento lo indica come futuro.
6. Dopo INIT valido: Network_InitAfterLwip, RDM_Init, NFC_Init, superloop. NFC runtime non è ancora avviato nel punto del controllo INIT. Ciò non esclude uno stato elettrico esterno sopravvissuto al reset di una precedente esecuzione.

Nel repository non compare una protezione denominata ST2: il controllo effettivo qui è FGM/MAC. Non è stata inventata, introdotta o disattivata alcuna procedura ST2. Token, MAC, funzioni di provisioning e comportamento dei dispositivi correttamente leggibili restano invariati.

### Reset, salto e stato residuo

Il launch salvato punta a Debug/ADSW100.elf e connect_under_reset; include le opzioni system/core/hardware/no reset. Non prova quale procedura sia stata usata dal programmatore reale. Il reset software esistente nel codice è NVIC_SystemReset dopo Init Memory esplicito; non è il percorso ordinario post-flash e non è stato aggiunto alcun reset.

Un ingresso da Reset_Handler passa dalla preparazione C/RAM; un salto diretto a main la salterebbe, ma nel repository non è presente un tale salto. SystemInit e HAL_Init non sono un power cycle degli slave I2C esterni. Il corpo HAL_Init locale configura NVIC/SysTick/MSP: il commento generato "Reset of all peripherals" non dimostra un reset globale di ogni periferica.

La nuova fotografia viene acquisita **all'ingresso main**, prima di MPU/cache/HAL, quindi dopo SystemInit e startup C: RCC.RSR, VTOR, MSP, PRIMASK/BASEPRI, CONTROL, IPSR. Non pretende di rappresentare i registri prima di SystemInit. Mantiene i flag prima del successivo LogAndClearResetFlags. Un VTOR o mascheramento anomalo è un'indicazione da correlare con il launch, non prova autonoma di un bootloader.

### Modifica minima e limiti

- `main.c:Startup_SimpleUnlock`: lettura esplicita EE_Read dei2byte con stato HAL; fotografia immediata di stato/lock/errore HAL, ISR/CR1 e PB6/PB7. Su I/O fallito mostra **EEPROM read error**, mantiene la protezione, non tenta il token, non inizializza, non scrive e non prosegue con una configurazione presunta. Nessun retry automatico, bus-clear, reset o delay aggiunto.
- Con I/O riuscito mantiene integralmente confronto A55A, token FGM/MAC e deadline30s. Non modifica la politica di caricamento o migrazione EEPROM né il fallback MAC preesistente.
- `usb_debug.c:USB_ReadLineTimeout`: sole query read-only `boot?`, `i2c?`, `ee?` servite durante provisioning e blocchi. Gli altri comandi restano restituiti al chiamante: non si eseguono setter di fabbrica/NFC nella modalità di attesa. Il token resta gestito esclusivamente dalla funzione originaria. La coda CDC conserva la risposta sotto backpressure.
- `Startup_FormatDiagnostics`: dati esclusivamente RAM, senza riletture EEPROM/I2C. `boot?` disponibile anche nel funzionamento normale. Nessuna stampa in IRQ.

File EEPROM, I2C/RTC, NFC, DMX/RDM/Art-Net/sACN/relè, catalogo, configurazione CubeIDE e launch verificati identici per hash alla baseline di questo intervento. Modifiche main soltanto USER CODE. Nessuna modifica a mappa0x00..0x37 o parametri persistenti.

### Interpretazione diagnostica

`boot?`: stage0 ingresso main,1 controllo HW,2 lettura INIT,3 INIT valido (non certifica che tutti i servizi successivi siano partiti),4 attesa token,5 errore lettura,6 attesa finale dopo timeout. rc=-1 non ancora tentato,0 OK,1 ERROR,2 BUSY,3 TIMEOUT; magic=---- significa **dato non affidabile**, non valore letto0. cfg=-1 non tentato, altrimenti stato HAL del caricamento. mac=0 non determinato,1 E48,2 E64,3 fallbackUID; brand_ram è il brand RAM, non una rilettura persistente. reset/VTOR/MSP/mask/control/ipsr sono la fotografia d'ingresso main; mask bit0 PRIMASK, bit8..15 BASEPRI. hal/lock/err/ISR/CR1/lines sono la fotografia subito dopo la lettura INIT.

`i2c?` fornisce il confronto con lo stato corrente e la precedente fotografia dopo init periferiche. lines: bit0 SCL/PB6, bit1 SDA/PB7;3 entrambe alte,1 SCL alta/SDA bassa,2 SCL bassa/SDA alta,0 entrambe basse. SDA bassa e BUSY suggeriscono bus trattenuto; non identificano quale slave. Linee alte con stato interno incoerente richiedono invece verifica della periferica/handle. Non attribuire senza misura il guasto a NFC, EEPROM o RTC.

IWDG1 è già avviato prima del controllo hardware (prescaler64, reload1000); l'attesa1s e gli stati bloccati lo alimentano. EE_BusScan preesistente e accessi HAL sincroni possono consumare tempo sotto timeout; HAL_IsDeviceReady ritorna invece subito HAL_BUSY se il flag BUSY è già presente. Un eventuale reset periodico va distinto dalla schermata stabile che alimenta il watchdog. Non aumentati timeout, non aggiunti refresh negli IRQ e non rimossa la protezione watchdog.

### Verifiche e procedura hardware

Host: controllo reale estratto da main.c a -O0 e -Os con ASan/UBSan: A55A valido passa senza attesa; tre errori HAL non avviano provisioning neppure ricevendo il token corretto; magic non valido resta protetto, MAC errato rifiutato, timeout invariato, solo token esatto raggiunge la routine esplicita; tutti128byte preservati. Test CDC: query boot consumata come query, token restituito identico, setter non applicato nel reader. Regressioni stabilizzazione10gruppi + nuovo BOOT, NFC e fondazione14gruppi. Build ARM Debug -O0 e Release -Os,150unità ciascuna; include/define letti dalle rispettive configurazioni .cproject senza modificarle.

1. Programmare manualmente la nuova Debug e usare lo stesso Run after programming che riproduce il problema. Non usare Init Memory o token FGM. Non togliere subito alimentazione.
2. In caso di blocco acquisire in CDC, una riga per volta: `boot?`, `i2c?`, `ee?`. Riportare anche il testo del display e il programma/opzione reset usati. Queste query funzionano ora anche prima del superloop, dopo la finestra30s e nello stato errore I/O.
3. Se stage5 e magic=----, correlare err/ISR/lines cached e correnti: è un errore di accesso, non prova di magic corrotto. Se stage4/6 con rc=0, riportare i byte magic effettivi: allora la diagnosi è diversa. Non scrivere magic per tentativi.
4. Se CDC non risponde, collegare debug senza reset/flash, fermare e leggere PC/LR, startup_diag, hi2c1 e registri I2C. Osservare SDA/SCL con analizzatore se necessario. Nessuna recovery elettrica introdotta prima della verifica.
5. Solo dopo l'acquisizione confrontare reset hardware e power cycle, rileggere diagnostica e verificare impostazioni/brand. Dopo la correzione della causa confermata ripetere almeno5aggiornamenti con avvio automatico e più cold boot, verificando persistenza e normale funzionamento. Release richiede anch'essa collaudo fisico.

Artefatti: `/private/tmp/adsw100-boot-20261122-debug/` e `/private/tmp/adsw100-boot-20261122-release/`, ELF/HEX/BIN, map, log, flag e manifest hash. Queste sono build di correzione del controllo/diagnostica: **criterio di successo dell'avvio automatico non ancora dimostrato su hardware**.


### Prima acquisizione sul banco del22/11, dopo programmazione manuale utente

L'utente conferma avvio riuscito, non il guasto. CDC ADSW100 acquisita senza reset o comandi di modifica:

```text
boot stage=3 rc=0 magic=A55A cfg=0 mac=1 brand_ram=0 reset=01460000 VTOR=08000000 MSP=2407FFE0 mask=00000000 control=0 ipsr=0 hal=20 lock=0 err=0 ISR=00000001 CR1=00000001 lines=3
ee writes=0 addr=00 len=0 rc=0 reserved=0 transaction=0 local=0 meta_rc=0 meta=00
```

INIT e configurazione letti con successo, EUI-48, AXN in RAM. Seconda query i2c? trova owner=0/reserved=0/HAL20/err0/lines3. Alla prima query i2c? era invece in corso RX NFC (owner1/phase3/HAL22/ISR8001/lines1): quella fotografia non è una prova di bus bloccato. La precedente fotografia dopo init periferiche riporta bootlines1; anche questo dato isolato non prova la causa, visto l'avvio riuscito. Flags01460000 decodificati dal CMSIS locale: CPURSTF/PINRSTF/SFTRSTF, senza IWDG/WWDG/POR/BOR impostati nella fotografia; sono flag latched, non un tracciato temporale completo.

NFC riporta addr02/capacity888/rev2/C3,134scansioni e1NACK/0timeout nella seconda acquisizione: dato di contesto, non attribuito al blocco boot, nessuna modifica al servizio. Log seconda acquisizione nella cartella Debug `hardware-acquisition-02.log`. Richiesta all'utente la ripetizione di5programmazioni senza power cycle; acquisizione del guasto e prova ripetuta ancora pendenti. **Un avvio riuscito non dimostra la causa né chiude il criterio di successo.**

## Regressione salvataggi e avvio dopo programmazione — 16 novembre 2026

Sviluppo funzionale NFC sospeso. Intervento correttivo circoscritto a EEPROM/hook NFC, inizializzazione IRQ e diagnostica. Nessun cambiamento a mappa, versione, catalogo, formato SDCP2, SYNC1 wire o ai servizi DMX/RDM/rete/relè. Nessun commit, tag o programmazione hardware eseguito.

### Evidenze e correzioni

| Stato / gravità | File e funzione | Causa / conseguenza | Intervento |
| --- | --- | --- | --- |
| Confermato su host / alta | `eeprom.c:EE_Write`, `EE_InitWithNfc` | Stato E8 (Init tag autorizzato ma non completato) restituiva HAL_BUSY per qualsiasi salvataggio locale. Il servizio tag può restare pendente per errore I2C, contenuto estraneo o pannello aperto; il bus può essere libero. | Un valore locale effettivamente cambiato prepara una nuova revisione C3 prima del parametro. Annulla il consenso Init tag precedente e preserva i default/parametri correnti; non attende un tag funzionante. |
| Confermato su host / alta | `eeprom.c:EE_Write`, `EE_NfcLocalLive` | C3 dopo reboot aveva il flag volatile local_live falso e bloccava tutte le successive scritture con HAL_ERROR. Accadeva anche se la configurazione EEPROM era valida. | Una nuova scrittura locale verifica l'intera configurazione persistente e prepara una nuova revisione C3; non riprende automaticamente il salvataggio interrotto. Richieste NFC con base precedente restano obsolete. |
| Confermato nel flusso HAL / alta, impatto fisico da verificare | `i2c.c:HAL_I2C_MspInit`, `MX_I2C1_Init` | IRQ EV/ER abilitati nel callback MSP, chiamato da HAL prima della configurazione della periferica e dello stato READY. Se esistono eventi residui nel percorso debugger, possono essere serviti troppo presto. | NVIC mascherato in MSPInit0; priorità preparate in MSPInit1; clear pending/enable soltanto dopo HAL init e filtri, in Init2. Recovery NFC conserva la propria riabilitazione dopo HAL init. Nessun reset aggiunto. |
| Confermato, comportamento corretto | `user_interface.c:UI_HandleSys` | Asterisco rimosso solo se EE_WriteU8 ritorna HAL_OK. Il toast EEPROM error e l'asterisco persistente riflettono un fallimento reale. | Nessuna modifica cosmetica o forzatura del successo UI. |
| Aperto su hardware | Avvio CubeIDE Run after programming | Nessuna acquisizione PC/registri nel guasto: non è dimostrato che il difetto IRQ spieghi il sintomo osservato. Restano da distinguere reset/debug, bus esterno trattenuto, IRQ e percorso di boot. | Build A/B e diagnostica RAM; non dichiarato risolto. |

I due test nuovi hanno fallito prima delle rispettive correzioni: salvataggio Backlight con E8 e attivazione IRQ prima che il mock HAL sia pronto. Il test IRQ esegue i blocchi USER CODE reali nell'ordine HAL; non simula l'intero reset fisico STM32.

### Confronto prima/dopo NFC e ownership

Confronto dei sorgenti tracciati in HEAD `743afc1` con l'integrazione NFC nel working tree, senza ripristinare altre modifiche. Prima EEPROM/RTC usavano HAL sincrona senza riserva NFC o hook revisione. Dopo: trasporto NFC IT con riserva `ee_nfc_bus_reserved` mantenuta anche nella pausa STOP/READ e durante programmazione/rilascio; RTC ed EEPROM rifiutano l'accesso durante tale riserva. HAL_READY da sola non autorizza l'accesso fra MEMA e READ.

Gli accessi applicativi sono nel main cooperativo; callback e IRQ NFC aggiornano stati/contatori, senza scritture EEPROM o stampe. EEPROM conserva scrittura a pagine8byte, ACK polling e readback sincroni. RTC resta sincrono; nel confronto il suo unico cambiamento è il controllo della riserva. `EE_SaveConfig` usa gli stessi EE_Write/readback del pannello. Non individuato un secondo driver I2C concorrente in ISR. I controlli di bus, range e validità restano attivi: B6/D7 e NF riconoscibili ma corrotti continuano a impedire salvataggi ambigui; errori I/O non inizializzano la configurazione.

`NFC_Service` conclude lo scambio pendente prima di cedere al pannello. La disabilitazione diagnostica termina anche una pubblicazione NDEF già iniziata, compreso readback, prima della quiescenza; non libera artificialmente un trasferimento HAL ancora attivo. Il timeout/recovery già esistente non è stato aumentato o sostituito. `stm32h7xx_it.c` inoltra EV/ER alla HAL; nessun nuovo cambio al bridge o alle callback in questo intervento.

### Avvio: indagine separata

Il boot inizializza I2C, quindi RTC e controllo EEPROM prima di avviare NFC. `Startup_SimpleUnlock` già prima dell'integrazione interpretava il fallimento di EE_ReadU16 come assenza magic: può mostrare Wait for boot/attendere il codice anche con EEPROM valida ma illeggibile. È un rischio preesistente, non prova del guasto attuale e non è stato modificato in questo intervento. Non introdotti reset software, recovery a clock sui pin, ritardi o timeout aggiuntivi.

Registrati in RAM, prima della pulizia dei flag reset: RCC.RSR, ISR/CR1 I2C e livelli PB6/PB7. Stage:1 dopo init periferiche e prima RTC;2 dopo RTC;3 dopo hardware_check;4 dopo unlock e init servizio. I flag reset hardware sono cumulativi: confrontarli con la configurazione effettiva CubeIDE/ST-LINK, senza dedurre il tipo di reset dal solo nome Run. Se il blocco precede il servizio CDC occorre fermare la CPU senza reset e leggere PC/LR, stage, hi2c1.State/Lock/ErrorCode/XferISR, I2C1 ISR/CR1/CR2 e GPIOB.IDR.

### Diagnostica e build A/B

- `ee?`: ultimo ritorno pubblico EE_Write (incluse scritture metadati), indirizzo/lunghezza, numero chiamate, riserva, transazione NFC e ultimo stato metadati osservato. rc:0 OK,1 ERROR,2 BUSY,3 TIMEOUT. Metadati riportati dalla cache, non riletti.
- `i2c?`: abilitazione NFC, owner, fase, risultato, riserva/pending/writing, stato/lock/errore HAL, registri e linee correnti; stage e fotografia boot.
- `nfc?` / `nfc_i2c?`: stato servizio e ultima fotografia di guasto già esistenti.
- `nfc=0` / `nfc=1`: selezione volatile diagnostica, senza EEPROM. Dopo OFF attendere `i2c?` con enabled=0, owner=0, reserved=0, pending=0, writing=0. Una pubblicazione o uno scambio pendenti vengono completati; se il pannello li sospende, tornare Home. Nessun reset del bus comandato dalla CDC.

Build completa ON: `/private/tmp/adsw100-i2c-regression-nfc-on/ADSW100.elf`; OFF: `/private/tmp/adsw100-i2c-regression-nfc-off/ADSW100.elf`. OFF inizializza il flag runtime con `NFC_START_ENABLED=0`: tutto il driver è incluso e gli hook EEPROM di sicurezza restano attivi, ma non parte alcuno scambio NTAG. EEPROM/RTC restano funzionanti; non è una build che aggira i metadati. Il progetto CubeIDE è invariato e compila ON per default. Riproduzione: `python3 -B tests/test_stabilization.py --arm-build <cartella>`; aggiungere `-DNFC_START_ENABLED=0` per OFF.

### Procedura hardware necessaria prima della chiusura

1. Conservare impostazioni e cablaggi; non usare Init Memory, erase EEPROM o factory reset. Tenere inizialmente assente il campo RF. Verificare il file ELF effettivamente selezionato da CubeIDE, evitando di programmare una vecchia build Debug.
2. Provare OFF: avvio da alimentazione; modificare Backlight e OFF Time30→0, confermare scomparsa asterisco e rc=0 in ee?; riavviare e verificare persistenza. Controllare RTC. Acquisire i2c?, ee?, nfc? e log Reset flags.
3. Con la stessa OFF ripetere almeno tre programmazioni con Run after programming, senza togliere alimentazione. Separare l'esito da quello di reset NRST/software e da power cycle. Se non riparte, fermare senza reset e acquisire il contesto sopra; non cancellare la prova togliendo subito alimentazione.
4. Ripetere gli stessi passi con ON e medesimi collegamenti. Non si assume uguale revisione NFC dopo i test: usare le stesse impostazioni, senza ripristinare byte EEPROM alla cieca. Confrontare ultimo esito salvataggio, riserva, IRQ e fotografie boot. Ripetere poi con NFC attivo/campo RF e traffico normale.
5. ON, poi nfc=0 fino a quiescenza, quindi salvataggi: distingue interferenza del trasporto runtime da errore degli hook metadati. OFF dal boot distingue inoltre gli effetti prima della prima operazione NTAG. Se entrambe falliscono, non attribuire il problema al solo trasporto NFC.

CDC ADSW100 tentata in sola lettura: porta occupata (Errno16), nessuna lettura del guasto disponibile e nessuna sessione altrui interrotta. Nessuna prova hardware dichiarata completata.

Test software: stabilizzazione10gruppi; fondazione14gruppi; NFC C -O2 ASan/UBSan con vettori ufficiali, nuovi casi E8/C3, drain runtime OFF, accessi RTC/EEPROM e diagnostica senza I/O, completamento pubblicazione durante OFF,24scenari di taglio locale,170errori commit,68tagli REQUEST e24tagli Init. Build ARM ON/OFF150unità, entrambe senza warning. BIN SHA-256 ON `191e845609b67cffc935a86cd8d56c69c4c239db585039290e2e969ed3556e0b`, OFF `23073734ea5404f57ff3d326c735cdf02496ebb64b36cf6c80ee1eaebf61fe44`. ELF/HEX/BIN, map/log e verification.json nelle cartelle di build; la prova host non certifica il reset/debug fisico.


## Correzione trasporto NFC I2C — 16 novembre 2026

Dato hardware utente: `nfc=I2C_ERROR addr=57 capacity=0 ... errors=38 timeout=38 nack=0`. Il vecchio campo addr era **il candidato successivo all'errore**, non un indirizzo confermato; nessuna prova che57 fosse programmato nel chip. Le precedenti letture ADSW avevano individuato02. Il riferimento Git esatto `0578567:Core/Src/nfc.c` usa invece NFC_ADDRESS55 fisso e `ChipHeader` per verificare UID0/SAK/ATQA; non contiene una scansione multi-indirizzo da copiare. Riutilizzati quel riconoscimento e la gestione3NACK del riferimento, adattando solamente la selezione delle alternative ADSW02/55/57/59 e il preload HAL H7. Le alternative57/59 sono tentativi storici locali, non default NXP attestati.

**Difetto confermato e riprodotto:** `BUS_Task` comprendeva nell'unica scadenza30ms anche il tempo del main dopo che la trasmissione MEMA era già terminata con STOP. La condizione RX richiedeva ancora `now-started<30`: con main fermo80ms non avviava affatto RX e registrava timeout, poi cambiava indirizzo. Nuova regressione sul C reale (ASan/UBSan,-O2): ripristinando in una copia temporanea la condizione originale fallisce esattamente sul caso STOP+80ms; il codice corretto completa lettura/pubblicazione. Il budget30ms resta invariato per ciascun comando HAL attivo; parte nuovamente quando RX è realmente avviata. La pausa STOP minima2tick e l'attesa di programmazione10tick rimangono quelle del driver DRV. Nessuna nuova macchina a stati I2C.

**Limite dell'evidenza:** il vecchio log non registra fase/IRQ/registri e non dimostra che tutti i38timeout fisici siano quello stesso caso. In particolare non consente di escludere IRQ non serviti o linea trattenuta dal chip. Queste restano verifiche hardware, non cause dichiarate certe.

Correzioni circoscritte:

- `nfc_i2c.c:Start/BUS_NfcSession/BUS_Task`: indirizzo a7bit distinto dall'argomento HAL, congelato per l'intero scambio.02->04,55->AA; STM32 aggiunge il bit di lettura, non si passa AB a HAL. Separata anche la variabile lunghezza del trasporto da quella del documento, che prima condividevano lo stesso nome/tipo nel file.
- `NFC_Service`: conferma l'indirizzo solo dopo lettura completa e ChipHeader valido. Timeout/errori generici non fanno ruotare l'indirizzo. NACK reale del probe può far avanzare il candidato; per perdere un indirizzo già confermato servono3NACK consecutivi. Non si usa XferCount==1 del G4: H7 precarica il primo byte e può avere già count0 prima dell'ACK. HAL_BUSY con vecchio ErrorCodeAF non vale come nuovo NACK.
- `BUS_Task`: niente comando di rilascio FE064000 verso un device dal quale sia stato letto un header non NTAG. Test dedicato: ACK senza header NXP non conferma il chip, non programma registri sessione e non tocca la configurazione.
- `i2c.c` USER CODE MSP Init/DeInit: attivazione/disattivazione IRQ come nell'impostazione DRV, mantenendo priorità2 H7 sotto USART/TIM6/TIM7. Nessun cambio a clock, timing, GPIO o file IOC. IRQ bridge in `stm32h7xx_it.c` conta gli ingressi e inoltra alla HAL, senza stampe.
- `Failure`: prima del reset HAL conserva fase, registro, indirizzo HAL, CR1/CR2/ISR, stato/count/errori HAL, NVIC enable/pending, PRIMASK/BASEPRI, livelli SCL/SDA e contatori IRQ/callback. Non si deduce assenza del chip da un timeout.

L'indirizzo attivo si conferma dall'ACK e dall'header valido a quel destinatario: il byte0 letto04 è UID0, non il registro indirizzo. In scrittura block0 il riferimento preserva l'indirizzo usando address7<<1; la correzione non riprogramma quel registro. I contatori sono cumulativi dall'avvio: dopo recupero devono smettere di crescere, non tornare automaticamente a zero.

Diagnostica CDC:

```text
nfc?
nfc_i2c?
```

Il primo mostra `addr=02 probe=02 hal=04` dopo conferma, oppure `addr=--` finché nessun header è stato validato. `probe` è il candidato corrente/prossimo, non una prova di presenza. Su DRV/default55 l'equivalente è `addr=55 probe=55 hal=AA`. `capacity` può essere816 o888 secondo il CC: non viene riprogrammato per correggere la comunicazione. Su NDEF proprio inizializzato si attende SYNCED e scansioni crescenti; JSON/tag estraneo può dare FOREIGN_OR_INVALID pur con comunicazione corretta. In quel caso Init Memory conserva le protezioni già implementate. Nessun cambiamento d'indirizzo programmato nel tag.

`nfc_i2c?` è la fotografia **dell'ultimo errore**, prima di DeInit/Init; `liveirq` e `cb` sono contatori correnti. Senza errori i registri storici possono essere zero. Phase1=prefisso,2=pausa dopo STOP,3=RX,4=write,5=rilascio,6=programmazione. `last_hal` è l'argomento del trasferimento fallito. `irq=EV/ER` è il conteggio al guasto, `en/pend` hanno bit0 EV e bit1 ER; `mask` è PRIMASK/BASEPRI. `lines` bit0=SCL(PB6),bit1=SDA(PB7),3 indica entrambe alte. Questi dati permettono di distinguere NACK processato, flag hardware pendente, IRQ assente e bus trattenuto. Non vengono letti/scritti EEPROM o parametri dal comando diagnostico.

Verifiche software: build ARM150unità senza warning; regressioni NFC complete con nuovo caso80ms/preloadH7, timeout vero senza rotazione, indirizzo02/55, header estraneo e bridge IRQ; fondazione14gruppi e stabilizzazione10gruppi PASS. Verificati hash identici di EEPROM, driver DMX/RDM/rete/relè, RTC/UI, catalogo e vettori allora in uso; funzioni NfcBuild/Parse/Process testualmente identiche. Le modifiche dei file generati sono solo USER CODE. Nessuna modifica a URC01/DRV_STG4, nessun commit/tag/flash.

Build: `/private/tmp/adsw100-nfc-i2c-fix/ADSW100.elf`, `.bin`, `.hex`; ELF SHA-256 `5ad90e66be602b9e153ff1bf3bf321ce0d3ef2be5a8de021cdeb981498f29ed7`, BIN `93bedd61e905f229fe3804ad25853d15f498d6b4d936c9d94806aeef8b584c8b`. Collaudo fisico ancora richiesto: pannello Home, prima prova senza campo RF, lettura delle due diagnostiche. Eventuali timeout residui richiedono la nuova fotografia; non sono dichiarati risolti sul dispositivo dai soli test host. Nessuna sonda ST-LINK rilevata; la CDC ADSW risultava occupata dal terminale, quindi non sono stati acquisiti nuovi registri live.

## SDCP2 ADSW100 — integrazione fase 2, 15 novembre 2026

Prima integrazione storica di SNAPSHOT, REQUEST e ACK SDCP2/NDEF/SYNC1. Il formato operativo è ora quello descritto all'inizio di questo audit. Il Software ID resta quello del firmware, coerente con RDM.

File funzionali: `nfc_i2c.c/h`, header dati generato, `eeprom.c/h`, integrazione USER CODE di `main.c` e `stm32h7xx_it.c`, comando CDC `nfc?` in `usb_debug.c`. `rtc_i2c.c` rispetta la riserva del bus; `UI_ConfigActive()` in `user_interface.c/h` è una query che non cambia navigazione e sospende nuove operazioni NFC mentre il pannello configura. Il bus pendente viene comunque drenato, per evitare BUSY sul salvataggio. Non aggiunti moduli NFC/controller/router o librerie crittografiche.

Trasporto IT STOP/READ/rilascio lock adattato da DRV_STG4 `0578567:i2c_bus.c`, indirizzo rilevato fra02/55/57/59, programmazione NTAG attesa cooperativamente10tick, timeout30ms con recupero HAL. IRQ I2C1 a priorità2, sotto USART/TIM6/TIM7. Nessuna HAL_Delay nel driver NFC. Le chiamate EEPROM legacy con readback restano sincrone: non si dichiara il commit interamente non bloccante. Due scansioni stabili e RF assente prima del processing; controllo RF e revisione prima di ogni scrittura, header NDEF commesso per ultimo, poi verifica completa. Nessun journal aggiunto al tag e nessuna modifica al CC già valido.

### Init Memory richiesto dall'utente

L'azione `HardFactoryReset` usa `EE_InitWithNfc`: D7 prima dei default, E8 solo dopo readback completo della EEPROM. Dopo il reboot il servizio pubblica i default verificati e consuma E8 solo dopo readback del tag. Riconosce JSON legacy anche dopo NULL TLV, ma verifica MIME, struttura e magic del proprio EUI; i valori JSON **non vengono mai applicati**. Cancella soltanto la coda occupata dal vecchio NDEF riconosciuto. NDEF estranei o illeggibili restano intatti anche con consenso pendente.

CC vergine/parziale compatibile con il provisioning: solo E8, probe del blocco2k, verifiche UID/accesso/static lock/config e nuova guardia header/RF. RMW block0 come DRV: byte0 scritto = indirizzo attuale<<1, non UID04; conserva gli altri byte, programma E1 10 66 00 e verifica il readback. CC validi/protezioni non vengono alterati. Nessuna deduzione di capacità dal solo indirizzo I2C. Presenza tag, supporto modello e probe2k vanno confermati sul dispositivo reale.

### Persistenza e recovery

Hook in `EE_Write` invalida la base NFC prima delle scritture ai parametri, incluse quelle dirette del pannello; identità/brand/policy non fanno parte della configurazione NFC. Nessuna scrittura legacy per bootstrap NFC. REQUEST prepara revisione/ID/CRC in B6 prima dei parametri; applica solo campi cambiati con CFG, salva mediante EE_SaveConfig, rilegge tutta la configurazione e solo allora commette metadata e ACK. ID/CRC già commessi producono solo ACK; ID uguale con CRC diverso, base obsoleta, editing locale o RDM occupato non applicano dati. Le sezioni sconosciute non critiche sono conservate byte per byte nell'ACK; critiche sconosciute rifiutate.

Dopo reset B6 si finalizza soltanto se il documento e **tutti** i parametri già coincidono, senza riscrivere configurazione. NF corrotto, D7 interrotto e C3 non appartenente alla sessione corrente non causano default o replay automatici. Sono possibili configurazioni parziali dopo un power-cut nella EEPROM in place: vengono lasciate invariate e segnalate, non corrette silenziosamente. Intenzione Init pendente non autorizza a cancellare tag estranei/illeggibili. Una pubblicazione tag interrotta non riconoscibile può richiedere manutenzione esplicita; nessuna garanzia di autoripristino universale senza journal/secondo slot.

### Verifiche effettive

- Host ASan/UBSan (anche -O2) `tests/test_nfc.py`: snapshot golden byte-identico, 18 vettori ADSW positivi/negativi, loop trasporto con21 REQUEST/ACK, RF busy, protezioni/capacità CC, JSON valido non importato, timeout e NACK, priorità pannello/RDM, deduplicazione e ID conflittuale, estensioni opache, replay dopo salvataggio locale.
- Conservazione legacy host: tre brand, Videlio con menu_code1, tutti i byte00..27 e luminosità legacy150 invariati durante la pubblicazione; nessuna normalizzazione del parametro non modificato.
- Fault injection host:170 scenari I/O nel commit;68 interruzioni immediate a livello di byte su richiesta multiparametro rete/DMX,24 nell'Init EEPROM. Reset simulato senza cleanup: nessuna riscrittura dei parametri già elaborati; E8 solo con default verificati. Non sono tagli fisici di alimentazione né un modello analogico delle celle EEPROM.
- Init host: sostituzione JSON proprio posto all'offset legacy, nessuna importazione, eliminazione coda, riconferma dopo reset senza seconda formattazione, CC vergine2k agli indirizzi02 e55 preservando UID/indirizzo/lock; contenuto estraneo preservato.
- Fondazione:14 gruppi PASS con i cataloghi allora in uso. Stabilizzazione:10 gruppi PASS, inclusi soak e hold Node30s. Sorgenti DMX/RDM/Art-Net/sACN/relè verificati per hash identico a inizio fase2; nessuna modifica ai riferimenti URC/DRV.
- Build ARM completa150unità PASS, senza flash automatico; artefatti in `/private/tmp/adsw100-nfc-sdcp2`.

Artefatti di questa build: ELF SHA-256 `7df63be13bbb6d1b9cd284668d89ceecd901773e45ee2360472a52511539c5be`, BIN `6ef2142c661d90843a66c903d191e370deb4d62695b49cc9965816d95f9e2128`. text221816/data648/bss75067 byte (build Debug host-generated,150unità). Nessun warning nei log ARM. Sezioni generate CubeMX fuori USER CODE verificate identiche a HEAD; CRLF preservati. Nessun flash eseguito.

Per il collaudo usare `nfc?` sulla CDC. Stati principali: SYNCED, RF_BUSY, WAIT_RDM, INIT_INTERRUPTED, INIT_FOREIGN_OR_EDIT, REQUEST_RECOVERY_REQUIRED, EEPROM_ERROR. `state` riporta l'ultimo stato metadata letto/tentato dal servizio, non una lettura live della EEPROM durante la diagnostica. Init Memory è l'azione esplicita che ripristina i default come prima e programma anche il tag dopo reboot; un avvio ordinario non sostituisce JSON legacy. Non viene abilitato il vecchio writer URC01 per SDCP2.

Da misurare su hardware: tempi I2C/RF e reali lock/capacità, field remove/reapply, concorrenza RDM/Node/Ethernet/RTC durante commit EEPROM, refresh DMX/relè e watchdog, power-cut fisici, CC/provisioning e consumo consenso. I test NFC host simulano lo stato occupato RDM, non qualificano l'intero superloop sotto traffico fisico simultaneo. URC01 attuale non è il nuovo client SDCP2: non dichiararlo collaudato da questi test.

## Fondazione NFC comune — 15 novembre 2026

Il primo censimento distingueva PID standard e coppie manufacturer + PID.
Il formato sperimentale è stato sostituito dal contratto corrente SDCP2 /
ST2FW v3 descritto sopra. Restano validi il mapping dei PID ADSW100 e la
protezione dei 128 byte applicativi legacy.

## Assegnazione Model ID e codice Uploader96 — 15 novembre 2026

Autorizzazione esplicita utente:96 sia in ADSW100 sia nel catalogo INI. Model ID RDM16 bit=96/0x0060 sotto Manufacturer0x17FA, comune a Brand0 AXN/NODIM01, Brand1 Videlio/ADSW100 e Brand2 Generic/NODIM01. Assegnazione distinta ma numericamente uguale per codice uploader/Product ID futuro32 bit=96/0x00000060. `Core/Inc/rdm.h:ADSW_RDM_MODEL_ID` sostituisce il placeholderFFFF; `Core/Inc/defs.h:ADSW_UPLOADER_PRODUCT_ID` conserva la prenotazione per la futura integrazione. Nessun Target ID, layout o bootloader aggiunto. Versione software resta1.4.0 durante lo sviluppo; UID, OEM, Manufacturer, EEPROM e brand non vengono cambiati.

File INI effettivo: `/Users/gianlucatini/micro/Studio Due/UPLOADER_FW_BUILDER/uploader_fw_builder/config/MakeFWF.ini`. `UPLOADER_PYTHON/Uploader_App/updater.ini` contiene solo connessione/trasporto e resta invariato. Il catalogo Builder registrava i profili fino a93 e le prenotazioni commentate94 DRV_STG4 e95 URC01. Prenotato96 con lo stesso metodo, senza inventare Key, Micro o profilo PIC. Una sezione legacy incompleta renderebbe read_catalog non valido; la prenotazione non pretende di rendere gia' programmabile ADSW100. Registro e censimento attivi aggiornati in Firmware_Git/URC01/docs/STUDIO_DUE_IDENTIFICATORI.md e STUDIO_DUE_ID_LEGACY.tsv, distinguendo Model ID RDM, Product/Uploader, Target e UID.

Controlli: nessuna precedente assegnazione96 trovata negli INI/censimenti e nelle macro Device/Model/Product delle sorgenti locali cercate; nessuna garanzia su archivi esterni non disponibili. Parser reale read_catalog:94 profili identici prima/dopo l’aggiunta dei commenti di assegnazione, CRLF/encoding preservati. La suite completa test_legacy_st2 non e' stata eseguita per dipendenza PySide6 assente nel Python disponibile; nessuna modifica a test/cataloghi preesistenti per aggirarla. Dieci gruppi firmware host PASS, con DEVICE_INFO nei tre brand che restituisce byte00 60 e codice uploader96; build ARM150 unita' PASS. Nessun flash/commit/tag automatico.

Artefatto con assegnazione96: `/private/tmp/adsw100-model96/ADSW100.elf`, SHA-256 `60a2ec0965e788fb3892d107878325663cda37bfd8c3d7efa05e7a4fc66630d1`; build senza warning/errori. Model ID da verificare sul controller dopo programmazione.

## Esito utente e ripristino URC01 alla versione IPv4

Il15 novembre2026 l’utente conferma «Funziona bene» per ADSW100 con la cessione RDM autonoma di30 s provata con URC01 precedente, quindi richiede il ripristino dei sorgenti URC01 alla versione IPv4 validata prima dell’esperimento Node. E' una conferma hardware dell’utente; non sono stati forniti un elenco dettagliato di tutti i casi/tempi/protocolli o prove DMXcat/Swisson, che non vengono dichiarati collaudati.

URC01: ripristinati dmx_tx.c, rdm_phy_test.py e dmx_stream_host_test.py alla snapshot pre-Node; rimosse solo le parti keepalive da rdm_manager.c e dal relativo harness, conservando integralmente le modifiche IPv4. I cinque file risultanti corrispondono byte per byte agli hash della snapshot pre-Node conservata durante l’implementazione. Restano RM_NetworkFormat, etichette interfaccia/editor, DHCP ON/OFF, IP/prefix, gateway e DNS e le relative regressioni. Nessuna acquisizione bus aggiuntiva, possesso750 ms o GET periodico500 ms rimane in URC01. La documentazione URC01 e' aggiornata alla scelta corrente; l’esperimento precedente resta documentato qui come storico, non come implementazione attiva.

ADSW100 mantiene la soluzione autonoma30 s e la correzione TIM7. Nessun sorgente ADSW100 modificato nel ripristino URC01; EEPROM, branding, parser Ethernet e relè invariati. Nessun flash/commit/tag automatico.

Validazione del ripristino URC01: build Release PASS senza warning/errori; test Manager/E1.37-2, controller/discovery, PHY controller, PHY responder e streaming DMX PASS (ASan/UBSan nei banchi previsti). Nuovo ELF `URC01/Release/URC_100.elf` SHA-256 `ae28c3788a80c3867dcad0a6df051b0351fbbe21f4e0134b173b5d780e6cbe43`; pacchetto `URC01.st2fw` SHA-256 `db0c51f174cd9d6ef78cd13f2fab23aec6d4c8cb367b08187f5f4bfbbcd14257`. Verificate anche le stringhe IPv4 nel binario. Artefatti ricompilati, nessuna programmazione della scheda.

## Cessione RDM autonoma ADSW100 di30 secondi — decisione corrente

L’utente sceglie di verificare con URC01 precedente alle modifiche Node e autorizza la soluzione autonoma ADSW100:30 secondi dall’ultimo pacchetto RDM con struttura e checksum validi, indipendentemente dal tool. Questa decisione supera la necessita' del mantenimento specifico URC01 descritto nell’incremento storico sotto. Non sono state annullate automaticamente le modifiche URC01 gia' preparate; non sono richieste per questo collaudo.

Modifica funzionale in `dmx_512.c`: `DMX512_ObserveController` rinnova lo stato RAM RDM separato dal rilevamento DMX; `RDMControllerPresent` gestisce scadenza monotona30000 ms, memorizzandola anche in IN per evitare riattivazione al giro del tick; `NodeTryStart` applica la cessione a entrambi i Node. DMX mantiene il rilevamento2 s preesistente. BREAK/Start Code/pacchetto in corso sono protetti subito dagli stati RX gia' presenti; il main rinnova i30 s soltanto dopo RDM_PacketValid. CRC errati, header incompleti o UART error non rinnovano il periodo. Nessuna logica PID o nuovo checksum in ISR. La cessione RDM e' conservata attraverso i cambi ruolo: SET IN→Node non fa ripartire il TX durante configurazione. Alla scadenza continuano a valere stato RX/TX, fine TC, attivita' recente, BUSY/RXNE/errori e pin libero: ripresa da BREAK con frame completo, mai taglio di un frame attivo.

`rdm_phy?` aggiunge `hold` in millisecondi, durata residua della cessione RDM (anche in IN, come protezione di un successivo cambio a Node). Nessuna nuova EEPROM, PID, opzione pannello o codice controller richiesto. Parser/arbitraggio Ethernet, relè, BREAK/MAB e correzione TIM7 invariati. Con un Manager che continua discovery/polling la cessione resta rinnovata: per verificare la ripresa devono cessare i messaggi RDM; uscire dal solo editor potrebbe lasciare attive le interrogazioni del Manager. Traffico DMX di un altro controller continua correttamente a impedire che il Node diventi una seconda sorgente.

Limiti: il ricevitore non ascolta durante il proprio TX. Il primo messaggio del tool puo' non essere intercettato e richiedere ripetizione; dopo una pausa operatore superiore a30 s il Node puo' aver ripreso la trasmissione. Non si promette arbitraggio multimasters o compatibilita' hardware con ogni tool. DMXcat e Swisson non sono stati collaudati dall’agente.

Prove software: dieci gruppi host PASS con ASan/UBSan nel banco Node/RDM. In entrambi i Node: GET, pausa10 s senza keepalive, SET→GET riusciti; rinnovo e tenuta oltre29 s; CRC errato non rinnova; scadenza durante RX parziale protetta e successivo messaggio valido rinnova; cambio IN→Node conserva la cessione; header incompleto/error UART non crea una falsa cessione; pin basso/BUSY dopo scadenza impediscono TX finche' la linea non e' libera; ripresa513 byte; wrap HAL_GetTick. Aggiornati solo i test di ripresa dopo RDM da2 a30 s, conservando le attese DMX/boot preesistenti. Restano PASS stress10 minuti per Node con keepalive, soakRDM/DMX, EEPROM/branding/UI/CDC e regressione ARR nullo.

Build ARM150 unita' PASS senza warning/errori: `/private/tmp/adsw100-rdm-hold30/ADSW100.elf`, SHA-256 `3b10cf34de6491d7017ae18e664684418c2a9a562357e2c0ce6b3cd7fcf7efac`. Nessun flash, commit o tag eseguito. Collaudo hardware richiesto con vecchio URC01: acquisizione iniziale, editor inattivo10–20 s, SET Address/Mode e lettura successiva, cessazione traffico e ripresa dopo30 s, ripetizione Art-Net/sACN e osservazione DE. Le modifiche Node URC01 restano opzionali e da qualificare su altri apparecchi: modificano tempi di acquisizione e GET periodici, pur senza nuovi payload o SET impliciti; conservare la presentazione IPv4 validata.

## Sessione coordinata Node autorizzata e implementata — 15 novembre 2026

La precedente correzione della presentazione IPv4 URC01 e' confermata dall’utente sul display dopo programmazione; non soltanto verificata in host.

L’utente autorizza esplicitamente la sospensione DMX OUT durante configurazione RDM. La nuova implementazione usa la cessione2 s gia' esistente in ADSW100, senza nuovi PID o EEPROM. Modifiche funzionali solo a URC01: `rdm_manager.c:Start/Reply/RDM_ManagerTask` mantiene l’editor cablato con GET DEVICE_INFO ogni500 ms quando non ci sono altre richieste; non tocca cache o piano SET. Alla chiusura cessano le letture, il Node riparte dopo il lease; se si disconnette il controller si applica la stessa scadenza. Restando nell’editor si sospende intenzionalmente il DMX fisico a valle, con le relative policy DMX Loss dei ricevitori.

`URC01/dmx_tx.c:DMX_TX_RDMExchange/RDM_BusIRQ/RDM_BusTimer/TIM2_IRQHandler/UART4_IRQHandler/DMX_TX_RDMPoll` aggiunge ascolto passivo non bloccante prima del TX. DE basso per30 ms di linea completamente silenziosa oppure fino a BREAK+SC0+512 slot del Node, poi gap100 us verificato. Risposte validate e corrispondenti a UID/TN/PID/sub-device/command class della richiesta mantengono il possesso coordinato750 ms per richieste ravvicinate. UART BUSY/RXNE/errori e pin basso impediscono l’avvio; il limite500 ms resta controllato anche sotto traffico continuo. Frame corti/incompleti non autorizzano una trasmissione nel mezzo del frame: tornano Port busy. Il buffer TX esistente conserva la richiesta con DMA inattiva; evitata una copia257 byte DTCM che superava il margine heap/stack, senza cambiare linker o dimensione stack.

Limite dichiarato: questa acquisizione e' pensata per un ADSW100 Node a512 slot con periodo inferiore a30 ms e un URC01. Non garantisce arbitraggio tra controller generici o piu' Node attivi. Non taglia frame DMX, non amplia semplicemente la finestra RX e non cambia i payload E1.20/E1.37-2. Firmware ADSW100 applicativo invariato rispetto alla correzione TIM7; aggiunta soltanto regressione host della sessione.

Prove: PHY e Manager ASan/UBSan PASS (DE basso durante frame, gap, pin/BUSY, frame corto, timeout sotto traffico, editor inattivo10 s, SET/readback dopo pausa, chiusura senza nuovi GET); regressioni controller/discovery, responder PHY e streaming DMX PASS. Build URC01 Release PASS con controlli heap/stack. ADSW100: dieci gruppi PASS, inclusi10 minuti simulati per ciascun Node con GET500 ms, SET/get, timeout, ripresa513 byte e ritorno IN. Collaudo elettrico con entrambe le schede aggiornate ancora necessario; nessun flash/commit/tag automatico.

Artefatto URC01 finale: `Release/URC_100.elf`, SHA-256 `b9751d8bf8dfa932969715bc990b0d6aa8e2cfee48c6ec8c4ca4901b42a079c0`; pacchetto `Release/URC01.st2fw`, SHA-256 `e4b5e49f452095a683d42d01ad123afe94d391b10789eb2410f8eecec05e9f66`. Build Release senza warning/errori. Firmware ancora da programmare e collaudare con ADSW100; il solo risultato host non chiude il difetto sul banco.

## Prova Node dopo correzione TIM7 — 15 novembre 2026

Utente: Art-Net Node attivo, discovery riesce e alcuni GET leggono, altri mostrano Invalid response; SET Mode→IN e indirizzo non riescono. Monitoraggio CDC di sola lettura nello stesso stato, senza reset o modifiche firmware: a936585 ms ruolo1/1, TX3/DE1, CR1=A9, TIM7 D/1/0/29999/6184, sent8953; a1017983 ms sent12165. Il TX continua a circa40 frame/s, fault0: non è il precedente blocco TIM7. RX RDM749, byte21556 e FE749 restano inizialmente invariati mentre il controller è nell’editor.

Su richiesta di una singola prova SET Mode→IN dal Manager, l’utente riporta `0 OK, 0 NACK, 1 Error`. Snapshot successivo a1089866 ms: ruolo ancora1/1, RX RDM749, byte21584 (+28), FE749 invariato, NE/ORE0, drop0 e fault0; il driver non ha pubblicato una nuova richiesta RDM completa. In questo tentativo il SET non è arrivato al decoder PID: non è stata dimostrata una perdita del solo ACK o una mancata persistenza EEPROM. Il rilevamento dei byte senza una nuova richiesta è compatibile con perdita del BREAK/start durante il proprio TX; senza capture elettrica non si attribuisce a un preciso fronte la contesa.

Codice verificato: `NodeTryStart` mantiene la cessione2 s dall’ultimo pacchetto/controller riconosciuto, poi riprende il DMX autonomo. `URC01/rdm_manager.c:RDM_ManagerTask`, ramo editor, esaurisce i dettagli da leggere e poi ritorna senza polling periodico. Ne deriva che durante la lettura iniziale può esserci RDM operativo, mentre dopo la pausa operatore il Node torna a trasmettere prima del SET. In URC01 `dmx_tx.c:RDM_RXIRQ` restituisce RDM_MALFORMED (mostrato come Invalid response) anche quando il primo byte della risposta attesa non è0xCC: un frame DMX del Node può dunque produrre quel messaggio. Non si afferma che ogni Invalid response osservato abbia lo stesso payload, non acquisito dal log CDC attuale.

**Stato:** nessuna nuova modifica al codice per questa prova. La transazione già iniziata in RX è protetta dalla macchina a stati; l’acquisizione iniziale del bus tra due sorgenti non è garantita. Non aumentata arbitrariamente la finestra e non forzato DE dentro un frame attivo. Per una soluzione sulla stessa coppia occorre definire la cessione coordinata e la sospensione DMX durante configurazione; richiesta scelta all’utente perché il requisito originario chiedeva refresh sostanzialmente continuo. Nessuna sessione/keepalive o nuovo PID implementato in assenza di tale scelta. RDM con DMX OUT continuo richiede invece un percorso di gestione indipendente o topologia coordinata adeguata.

## Debug durata RDM, Node separato e presentazione URC01 — 15 novembre 2026

Baseline ADSW100 `743afc1`, URC01 `97a66d7`. Segnalazione hardware: scomparsa dalla discovery intorno a4 minuti, non recuperata dal riavvio del controller; Node e presentazione IPv4 sono problemi distinti. Sono state eseguite prima le prove sul codice non corretto, poi le modifiche ai soli percorsi riprodotti. Non è stata riprodotta una scadenza fissa a240 s nel test host; non dichiarare dimostrata la causa temporale sulla scheda.

**Aggiornamento hardware dell’utente:** al guasto si arrestano anche la ricezione DMX e il lampeggio del LED DMX, mentre il pannello resta operativo. Il solo stato muted non spiega questa perdita: le correzioni discovery sotto sono difetti riprodotti separati, non una causa dimostrata dell’arresto della porta. CDC attuale espone soltanto rdm? di base; rdm_phy? richiede la programmazione della nuova build. Nessun reset o flash eseguito durante l’acquisizione, per preservare il guasto.

### Evidenze prima della correzione

- Nessun timer di disabilitazione RDM a240 s trovato. I divisori uint16 del task main vengono azzerati a10/100/1000/3000 chiamate; TIM6 mantiene impulsi e flag1 ms. Il contatore inattività LCD si ferma a30 s. `enabled` è impostato solo da RDM_Init; `muted` cambia solo all’init e con controlli discovery. Nessun reset generico periodico aggiunto.
- Senza traffico, discovery dopo **600007 ms simulati** riuscita. Discovery ogni3 s più GET automatici: **603717 ms,1200 richieste,200 cicli** riusciti. Prova di volume: **70210 richieste in208280 ms**, oltre256 TN e65536 transazioni, senza blocco. DWT32 bit attraversa ripetutamente il wrap a480 MHz; ciò non equivale a una scadenza a4 minuti.
- **Errore riprodotto sul codice originale:** dispositivo muted, UN_MUTE broadcast completo in pending, nuovo BREAK/SC prima del servizio main. RDM_Receive passava da CanReply prima di applicare il controllo: UART occupata → return → flag rimasto muted. Il nuovo DUB non riceve risposta. Il test falliva sull’assert di unmute. Lo stesso effetto esisteva con frame oltre la deadline di risposta: ReceiverExpire cancellava il pending prima del protocollo.
- **Seconda perdita riprodotta:** un precedente GET occupava lo snapshot unico; il successivo UN_MUTE completo era scartato, senza essere elaborato. Un test dedicato falliva anche dopo la sola correzione di CanReply. Non era un overflow del TN o di un contatore16 bit: era perdita di messaggi/condivisione del buffer.

Questi percorsi spiegano una persistenza muted fino al reset e una mancata recovery con discovery ripetuti se il pattern si ripete. **Il collegamento quantitativo con i circa240 s osservati non è ancora provato**: restano da misurare carico main, pattern URC01 automatico e stato fisico USART al guasto. I test host qui non eseguono l’intero superloop con latenze reali LCD/Ethernet/I²C o l’hardware NVIC/APB.

### Aggiornamento dopo il riscontro dal display

L’utente ha confermato che ADSW100 **non è più presente nel Manager**, mentre una nuova query CDC sullo stesso firmware ha restituito `rdm=1 uid=17FA:E7164570 rx=6219 late=0`. Quindi la scomparsa dal controller è avvenuta durante la sessione: la crescita di rx provava elaborazione di pacchetti validi, non successo discovery o ricezione delle risposte. Non equivale a RX fisicamente arrestata. L’eventuale perdita DMX nello stesso stato resta da verificare tornando a DMX Send senza reset. Nessuna sonda ST-LINK risulta collegata; la CDC caricata non permette ancora lo snapshot dei registri o del flag muted.

Nel Manager URC01 `SyncDiscovery` incrementa misses dopo discovery completati con esito OK senza trovare l’UID: a3 lo marca offline, a6 rimuove la riga se non possiede IDENTIFY. L’intervallo nominale POLL_INTERVAL è5000 ms, cui si sommano i lavori in corso. Non è un timer fisso a240 s e non è stato modificato per nascondere la perdita.

**Ulteriore difetto riprodotto in host:** pacchetto RDM completo in pending, BUSY USART attivo al primo giro main e rilasciato al giro successivo. Il vecchio `DMX512_Service` consumava lo snapshot prima di `RDM_Receive`; `CanReply` rifiutava BUSY e non restava alcun retry. Il test falliva sull’asserzione dello snapshot conservato. Correzione locale: finché lo stato è BUS_RDM_PENDING e BUSY persiste, lasciare il pacchetto in coda e riprovare nel superloop. La deadline originale continua a scadere, un nuovo BREAK mantiene la propria proprietà e nessun frame viene interrotto. Il contatore diagnostico `defer` misura i giri main rinviati, non il numero di pacchetti. Test successivi:100 richieste GET/SET/DUB, BUSY transitorio, BUSY oltre deadline e recupero della successiva richiesta. Nessuna attesa attiva o aumento timeout.

La semantica hardware di BUSY come ricezione in corso è documentata da [ST RM0433, USART_ISR](https://community.st.com/ysqtg83639/attachments/ysqtg83639/stm32-mcu-products-forum/58237/1/RM0433.pdf). L’intervallo8 µs del mock è solo uno stimolo del test: **non è una misura né una conferma della sovrapposizione RXNE/BUSY su questa scheda**. Il collegamento tra questo difetto, le perdite UN_MUTE e la scomparsa reale richiede ancora rdm?/rdm_phy? estesi sulla nuova build o una misura del bus.

### Guasto riprodotto sulla nuova build: turnaround TIM7 con ARR nullo

**Confermato su hardware:** dopo presenza iniziale e valori aggiornati, l’utente segnala nuovamente la scomparsa dal Manager. Ultimo campione sano a327547 ms: TX0, bus0, RX1122, sent666, TIM7 CR1/DIER/SR=4/0/0. Primo campione bloccato a337718 ms: TX6 (attesa prima del BREAK RDM), bus6, DE0, USART CR1=2D/CR3=1/ISR=621090, NVIC USART1=1/0, TIM7=D/1/0, RX1135, sent670. Nei campioni successivi RX1135/sent670 restano fermi, mentre FE e byte continuano a crescere (a398796 ms: FE1263, byte36543), senza NE/ORE e senza fault TX. Quindi USART/ISR continuano a ricevere: il ramo `if(tx_state)` scarta i byte perché la risposta precedente non avanza. Questo spiega anche il DMX IN non recuperato, senza attribuirlo a muted.

**Difetto software verificato:** `DMX512_SendPacket` calcola un turnaround residuo di1 µs quando la richiesta è servita a199 µs oppure almeno200 µs dalla fine del pacchetto (entro la deadline ammessa1800 µs). `TimerArm(1)` scrive ARR=0. Con autoreload nullo TIM6/TIM7 non contano: è documentato nel driver ST locale `stm32h7xx_ll_tim.h`, LL_TIM_SetAutoReload, e nel manuale STM32. Il watchdog TX usa lo stesso timer, quindi non può liberare questa fase ferma. Non è una scadenza fissa a240 s o overflow16 bit: dipende dal primo turnaround che cade nel percorso da1 µs. Il registro ARR non era incluso nello snapshot hardware caricato: il collegamento al valore nullo deriva dal percorso sorgente, dal pattern registri e dalla riproduzione host, non da una lettura diretta di ARR sulla scheda bloccata.

**Prima della correzione:** aggiornato il mock per rispettare ARR nullo/CEN/OPM reali; il vecchio mock generava artificialmente UIF anche con ARR=0. Un test con ritardo199 µs fallisce con `tx=6 CR1=D DIER=1 SR=0 ARR=0`, coerente col blocco acquisito. **Correzione minima:** TimerArm impone almeno2 µs (ARR≥1); BREAK176 µs, MAB16 µs, finestre Node e watchdog30 ms restano invariati. Nella nuova build tim riporta CR1/DIER/SR/ARR/CNT per verificare anche l’autoreload reale. Aggiunto al massimo1 µs ai soli residui prima da1 µs; nessuna attesa o reset periodico USART. Dopo la correzione16 casi normali/discovery a0/1/198/199/200/201/500/1790 µs completano fino aTC e ritornano RX. Tutti i dieci gruppi host passano, inclusi ASan/UBSan e stress precedenti. Resta necessario riprogrammare questa correzione e ripetere hardware discovery/GET/SET, durata e ritorno DMX Send senza reset.

**Confronto richiesto DRV_STG4:** ispezionati sia HEAD `93eefde` sia il working tree locale con modifiche preesistenti in `FIRMWARE_DRV_STG4`. In dmx.c DMX_TxService, BREAK/MAB avanzano tramite differenze unsigned di TIMER_Cycles; timer.c restituisce DWT->CYCCNT. DMX_Send avvia discovery con TXE o normale BeginBreak: non usa il one-shot TIM7 con ARR=durata−1. In HEAD lo stesso confronto DWT si trova in DMX_Task; nella copia locale corrente in DMX_TxService. Pertanto questo specifico difetto non è presente nel percorso DMX/RDM esaminato. Non è una qualificazione generale del firmware DRV_STG4; nessuna modifica applicata al riferimento.

Build corretta TIM7: `/private/tmp/adsw100-rdm-timer-fix/ADSW100.elf`, SHA-256 `2b6da259be4340d9eaf9b8201218b4c411e631142f3978dc869df8c5b7db8d13`. Build ARM completa150 unità PASS, dieci gruppi host PASS. File distinto dal precedente ELF diagnostico ancora caricato sulla scheda; nessuna programmazione eseguita dall’agente. Il collaudo hardware della correzione ARR resta da eseguire.

### Nuova build caricata dall’utente: campioni iniziali

Il15 novembre2026 l’utente ha confermato programmazione della build diagnostica, RDM Manager avviato e ADSW100 **presente con valori aggiornati**. Nessun aggiornamento URC01 eseguito durante questa prova. Il monitor è stato riaperto dopo la disconnessione USB della programmazione; primo snapshot valido a52743 ms: ruolo0/0 (IN), DE0, NVIC1/0, CR1=2D, CR3=1, ISR=621090, RX123, TX completati75, NE/ORE0 e fault0. A164731 ms: RX533, TX completati325, stessi registri/stati e nessun errore NE/ORE; query discovery con mute/unmute/DUB e risposte accodate in crescita. FE segue i BREAK e non è di per sé un errore. Nessun reset o cambio configurazione dall’agente; questi sono campioni iniziali, non ancora un collaudo di durata concluso.

### Build diagnostica proposta per il nuovo collaudo

ELF compilato e verificato: `/private/tmp/adsw100-rdm-lifetime/ADSW100.elf`, SHA-256 `15fbd7116568978d032482ca08569bd6368ec422c3e4f75d403f13e89c73419a`. Include correzioni riprodotte e query estese; nessun flash eseguito dall’agente. L’utente ha scelto di caricare questa build e ripetere il test. In attesa della programmazione, il monitor CDC alterna esclusivamente rdm? e rdm_phy? ogni5 s, con gestione disconnessione/riconnessione. Una risposta ERROR a rdm_phy? indica che la diagnostica estesa non è ancora disponibile sul firmware caricato. I nuovi dati hardware saranno registrati soltanto dopo la loro acquisizione.

### Ritorno DMX Send senza reset: esito negativo

L’utente ha poi riportato **LED DMX spento e nessuna risposta DMX** passando da Manager a DMX Send senza resettare ADSW100. Query CDC subito dopo: `rdm=1 uid=17FA:E7164570 rx=6219 late=0`. Il valore RDM fermo in DMX Send è atteso, perché DMX non incrementa rx RDM; non consente di dedurre se arrivino byte UART o BREAK. La porta non torna a pubblicare DMX valido, mentre CDC/main restano operativi. Questo esclude la sola rimozione grafica o il solo muted come spiegazione completa. Non dimostrata invece una disabilitazione fisica USART: manca ancora lo snapshot al guasto.

La nuova rdm_phy? comprende anche `irq=enable/pending` NVIC USART1, `pin` RX istantaneo e `role=applicato/configurato`, oltre a DE, CR1/CR3/ISR, stati e contatori. Nessuna lettura RDR, cancellazione flag, reset periferica o modifica di configurazione. La build precedente ancora caricata non offre questi dati; per osservare lo stato originale serve una sonda in hot-plug senza reset, oppure caricare la nuova build e riprodurre il guasto. Nessuna sonda era presente al censimento. Il difetto BUSY corretto non viene presentato come soluzione dimostrata dell’arresto DMX.

### Correzioni mirate ADSW100

| File/funzione | Modifica effettiva |
| --- | --- |
| rdm.c RDM_Receive | MUTE/UN_MUTE con CRC, destinazione, CC, root e PDL validi agiscono sul flag prima di CanReply/deadline. Broadcast non risponde; unicast risponde soltanto se ancora consentito. Nessun ACK tardivo o tentativo TX nel frame successivo. Gli altri PID mantengono i controlli preesistenti. |
| dmx_512.c ReceiverExpire | Scade la proprietà della transazione, non la validità di un comando che non deve rispondere. Il messaggio completo rimane nello snapshot main; una richiesta scaduta che richieda risposta continua a essere rifiutata. |
| dmx_512.c RX/main | Due snapshot statici FIFO invece di uno, +257 byte di payload più metadati. Copia/pop sotto sezione critica; nessuna allocazione né PID elaborato in ISR. Un terzo pacchetto prima del servizio è scartato e conteggiato; due UN_MUTE sono conservati e il DUB può essere ritentato. Nessuna promessa di coda illimitata sotto main fermo. |
| NodeTryStart / main | La proprietà RX attiva blocca il TX; un messaggio già scaduto e conservato per il main non arresta il Node per sempre. Il main non rimpiazza lo stato con PROCESSING mentre un TX è attivo. Nessun taglio del frame fisico, DE rilasciato al TC o su watchdog TX. |
| rdm.c / dmx_512.c / usb_debug.c | Diagnostica CDC di sola lettura, buffer risposta320 byte. Nessuna stampa o formattazione in ISR. Nessun comando reset/clear aggiunto per mascherare il guasto. |

La separazione controllo/risposta segue [E1.20-2010 §7.6.3/7.6.4 e broadcast §5.3](https://getdlight.com/media/kunena/attachments/42/ANSI_E1-20_2010.pdf). Non è stato introdotto un auto-unmute a tempo: dopo MUTE il dispositivo deve restare muted finché riceve UN_MUTE o viene reinizializzato, mentre GET/SET restano operativi. UID, branding, timeout DMX che spegne i relè, Art-Net/sACN, TIM6, clock, EEPROM e watchdog restano invariati.

### Diagnostica da acquisire sul banco

- `rdm?`: mantiene enabled/UID/rx/late, aggiunge `muted`, `mute`, `unmute`, `dub`, `queued`, `bad`, `busy`. `rx` conta pacchetti validati nel main; `queued` è una risposta discovery accodata, non la prova che URC01 l’abbia ricevuta. La deadline main resta1800 µs; i controlli discovery validi possono agire oltre tale limite senza risposta.
- `rdm_phy?`: `ms`, stato TX, stato bus, profondità `pending`0..2, DE, CR1/CR3/ISR USART, CR1/DIER/SR TIM7, pacchetti RX completi, scadenze, scarti coda, FE/NE/ORE, TC completati e guasti TX, byte USART RXNE, frame DMX pubblicabili e rinvii main per BUSY. Snapshot IRQ-safe; la query non legge RDR né cancella flag, non modifica direzione/stati o EEPROM.
- FE include i BREAK validi, non equivale a errori di linea. `exp` include timeout di RX/pending/ascolto risposta altrui. Stati TX:0 RX,1 BREAK,2 MAB,3 byte TX,4 attesaTC,5 turnaround discovery,6 turnaround normale. Stati bus:0 IDLE,1 SC,2 RDM RX,3 DMX RX,4 PENDING,5 PROCESSING,6 REPLY,7 OTHER_REPLY.
- Rilevare entrambe le query prima e dopo la perdita: confrontare `ms` e conteggi per distinguere tempo/volume, `unmute` applicati da `dub` senza risposta, pacchetti ingressi da CRC/scarti, RE/RXNEIE e DE da stato muted. Non dedurre che la causa sia muted se PHY mostra RE spento, TX bloccato, UART senza attività o main non responsivo.

### Node OUT: risultato separato

La macchina a stati introdotta nel precedente incremento resta valida per richieste **iniziate in RX**: BREAK/SC proteggono fino a pacchetto completo e TC della risposta, con soli timeout di sicurezza. Test per entrambi i Node: GET/SET in RX completano; una richiesta iniettata durante il proprio TX non viene ricevuta né applicata. Questo è previsto, perché il ricevitore USART è disabilitato durante guida del transceiver.

URC01 controller e ADSW100 Node sulla stessa porta/coppia costituiscono due sorgenti. Nessun timer può garantire che il controller esterno inizi proprio nella pausa RX; non si forza una risposta o un BREAK dentro un frame in corso. Per un singolo controller esterno stabile, usare ADSW100 IN; per Node continuo con controllo indipendente serve topologia coordinata/porta separata/gateway, fuori da questa correzione. Non ampliate finestre2/20 ms, lease controller2 s o refresh, non alterati parser Art-Net/sACN.

### Prove dopo la correzione e limiti

Dieci gruppi di `tests/test_stabilization.py` PASS. NODE_RDM eseguito con **AddressSanitizer e UndefinedBehaviorSanitizer**: dieci minuti senza traffico, altri dieci minuti muted senza auto-reset, dieci minuti con discovery periodico, ulteriori614928 ms con15000 frame DMX512 e200 cicli discovery alternati sullo stesso ingresso,70210 transazioni con TN wrap, wrap di HAL_GetTick, GET/SET continui, UN_MUTE broadcast ritardato5 ms, nuovo BREAK prima del main, due snapshot e overflow contato, UN_MUTE unicast tardivo senza ACK e CRC invalido che non cambia mute. Verificato che fine risposta ripristini RE/RXNEIE e DE basso. Le prove Node precedenti e le regressioni EEPROM/relè/protocolli restano PASS.

Build completa GCC ARM150 unità in `/private/tmp/adsw100-rdm-lifetime`; nessuna programmazione hardware eseguita dall’agente. Restano: collaudo IN oltre10–30 minuti con/senza URC01, collegamento del controller dopo almeno5 minuti senza traffico, confronto attorno a240 s, capture CDC al guasto, misura BREAK/MAB/TC e latenze main sotto carico. La correzione dei percorsi riprodotti non è ancora una dimostrazione della sparizione hardware a4 minuti.

### Acquisizione CDC sul firmware già caricato

Su autorizzazione utente, identificate le porte `/dev/cu.usbmodem3571326331331` (ADSW100) e `/dev/cu.usbmodem356E326C31331` (URC01). Nessun reset, flash, SET o comando RS485 inviato dall’agente. L’utente aveva resettato ADSW100 e URC01 era inizialmente in DMX Send: `rdm?` riportava `rx=0 late=0`. Il passaggio manuale a RDM Manager è stato confermato dall’utente alle12:19:07 UTC. La query CDC ogni20 s ha mostrato `rx=47` a100 s dalla raccolta, `rx=560` a240 s, `rx=1073` a380 s, sempre `late=0`. Il tempo della raccolta non è il tempo dal reset. A380 s erano trascorsi circa cinque minuti in Manager; il contatore validato continuava a crescere. Questo dimostra elaborazione delle richieste, non ricezione delle risposte dal controller, né corretto DMX IN senza un nuovo frame DMX Send. La diagnostica caricata non include ancora i registri della nuova query PHY; questa prova non identifica la causa del precedente arresto.

Ultimo campione acquisito dopo oltre dieci minuti dal passaggio a Manager: `rdm=1 uid=17FA:E7164570 rx=2365 late=0` (registrazione conclusa 2026-11-15T12:29:49+00:00). Il contatore continuava a crescere; il successivo riscontro utente conferma tuttavia la scomparsa dal display URC01, con ulteriore rx=6219. Il successivo ritorno a DMX Send senza reset è fallito: LED spento e relè non rispondenti, CDC sempre attiva; vedere aggiornamento sopra. Ripetere anche senza interrogazioni CDC per confrontare le condizioni del guasto originale. Le nuove correzioni non erano caricate, quindi questo esito non può essere attribuito alla nuova build.

### Presentazione IPv4 URC01

Modifica separata sotto URC01: `ON/OFF (Interface 1)`, IPv4 puntato/prefix con interfaccia distinta, gateway puntato, `DNS 1: ...`/`DNS 2: ...`. Nomi degli editor esplicitano input numerico `IF 0/1`, `IF A B C D bits`, `0 A B C D`, `index A B C D`; l’input parte vuoto nel keypad esistente. **NetworkEncode, selettori GET e payload E1.37-2 invariati**, verificati dal confronto del codec e dai test su due interfacce32 bit. Non modificati Chora o Graphics. Il display non viene riletto come payload SET.

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

Nel primo incremento il Model ID numerico **0xFFFF era un placeholder non assegnato**; dal15 novembre2026 e' sostituito dall’assegnazione utente **96/0x0060 sotto17FA**, comune ai tre brand. Nei riferimenti esaminati compaiono RSH7 modello 0x0001, URC01 0x0100, DRV_STG4 0x005E, nel dominio storico 0x0206. Assegnazione ADSW100/NODIM01 registrata nel registro locale, distinta dall’OEM Art-Net. Model description/manufacturer label provengono da Brand_Current, non da stringhe replicate. OEM Art-Net non assegnato: nessuna sostituzione con quello RSH7.

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
| Parzialmente risolto | rdm.c RDM_Init / rdm.h Model ID | Model ID96 assegnato dall’utente e implementato; resta il censimento UID/OUI sotto17FA. Con EUI non riconosciuto RDM resta disabilitato. |
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


## NFC — decisione definitiva 17 novembre 2026

Il contratto corrente sostituisce la proposta hash/firma descritta negli audit
precedenti. Compatibilita' soltanto Manufacturer ID, Model ID, Schema Revision
manuale; sezione07 quattro byte, nessuna costante ROM digest o generazione durante
la build. CRC32 e controlli REQUEST/ACK invariati. ST2FW v3 unico da96byte,
JSON facoltativo senza confronto PID/BIN o versione firmware. Legacy ST2 e
bootloader dei payload invariati. Documentazione normativa in protocol/nfc e
ST2FW_V3_INTEGRAZIONE.md. Test NFC ADSW: 21 transazioni,170 fault I/O,68 tagli
REQUEST,24 Init PASS; build ARM150unita' PASS. Nessun flash, commit o pubblicazione.
