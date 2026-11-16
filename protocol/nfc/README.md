# Studio Due — fondazione NFC condivisa, fase 1

Stato: architettura approvata il 15/11/2026; **formati v1 del catalogo e SDCP2
congelati per l'implementazione successiva**. La successiva integrazione ADSW100 è documentata in fondo; nessuna integrazione
nei firmware di riferimento/client, chiave o firma di produzione è inclusa qui.
Le prove presenti sono host: non certificano RF, timing o power-cut hardware.

Questa cartella è una risorsa autonoma: Python 3.9+, sola libreria standard,
nessun import dagli altri repository, nessun submodule o dipendenza Git.
Può essere copiata integralmente nei repository o estratta successivamente in
un repository condiviso conservando storia, specifica e vettori. Per ogni copia
registrare revisione sorgente e hash dei file; non mantenere fork manuali dei PID.
La posizione ADSW100 è iniziale, non attribuisce tutti i PID al namespace AXN.

## File e comandi

- `registry.json`: 66 chiavi censite; standard e proprietarie separate. I PID
  osservati restano osservazioni dei sorgenti, non registrazioni ESTA nuove.
- `schema.json`: JSON Schema draft 2020-12, root `oneOf` registry/catalog,
  definizioni chiuse. Nessuna proprietà sconosciuta accettata.
- `catalogs/*.json`: schemi ADSW100/17FA:0060 e DRV_STG4/0206:005E, revisione 1.
- `sdcp.py`: validazione strutturale/semantica, generatore, decoder di controllo,
  codec SDCP2/NDEF, modello host del mapping rete e confine API firma.
- `test_sdcp.py`: regressioni e generazione riproducibile dei vettori.
- `vectors/vectors.json`: cataloghi binari in hex e 31 messaggi annotati (8 validi,
  23 invalidi), con livello di verifica sullo schema del modello e motivo atteso;
  include NDEF per i casi validi. UID/device ID di esempio, non dispositivi reali.
- `vectors/fingerprints.json`: fingerprint attesi; modifiche incompatibili non
  si risolvono aggiornando alla cieca il golden, ma revisionando lo schema.
- `vectors/ed25519-rfc8032.json`: fixture pubblica RFC 8032 §7.1 TEST 1 per il
  futuro backend Ed25519. Non contiene una chiave privata.

Dalla root ADSW100:

```sh
python3 -B protocol/nfc/test_sdcp.py
python3 -B protocol/nfc/sdcp.py --output /private/tmp/sdcp-foundation-build --release 1 --key-id release-1
```

Output: un `.sdcat` per modello, fingerprint testuale, `manifest.json` canonico
e `signing-input.bin`. **Sono artefatti NON FIRMATI**, da non installare come
pacchetto fidato. Il comando base non scrive file firmware (export C soltanto con opzione esplicita); nessun timestamp,
percorso assoluto, RNG o endian host entra negli artefatti.

Rigenerare i vettori solo dopo revisione intenzionale del contratto:

```sh
cd protocol/nfc
python3 -B -c 'import json, test_sdcp, sdcp; (sdcp.ROOT/"vectors/vectors.json").write_text(json.dumps(test_sdcp.vector_set(), indent=2, sort_keys=True)+"\n")'
```

Il formato dati del registro è validato dal JSON Schema; il validatore senza
librerie implementa esattamente i costrutti usati nelle due definizioni. Chiavi
JSON duplicate, NaN, booleani al posto di interi, proprietà ignote e stringhe
non ammesse vengono rifiutati. La radice schema è anche utilizzabile da normali
validatori draft 2020-12. I controlli incrociati seguenti sono aggiuntivi.

## Registro, supporto e controlli semantici

Chiave testuale: `std:PPPP` oppure `mfr:MMMM:PPPP`, hex maiuscolo a quattro cifre.
Standard: PID 0001..7FFF. Proprietari: 8000..FFDF e manufacturer diverso da
0000/FFFF. FFE0..FFFF non sono assegnabili dal registro. Un numero uguale sotto
manufacturer diverso è lecito. Un Model ID diverso non consente significati
incompatibili sotto la stessa coppia manufacturer/PID.

Ogni voce dichiara identificatore semantico stabile, tipo canonico NFC, selettore,
unità, range base, enum, descrizione dei formati RDM GET/SET, provenienza e stato.
Le voci `opaque` completano il censimento senza inventare contratti ancora non
verificati: non possono entrare in un catalogo NFC editabile finché non vengono
specificate. Questo vale anche per le azioni di servizio censite ma escluse.

