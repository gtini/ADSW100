# ST2FW v3 e NFC: contratto corrente

Decisione definitiva del 17 novembre 2026. Un solo ST2FW per tutti i prodotti,
con JSON NFC facoltativo. Nessun firmware ST2FW è stato venduto: i contenitori
sperimentali precedenti non sono supportati. I file `.ST2` legacy distribuiti,
i loro parser, checksum e trasporti restano invariati.

## Compatibilità NFC e responsabilità dell'autore

La chiave è soltanto **Manufacturer ID, Model ID, Schema Revision**.
Non esistono digest di schema, costanti ROM di riscontro, confronto PID/BIN,
firme, chiavi o generatori Python nella compilazione.

La revisione è un intero uint32 non zero gestito manualmente:

- PID, selettori, tipi, permessi, range, enum, default o significato incompatibili:
  incrementare la revisione prima della distribuzione.
- Sole descrizioni/etichette: mantenere la revisione.
- Nuovo firmware con lo stesso contratto NFC: mantenere la revisione.

Il Builder non verifica che i PID siano implementati nel BIN. Controlla struttura
JSON e identificativi, preserva i byte dei file scelti e calcola offset e CRC.
Le API del dispositivo rimangono autoritative per validare ogni REQUEST.

## Header unico, 96 byte

Tutti gli interi sono little endian, senza padding C. Nessun flag NFC.
`catalog_length > 0` identifica la presenza del catalogo.

| Offset | Byte | Campo |
|---:|---:|---|
| 0 | 4 | Magic ASCII ST2F |
| 4 | 2 | Versione = 3 |
| 6 | 2 | Dimensione header = 96 |
| 8 | 4 | Product ID, dal descriptor SDI1 |
| 12 | 4 | Target ID, dal descriptor SDI1 |
| 16 | 4 | Versione firmware, dal descriptor SDI1 |
| 20 | 4 | Offset firmware = 96 |
| 24 | 4 | Dimensione BIN fisico finalizzato |
| 28 | 4 | CRC32 del solo BIN |
| 32 | 4 | catalog_offset |
| 36 | 4 | catalog_length |
| 40 | 4 | catalog_crc32 |
| 44 | 2 | Manufacturer ID RDM/NFC |
| 46 | 2 | Model ID RDM/NFC |
| 48 | 4 | schema_revision |
| 52 | 32 | Firmware model UTF-8, 1..31 byte, NUL e padding zero |
| 84 | 8 | Riservati, zero |
| 92 | 4 | CRC32 dei primi 92 byte dell'header |
| 96 | variabile | BIN originale, immutato |
| catalog_offset | catalog_length | JSON facoltativo, contiguo al BIN |

Senza catalogo: offset, lunghezza, CRC catalogo e revisione sono tutti zero.
Manufacturer/Model possono restare noti; il Builder generico usa zero se non
forniti. Non derivarli dal Product ID: sono domini distinti.
Con catalogo: offset=96+firmware_size, lunghezza 1..65536, revisione non zero;
Manufacturer/Model sono quelli del JSON e vengono confrontati dal client col tag.
Non esiste un campo schema_fingerprint, neppure riservato o azzerato.

Dimensione file esatta:96+firmware_size+catalog_length. Nessun buco, trailer o
sovrapposizione. CRC-32/ISO-HDLC, `123456789 -> CBF43926`, memorizzato LE.
CRC catalogo può legittimamente risultare zero: non è un indicatore di assenza.
Header, BIN e JSON hanno CRC separati; nessun ulteriore CRC globale.

I loader firmware non interpretano JSON. Il parser PC verifica anche il suo CRC;
URC01 separa le responsabilità: l'Uploader verifica il BIN, il client NFC verifica
il CRC JSON prima di usarlo. Un catalogo corrotto non viene interpretato e porta
al fallback RAW. Il payload trasmesso/programmmato non contiene mai JSON o header.
Descriptor SDI1, commit SDC1, padding fisico, canonical image e CRC16 DMX restano
quelli esistenti. Non si modifica alcun bootloader che riceve solo l'immagine estratta.

## JSON e microSD

JSON autonomo per modello/revisione, mantenuto manualmente nel progetto. L'export
Python disponibile è facoltativo per conversioni/test; non è invocato dalla build.
Il formato JSON corrente mantiene i campi già usati dal lettore streaming:
format, manufacturer, model, revision, category, name, parameter_count, parameters.
Ogni parametro dichiara namespace/manufacturer, PID, tipo, selettore/istanze,
accesso, unità, range, default, scelte, semantic, binding e label. Nessun hash.

Il Builder accetta qualsiasi ordine dei campi. Il parser firmware corrente
richiede l'ordine documentato dai JSON di esempio in `protocol/nfc/json/`;
whitespace ed escape JSON leciti restano ammessi. Conservare l'ordine durante
l'editing manuale. Una serializzazione non supportata produce fallback RAW,
non interpretazioni euristiche. Non ci sono registri generati durante la compilazione.

URC01 cerca i pacchetti in `/firmware/`, leggendo header ST2FW e descriptor SDI1,
non il corpo del BIN. Filtra la tripla NFC e fa seek diretto al JSON. Verifica
CRC, struttura, corrispondenza della tripla e valori/istanze. Non sceglie il firmware
più recente: un altro pacchetto compatibile viene provato se il primo è corrotto.
Scansione cooperativa per nuovo documento/generazione SD, senza database o polling
continuo. SD assente/rimossa, catalogo mancante/incompatibile: RAW resta disponibile.
I JSON separati sono stati tolti dal template ordinario `sdcard/`.

Restano invariati `/firmware/`, `/ST2/UPDATE/`, `/System/Themes/`, configurazioni
Operating.cfg/bak, `/cloud/`, archivi calibrazione, profili e aggiornamenti NINA.
Il cloud esistente conserva i file come blob: nessun servizio nuovo. Sono
aggiornati soltanto i consumer locali che interpretavano esplicitamente v2.

## Censimento dei consumer

- Python comune: `RDM_SPLITTER_H7/Tools/st2fw_format.py`, copiato in Tools DRV,
  shared PC e tools URC. Nessuna sincronizzazione massiva dei backend USB.
- Builder: servizio, selezione BIN/JSON facoltativo e UI; generazione ST2 invariata.
- PC: loader formato comune, estrazione BIN; estensioni G474/H1 preservate.
- URC01: parser streaming, Uploader/self-update, packaging, cloud catalog e client NFC.
- Boot RSH7/DRV/URC: ricevono solo SDI1 estratto, nessun parser ST2FW aggiunto.
- Uploader Python attuale: non aveva un loader ST2; il supporto storico resta
  nei programmi/trasporti legacy e in URC01, senza inventare un nuovo protocollo USB.

## DRV_STG4 e ADSW100

DRV_STG4 pubblica SDCP 2.0 con revisione manuale per ciascuna configurazione
strutturale; il firmware continua a validare i valori e rifiuta REQUEST di schema
differente. ADSW100 usa la stessa sezione 07 di quattro byte e non contiene
più il digest ROM. Le sorgenti e i test dei due target sono stati aggiornati.
I bootloader e il protocollo dei dispositivi legacy ST2 distribuiti restano invariati.

Le verifiche host coprono codec, CRC, REQUEST/ACK, tagli di alimentazione simulati,
lettura del catalogo incorporato, fallback RAW e aggiornamento del solo BIN.
Le build ARM dei tre firmware sono state verificate; la prova RF sui dispositivi
fisici resta da eseguire.