Un catalogo modello può restringere range/enum del registro, non allargarli,
ridefinirne codici o cambiare unità e tipo. Sono controllati chiavi duplicate,
manufacturer del prodotto, intervalli, dimensioni, default, selettori, ASCII,
enum, numero di parametri e numero di istanze. La stessa voce DNS ha due istanze,
non due assegnazioni PID. Le etichette enum del modello devono concordare con il
registro quando l'enum ha significato comune; Personality è esplicitamente
modellata con le scelte del prodotto.

Le etichette sono ASCII stampabile 1..63, volutamente senza Unicode in questa
prima versione; tipo ASCII del valore: 0..64 byte secondo range. La futura app
può tradurre la UI, ma il file v1 non contiene traduzioni Unicode. Nessun campo
catalogo contiene script o codice da eseguire; `binding` è un ID dichiarativo.
Brand, policy menu, Factory INIT, segreti e calibrazioni non vengono abilitati
alla configurazione NFC ordinaria dal semplice fatto di avere un PID.

## Catalogo binario `.sdcat`, versione 1

Interi big endian, nessun padding. Lunghezza massima file 65536 byte, minimo 64;
limite 64 parametri/istanze, 16 selettori per parametro, 64 scelte per enum.
Header **60 byte**:

| Offset | Byte | Campo |
| --- | ---: | --- |
| 0 | 4 | Magic ASCII `SDC2` |
| 4 | 2 | Versione binario = 1 |
| 6 | 2 | Flags = 0 |
| 8 | 4 | Lunghezza totale, CRC incluso |
| 12 | 2 | Manufacturer ID |
| 14 | 2 | Model ID |
| 16 | 4 | Revisione schema, 1..FFFFFFFF |
| 20 | 32 | Fingerprint schema SHA-256 |
| 52 | 4 | Lunghezza contratto |
| 56 | 4 | Lunghezza presentazione |
| 60 | variabile | Contratto, poi presentazione, poi CRC32 di 4 byte |

Contratto: `count:u16`, `category:u16`, poi record ordinati per
`(namespace,manufacturer,pid)`, unici. Ogni record è preceduto da `length:u16`
che conta i byte successivi del solo record:

```text
namespace:u8 manufacturer:u16 pid:u16 type:u8 selector_type:u8
access:u8 unit:u8 selector_count:u8 selectors[]
minimum:i64 maximum:i64 default_length:u8 default[] choice_count:u8
choice_values[] semantic_length:u8 semantic[] binding_length:u8 binding[]
```

Namespace 0=standard (manufacturer=0), 1=proprietario (manufacturer del modello).
Access 0=lettura, 1=lettura/scrittura. Selettori ordinati/unici: tipo none ha
count=1 e nessun byte selettore; u8/u32 usano rispettivamente 1/4 byte per istanza.
I valori enum usano la larghezza e il segno del tipo. Stringhe semantic/binding
ASCII 1..63 senza NUL. Default è un valore canonico, non una stringa da mostrare.

| Tipo | Codice | Byte canonici |
| --- | ---: | --- |
| opaque | 0 | Solo registro; vietato nel catalogo editabile v1 |
| u8, u16, u32 | 1, 2, 3 | 1, 2, 4 |
| i8, i16, i32 | 4, 5, 6 | 1, 2, 4, complemento a due |
| bool | 7 | 1, esclusivamente 0 o 1 |
| ascii | 8 | 0..64 secondo min/max lunghezza |
| ipv4 | 9 | 4 ottetti nell'ordine di rete |
| ipv4_prefix | 10 | 4 ottetti + prefisso 0..32 |
| route | 11 | route ID:u32 + IPv4:4; nel binding ADSW ID=0 |

Selector type: 0=none, 1=u8, 2=u32. Unit: 0=none, 1=second,
2=minute, 3=hertz, 4=level. Tipo/unità/codice ignoto: catalogo non utilizzabile
in questo motore v1; niente interpretazioni numeriche euristiche.

Presentazione: nome modello come `length:u8 + ASCII`, poi, nello stesso ordine
del contratto, label parametro e label di ogni scelta con identico framing.
Non esistono count impliciti derivati da fine file: tutto deve terminare esattamente
alle lunghezze dichiarate. Il CRC32 finale copre header e due segmenti.

Fingerprint = SHA-256 della concatenazione:

```text
ASCII "SDCP2-SCHEMA" + byte 00
manufacturer:u16 model:u16 schema_revision:u32
contratto binario completo (count/category/record)
```

Presentazione esclusa; semantica, binding, default, enum numerici, range,
selettori e permessi inclusi. Il cambiamento dei soli nomi non richiede firmware
nuovo. L'hash SHA-256 del file intero nel manifest protegge anche i nomi.
Una nuova categoria/binding/default modifica il contratto e richiede revisione.

Il decoder host ricalcola hash/CRC, controlla lunghezze/ordinamento/limiti e
ricodifica per verificare l'unicità della rappresentazione. Fingerprint e CRC
non sono autenticazione. In firmware il loader richiederà anche manifest firmato.

## SDCP 2.0 e NDEF: contratto wire congelato

Header 6 byte: `53 44 20 00`, `total_length:u16`; CRC32 finale 4 byte.
Lunghezza SDCP 10..4096, massimo 16 sezioni. NDEF Type 2 standard, un solo MIME
`application/vnd.studiodue.sdcp` (30 byte), TNF=2, MB=ME=1, CF=IL=0.
Accettare SR e non-SR; generatore SR per payload <=255. TLV `03 len NDEF FE`
o `03 FF len:u16 NDEF FE`; dopo FE consentito solo padding zero entro capacità.
Verificare sempre CC/capacità reale: il parametro predefinito 816 non è una
rilevazione del chip e non autorizza a cambiare CC/lock.

Sezione: `id:u8 flags:u8 length:u16 data[]`; ordine crescente, ID non zero,
nessun duplicato. Flag bit 0 CRITICAL, altri vietati. Tutte le quattro sezioni
obbligatorie devono essere critiche. Ignote critiche rifiutate; ignote non
critiche conservate integralmente dal client durante una transazione.

| ID | Corpo |
| --- | --- |
| 01 | manufacturer:u16, model:u16, category:u16, software:u32, UID RDM:6; 16 byte |
| 02 | VALUES, record indicizzati ordinati per PID, poi selettore in ordine lessicografico |
| 07 | schema_revision:u32 + SHA-256:32; 36 byte |
| 80 | SYNC1: version:u8=1, kind:u8, revision:u32, request_id:u32, applied_id:u32; 14 byte |

VALUES: `pid:u16 selector_length:u8 selector[] value_length:u8 value[]`.
Selettore 0/1/4 byte, valore massimo 64, 1..64 istanze. Chiave univoca
PID+selettore. PID zero o FFE0..FFFF rifiutati. Namespace implicito standard o
manufacturer del documento come nel registro; niente subdevice o più manufacturer
nello stesso documento. L'insieme di istanze deve corrispondere allo schema
attivo; una variazione di supporto richiede uno schema diverso, non una lista
parziale indistinguibile da un pacchetto troncato. Nessuna whitelist di modelli.

UID deve avere lo stesso manufacturer di IDENTITY e non essere broadcast.
SDCP è device-only, flags 0: nessun PROFILE ereditato tacitamente da SDCP1.
Software ID non è schema ID e non seleziona regole specifiche nel client.
I valori sconosciuti sono consultabili raw e non modificabili; senza schema
esatto verificato il client non abilita scritture. I byte immutabili e le sezioni
non note non possono essere eliminati o cambiati dalla REQUEST.

SYNC snapshot kind=0: request=applied. Request kind=1: request=next(applied),
incremento UINT32 con wrap FFFFFFFF->1; zero saltato. Revisione non zero, deve
coincidere esattamente con la base letta; la pubblicazione di ACK viene fatta
soltanto dopo commit persistente verificato. Il parser non sostituisce il
verificatore persistente di replay: quest'ultimo resta da integrare nel target.
Le modifiche ai campi read-only vengono rifiutate confrontando richiesta e snapshot.

Tutti i CRC usano CRC-32/ISO-HDLC riflesso EDB88320, init/xorout FFFFFFFF,
`123456789 -> CBF43926`, salvataggio MSB-first; nessun checksum RDM riutilizzato.
Il tag non contiene descrittori, nemmeno per fallback: solo le quattro sezioni
necessarie ed eventuali estensioni future non descrittive approvate.

## Mapping ADSW rete e protezione legacy

La chiave standard è normalizzata per NFC: il selettore non viene duplicato
nel valore. Questo non cambia i payload RDM, che il futuro adapter comporrà.

| Chiave | Selettore | Valore NFC | Parametri/offset persistenti ADSW |
| --- | --- | --- | --- |
| std:0703 | u32=1 | bool 1 byte | use_dhcp, 03 |
| std:0706 | u32=1 | IPv4:4 + prefix:1 | ip_address 04..07, net_mask 08..0B |
| std:070A | vuoto | u32=0 + gateway:4 | gateway 0C..0F |
| std:070B | u8=0 | IPv4:4 | dns1 10..13 |
| std:070B | u8=1 | IPv4:4 | dns2 14..17 |

Importante: `EE_Put32` salva **big endian il numero raw lwIP**. Su STM32
little endian, IP 192.168.1.20 ha raw 0x1401A8C0 e byte EEPROM `14 01 A8 C0`;
il valore NFC è invece `C0 A8 01 14`. Non copiare direttamente quattro byte
EEPROM nel messaggio. I test coprono questa differenza e tutti i 33 prefissi.
Maschera non contigua: errore, nessuna normalizzazione/scrittura automatica.

Il valore canonico 0706 è il tuple **persistente configurato** anche con DHCP=1;
non è il GET 0705 corrente e non cancella IP/mask quando DHCP è attivo.
Il tipo route usa la forma di configurazione SET con ID=0 supportata da ADSW,
non copia il GET route che può restituire interface=1. Non si promette che la
procedura NFC sia identica a sequenze SET/APPLY E1.37-2: è un candidato batch
CFG, con gli stessi dati salvati e convalida finale del target. `0709` non è un
nuovo campo da salvare. Non esistono due insiemi statico/DHCP persistenti separati.

`ads_network_decode/encode` è esclusivamente un modello host: nessun accesso al
bus. I test preservano integralmente 00..02 e 18..7F (brand, menu, ruolo,
metadati e byte indefiniti compresi); DHCP attivo con tuple non zero fa round-trip.
In firmware il futuro adapter deve usare CFG_Get/Validate/Set e il salvataggio
conservativo autorizzato, non una nuova funzione che bypassa le protezioni EEPROM.

Metadati NFC autorizzati 28..37: discriminante/versione/stato, revisione dinamica,
request ID, CRC richiesta; ancora non implementati. Schema e SHA-256 statici
stanno in flash. La fondazione non risolve l'atomicità dei salvataggi in place:
prima del porting vanno testati stati preparati, commit interrotto e recupero
conservativo, senza applicare nuovamente richieste già elaborate o dati obsoleti.

## PWM DRV: enum a un byte

Come precisato dall'utente, sul wire 8200 rimane un byte: 0..6, mostrati F1..F7.
Non si trasmettono Hz o float. Solo per tracciabilità, i codici attuali nella
sorgente corrispondono a 1000, 4000, 2000, 324, 10376, 20000, 20757 Hz nominali.
Non ordinare i codici per frequenza. La scelta del nome F1 non introduce il
valore esadecimale F1: F1 è label del codice 0, F2 del codice 1, ecc.

DRV è modellato sul profilo corrente RGBWA con cinque personality; le altre
configurazioni hardware del prototipo richiederanno schemi espliciti, non
riutilizzo cieco del fingerprint. Validatori contestuali Smooth/Ramp e footprint
restano nel target; i controlli host di tipo/range non certificano tali regole.

## Pacchetto e futura firma Ed25519

`manifest.json`: ASCII JSON canonico, chiavi ordinate, nessuno spazio o newline,
nessun numero float o booleano; campi esatti `format=1`, `release` UINT32>=1,
`key_id` ASCII `[a-z0-9_-]{1,32}`, `files` ordinati per path. Ogni file ha
`path`, `size`, `sha256` lowercase 64 hex. Path relativo limitato dal validatore,
nessun `..`, prefisso slash o doppio slash. Massimo 256 file, ciascuno <=65536.
Il sottoinsieme ASCII/interi evita dipendenze da canonicalizzatori JSON generici.

Messaggio Ed25519 da firmare:

```text
ASCII "SDCP2-CATALOG-PACKAGE" + byte 00 + byte esatti manifest.json
```

Algoritmo fissato Ed25519 per manifest v1, non scelto dal pacchetto. Firma
sidecar `manifest.sig` di esattamente 64 byte, chiave pubblica fidata di 32 byte,
scelta tramite key_id ma già installata nel trust store. Nessuna chiave privata
in questo repository. Il nome release-1 è un ID proposto, non una chiave operativa.

`verify_package` richiede esplicitamente un callback verificatore crittografico:
senza firma/chiave/callback validi rifiuta. I test del confine usano uno stub
riconoscibile e controllano alterazioni, chiavi ignote, manifest e file modificati;
**non attestano una verifica Ed25519 reale**. La fixture RFC serve alla futura
integrazione, da provare anche su firme errate e messaggi modificati con un backend
verificato. Nessuna libreria crittografica aggiunta ai firmware in fase 1.
Il futuro loader deve prima chiamare `decode_manifest` sui byte ricevuti:
rifiuta duplicati e serializzazioni non canoniche, senza cambiare i byte firmati.

La verifica deve precedere l'attivazione: staging, firma, hash/struttura,
ricostruzione fingerprint, scrittura completa, attivazione recuperabile al reboot,
con generazione precedente conservata. Il rename FAT da solo non è atomico a
power-cut. Offline sono ammessi pacchetti già verificati; autenticità non dipende
da cloud. Rotazione/revoca chiavi e contatore anti-rollback client rimangono da
integrare; la SD rimovibile da sola non fornisce anti-rollback sicuro.

Riferimento fixture: [RFC 8032, sezione 7.1](https://www.rfc-editor.org/rfc/rfc8032).
Firma del catalogo non equivale ad autenticare il tag o ad autorizzare i SET NFC.

## Aggiungere un PID o un modello

1. Cercare la coppia namespace/manufacturer/PID nei registri e sorgenti pertinenti.
   Riutilizzare il PID quando contratto/semantica coincidono; fra manufacturer
   diversi non è obbligatorio. Se il contratto è incompatibile nello stesso
   manufacturer, assegnare un nuovo PID con approvazione, senza inventarlo.
2. Aggiungere la voce al registro: semantica, tipo, selector, unità, range/enum,
   formati GET/SET, origine e stato. Non abilitare `opaque` per aggirare verifiche.
3. Aggiungere o revisionare il catalogo modello, indicando solo istanze supportate,
   default verificati, permessi e binding. Nessuna condizione Model ID nel client.
4. Eseguire validatore, generatore e suite. Un nuovo tipo richiede una revisione
   esplicita del motore/formato; un nuovo modello con tipi noti è solo dati.
5. Congelare revisione/hash, rivedere i golden con motivazione e aggiungere vettori
   positivi/negativi per nuovi selettori o vincoli. Non sovrascrivere il contratto
   di una revisione distribuita: archiviare anche le versioni necessarie ai prodotti
   installati. Il generatore fase 1 produce una release con i modelli sorgente
   presenti; la gestione di più revisioni storiche nel pacchetto è lavoro successivo.
6. In fase successiva incorporare in flash del prodotto il fingerprint generato,
   mantenendo il controllo server nelle API condivise. Firmare il pacchetto nel
   processo di rilascio separato, verificare su URC/app e distribuire i soli dati.

Restano da implementare loader SD/cache, fallback in flash, trust store/verifica
Ed25519 reale, cataloghi storici/attivazione sicura e adapter firmware. Né questa
cartella né i vettori programmano o modificano ADSW100/URC01/DRV_STG4.

## Esportazione dati C per ADSW100 — fase 2

Il generatore aggiunge un export opzionale, senza modificare wire, cataloghi,
manifest o fingerprint approvati:

```sh
python3 -B protocol/nfc/sdcp.py --output /private/tmp/adsw-nfc-catalog --model ADSW100 --firmware-header Core/Inc/nfc_catalog.h
python3 -B tests/test_nfc.py
```

L'header è l'unico file firmware nuovo: dati schema/istanze generati, nessun
codec duplicato o descrittore nel tag. Il test confronta l'header ai dati generati.
La risorsa condivisa resta autonoma: l'export accetta un catalogo sorgente per
nome, non introduce whitelist nei client. Il test firmware vive nella root ADSW
separatamente dalle regressioni portabili della fondazione.

Nel solo ADSW100 fase2 sono implementati snapshot/REQUEST/ACK e Init Memory;
le API EEPROM preesistenti restano sincrone. Software ID del prodotto resta1.4.0,
distinto dal formato SDCP2. Il consenso Init usa lo stato metadata E8 dopo
verifica dei default (D7 è preparazione interrotta): il servizio inizializza il
proprio tag e consuma l'intenzione dopo readback. Non interpreta automaticamente
il JSON legacy come configurazione e non elimina contenuti estranei/illeggibili.
CC nuovo solo con consenso Init e verifiche/probe2k del riferimento DRV; CC validi
invariati. Dettagli e limiti fisici sono nel documento AUDIT ADSW100.
