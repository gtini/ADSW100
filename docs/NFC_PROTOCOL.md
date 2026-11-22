> Archivio della proposta del 15 novembre 2026, conservato per il censimento e
> l'analisi hardware. Le proposte di fingerprint, firma, catalogo binario e fallback
> ROM riportate sotto sono ritirate. Il contratto corrente e' in
> [protocol/nfc](../protocol/nfc/README.md), [revisione manuale](NFC_REVISIONE_MANUALE.md)
> e [ST2FW v3](ST2FW_V3_INTEGRAZIONE.md): Manufacturer/Model/Revision, JSON
> facoltativo, CRC32, nessun hash o firma. Le stime basate sul vecchio schema
> di 36 byte non descrivono il wire corrente di quattro byte.

# NFC comune — registro PID e catalogo aggiornabile

## Stato fase 2 — ADSW100 implementato

Approvazione fondazione ricevuta. Il firmware ADSW100 usa ora NDEF/SDCP2/SYNC1 con catalogo/hash invariati; nessuna modifica a URC01 o DRV_STG4. Dettagli effettivi e limiti in [AUDIT.md](AUDIT.md), mappa metadata in [EEPROM_MIGRATION.md](EEPROM_MIGRATION.md). Sono implementati snapshot, richieste, ACK, deduplicazione e Init Memory con consenso persistente per il tag; le prove restano host/build ARM e il collaudo RF/power-cut fisico è da eseguire. Le sezioni storiche successive descrivono analisi e fondazione, non sostituiscono questi esiti.

Data: 15 novembre 2026. **Architettura approvata dall'utente; fondazione fase 1 implementata.**

La specifica normativa congelata, i comandi e i controlli sono in
[protocol/nfc/README.md](../protocol/nfc/README.md). Registro e sorgenti JSON,
JSON Schema, catalogo binario v1, generatore/validatore Python senza dipendenze,
SDCP 2.0 e vettori sono nella stessa cartella autonoma. Nessun firmware modificato.

Decisioni definitive: catalogo esterno senza descrittori nel tag, SDCP2/NDEF/SYNC1,
namespace standard oppure manufacturer+PID, SHA-256 dello schema statico e futura
firma Ed25519 del pacchetto. URC microSD/fallback in flash e futura app useranno gli
stessi dati; cloud successivo, nessuna whitelist dei modelli. La retrocompatibilità
NFC dei prototipi non è richiesta; quella EEPROM ADSW resta obbligatoria.

Verifica fase 1: 14 gruppi host, 31 vettori SDCP (8 validi, 23 invalidi), cataloghi
binari e NDEF golden, generazione ripetuta identica e fingerprint verificati;
round-trip della rete EEPROM con DHCP attivo e prefissi 0..32. PWM enum UINT8
0..6/F1..F7, senza Hz sul wire. Cataloghi generati 1340 byte ADSW, 1621 byte DRV;
NFC snapshot default 233 e 238 byte, escluso padding. Nessuna misura RF/build ARM.
Ed25519: formato/input/fixture e confine API preparati, verifica crittografica reale
non implementata. Restano adapter firmware, loader SD/fallback e commit persistente.

Le sezioni seguenti conservano censimento e confronto architetturale; le specifiche
puntuali di prima proposte sono ora precisate dal README normativo. Le assegnazioni
osservate non sono dichiarazioni di registrazione ESTA; quelle non verificate
restano opaque e non entrano nei cataloghi editabili.

## 1. Raccomandazione e decisione richiesta

Si raccomanda **catalogo esterno aggiornabile, con copia locale verificata e
fallback minimo in flash**, e tag contenente identità, riferimento allo schema,
parametri supportati/valori e transazione. Il registro è centralizzato come
processo di assegnazione, non come dipendenza da un server durante l'uso.

Il firmware generico URC01 e l'app futura interpreteranno gli stessi tipi di
base e gli stessi file catalogo. Un nuovo modello richiederà distribuire un
catalogo dati, non un aggiornamento specifico del programma. Una primitiva o
un'operazione nuova e sconosciuta potrà comunque richiedere un aggiornamento del
motore generico: un catalogo non deve contenere codice o script da eseguire.

Conviene adottare **SDCP 2.0**, conservando NDEF e la semantica SYNC1. Il salto
esplicito evita ambiguità con SDCP 1.0 e permette parametri indicizzati senza
inventare nuovi PID per ogni DNS/interfaccia. Il porting della macchina a stati
DRV resta riutilizzabile; cambiano codec e risoluzione del catalogo, non serve
riscrivere driver e logiche DMX/RDM già funzionanti.

Il fallback in flash non rende universale il funzionamento offline: un modello
nuovo sarà configurabile offline solo se il relativo catalogo è stato installato
prima, oppure se è già compreso nel fallback. In assenza del catalogo esatto si
mostrano identità e valori raw, con **scrittura disabilitata**. Questa è la
principale rinuncia rispetto a un tag completamente autodescrittivo.

## 2. Evidenze e ambito del censimento

Sono stati esaminati i working tree, non soltanto gli header o il nome del branch.

| Progetto | File rilevanti e riferimento |
| --- | --- |
| ADSW100 | HEAD `743afc18deeaff21f454f9d59184430349f933c2`; `Core/Inc/rdm.h`, `defs.h`, `Core/Src/rdm.c`, `cfg_io.c`, `eeprom.c`, `nfc_i2c.c`, `main.c` |
| DRV_STG4 | `/Users/gianlucatini/Dropbox/Firmware_Git/FIRMWARE_DRV_STG4`; `Core/Inc/rdm.h`, `defs.h`, `Core/Src/rdm.c`, `defs.c`, `nfc.c`, `eeprom.c` |
| RSH7 | `/Users/gianlucatini/Dropbox/Firmware_Git/RDM_SPLITTER_H7`, HEAD `d288fb2d06598e3cd254188ef4158e2145768b35`; `Core/Inc/rdm.h`, `Core/Src/rdm.c` |
| URC01 | `/Users/gianlucatini/Dropbox/Firmware_Git/URC01`, HEAD `b7f9af02ba54fab7c1d6d87d91fd70bc3c645a27`; `rdm_device.c/h`, `rdm_parameters.c`, `sdcp.c`, `nfc_sdcp.c`, `config_profiles.c`, servizi storage |

L'implementazione NFC completa DRV `Core/Src/nfc.c` corrisponde alla revisione
`0578567286a353c4da89725b08d9951ab3135dcd`, SHA-256
`e6ab0cd0bd8484010973e3b80226ffabe4986108f72a57b94256f5616b33060a`;
è stata individuata anche la copia in `/Users/gianlucatini/micro/Studio Due/DRV_STG4`.
Le modifiche pregresse nei repository non sono attribuibili a questa analisi.

### Stato NFC effettivo

- ADSW100 usa ancora JSON `ver:1`, MIME `application/vnd.FGM.adsw100.cfg`, magic
  `ADSW100-<MAC>`; non contiene ancora il porting SDCP. Il JSON non comprende
  l'intera configurazione corrente e non ha revisione/request persistenti.
- DRV usa NDEF MIME `application/vnd.studiodue.sdcp`, SDCP `0x10`, valori,
  descrittori, CHOICES e SYNC1. Il catalogo NFC ordinario comprende 19 PID,
  con 17 descrittori proprietari. Altri PID RDM non appartengono a tale catalogo.
- Il codec `SDCP_Decode()` URC è generale, ma il servizio `nfc_sdcp.c` abilita
  scrittura e conferma tramite `DrvStg4()`, identità note, lista PID e CRC di
  descrittori noti. Anche `NFC_SDCP_Refresh()` contiene controlli di modello.
- Il decoder target DRV rifiuta sezioni extra e flag non previsti: non basta
  aggiornare un file sulla SD per rendere generici i firmware attuali.
- URC ha già FatFs, servizi di storage e pubblicazione di file/profili. Questo
  offre codice riutilizzabile, non prova l'esistenza di un catalogo firmato o di
  un verificatore crittografico adatto: questi componenti vanno verificati prima
  dell'implementazione, senza aggiungere ora dipendenze.

## 3. Registro PID: namespace e regole

Questo censimento costituisce una **bozza del registro interno da ratificare**,
non una registrazione ESTA né una dichiarazione di copertura di ogni prodotto
storico Studio Due. Prima di assegnare un nuovo PID verificare anche gli altri
prodotti/rami e gli eventuali registri aziendali; non dedurre disponibilità dalla
sola assenza nei quattro progetti.

Chiave del registro:

- standard: `(namespace=standard, PID)`, senza attribuirlo al manufacturer del prodotto;
- proprietario: `(namespace=manufacturer, Manufacturer ID, PID)`;
- supporto del modello: `(Manufacturer ID, Model ID, schema_revision, schema_fingerprint)`.

Il Model ID non rende lecita una ridefinizione incompatibile dello stesso PID
proprietario sotto lo stesso manufacturer. Il catalogo modello seleziona un
sottoinsieme e può restringere limiti ammessi; non cambia formato, unità,
polarità booleana o significato di un enum già assegnato. Un significato
incompatibile richiede un PID diverso. Fra manufacturer diversi non si impone
univocità del numero: il riuso per funzioni identiche è preferito, non obbligatorio.

Nel registro separare contratto RDM GET, contratto SET, valore canonico NFC,
selettori, unità, enum, persistenza, privilegi e provenienza. PID standard non
riclassificati come proprietari; ID di stato, codici di prodotto/uploader,
pseudocampi UI e costanti numeriche simili non sono assegnazioni PID.

### Identità riscontrate nei sorgenti

| Prodotto | Manufacturer ID | Model ID | Nota |
| --- | --- | --- | --- |
| ADSW100 / NODIM01 | `17FA` | `0060` (96) | Tutti i brand, assegnazione dell'utente |
| DRV_STG4 | `0206` | `005E` (94) | Valori del prototipo esaminato |
| RSH7 | `0206` | `0001` | La tabella RDM esaminata usa 0206 anche con label AXN |
| URC01 responder | `0206` | `0100` | Da `rdm_device.h`; distinto dal ruolo di controller |

Non sostituire automaticamente gli ID letti con quelli suggeriti dal nome
commerciale. I riferimenti non dimostrano un'assegnazione ufficiale esterna di
ogni Model ID; prima del rilascio ratificare il registro interno completo.

### Proprietari ADSW100 — namespace `17FA`

Da `rdm.h`, tabella `custom[]`, GET/SET e PARAMETER_DESCRIPTION in `rdm.c`;
validazione operativa in `cfg_io.c`. Numeri unsigned; UINT16 big endian.

| PID | Significato | Valore GET/SET | Range/semantica |
| --- | --- | --- | --- |
| `9100` | Funzione porta DMX | UINT8 | 0 IN, 1 ArtNet Node, 2 sACN Node |
| `9101` | Modalità sorgenti | UINT8 | 0 DMX, 1 Art-Net, 2 sACN, 3 Auto; distinta dal ruolo porta |
| `9102` | Universo Art-Net | UINT16 | 0..32767 |
| `9103` | Universo sACN | UINT16 | 1..63999 |
| `9104` | Trasporto sACN | UINT8 | 0 multicast, 1 unicast |
| `9105` | Priorità minima sACN | UINT8 | 0..200 |
| `9106` | Ordine Auto | UINT8 | 0..5; copiare la tabella reale dei sei ordini nel catalogo |
| `9107` | Display Auto OFF | UINT8 | 0 disabilitato, 1 abilitato |
| `9108` | Timeout spegnimento relè | UINT8 | 0..30 secondi; non è il timeout del display |

Sono parametri GET/SET persistenti nella configurazione corrente. Brand e policy
menu restano esclusi da NFC e dal catalogo di configurazione ordinaria.

### Proprietari DRV_STG4 — namespace `0206`

Da `rdm.c`, `rdm.h` e `rdm_settings` in `defs.c`. Non confondere l'insieme
SUPPORTED_PARAMETERS RDM con quello dei 19 valori pubblicati via NFC.

| PID | Significato riscontrato | Formato/range e note |
| --- | --- | --- |
| `8200` | PWM/Flicker | SET UINT8 indice; GET include current/count; enum da `pwm_options`, non dedurre Hz dall'indice |
| `8201` | Smooth | SET UINT8 0..3; GET current/count |
| `8202` | Simulazione alogena | SET UINT8 0..2; GET current/count; fuori dal catalogo NFC 19 PID |
| `8203` | Simulazione CYM | SET UINT8 0..1; GET current/count |
| `8301` | WDMX Bridge | GET/SET UINT8 booleano; fuori dal catalogo NFC 19 PID |
| `8400` | Inizializzazione interna | SET UINT8 0..7, privilegi/compatibilità controller specifici; azione Factory, non configurazione NFC ordinaria |
| `8500` | Display Auto OFF legacy | GET/SET UINT8 booleano; inversione esplicita di `display_mode`; alias funzionale, non stesso formato di E103 |
| `E000` | Abilitazione scena | GET/SET UINT16 0..1 |
| `E001..E007` | Livelli scena canali 1..7 | GET/SET UINT16 0..255; non UINT8 nonostante il range |
| `E100` | Algoritmo rampa | GET/SET UINT16, 0 legacy, 1 moderno |
| `E101` | Curva dimmer | GET/SET UINT16, 0 legacy, 1 quadratic, 2 linear |
| `E102` | Tipo rampa | GET/SET UINT16 0..5 nella copia corrente; precedenti cataloghi avevano massimo 4 |
| `E103` | Modalità OLED | GET/SET UINT16, 0 Auto OFF, 1 Always ON |
| `E104` | Tempo Auto OFF OLED | GET/SET UINT16, 10..3600 secondi |
| `E105` | Tempo salvataggio OLED | GET/SET UINT16, 0..240 minuti; 0 Never |

La tabella descrive i formati riscontrati; non autorizza a cambiare i PID del
prototipo ora. Le risposte descrittive possono avere adattamenti legacy DRS;
registrare esplicitamente varianti/alias prima di generare un catalogo ufficiale.
`E102` e PWM mostrano perché servono revisione modello ed enum verificati, oltre
al solo nome del parametro. Per i nuovi prodotti sotto 0206 non ridefinire
silenziosamente l'associazione tra un valore enum e il suo significato.

### Proprietari RSH7 e URC01 — namespace `0206`

| Progetto/PID | Significato | Formato e range riscontrati |
| --- | --- | --- |
| RSH7 `8001` | Splitter Mode | UINT8 GET/SET: 0 Transparent, 1 Full Proxy, 2 Transparent Plus; 3 riservato |
| RSH7 `8002` | DS RDM Enable | UINT8 GET/SET: 0 OFF, 1 ON |
| RSH7 `8003` | Frame Forward Mode | UINT8 GET/SET: 0 standard 512, 1 universal |
| URC01 responder | Nessun PID proprietario individuato nel dispatcher esaminato | Standard RDM; address/label sono RAM nel responder attuale |

Lo status message ID `8001` di RSH7 non è una seconda assegnazione del PID 8001.
I PID sintetici nei vettori SDCP di test e gli pseudoparametri UI del Manager URC
non vanno promossi a registro di prodotto. URC come controller interpreta anche
parametri altrui: conoscerli non equivale ad assegnarli al suo responder.

**Esito del confronto:** negli insiemi proprietari censiti non è emerso un numero
con significati incompatibili fra DRV e RSH7 sotto 0206. Non è una certificazione
su tutti i prodotti/rami. Funzioni apparentemente simili possono essere diverse:
`17FA:9108` spegne i relè, `0206:E104` il display; `17FA:9107` e `0206:E103`
hanno polarità e larghezza differenti. Sono coppie distinte legittime, non vanno
unificate solo perché il nome contiene Display o OFF Time.

### Namespace standard da censire separatamente

| Famiglia | PID riscontrati / funzione |
| --- | --- |
| Discovery | `0001..0003` |
| Proxy/stato RSH7 | `0010`, `0011`, `0015`, `0020`, `0030` |
| Identità/descrittori | `0050`, `0051`, `0060`, `0070`, `0080`, `0081`, `0082`, `00C0` secondo prodotto |
| Configurazione DMX | `00E0`, `00E1`, `00F0` |
| Servizio/contatori DRV | `0090`, `0400`, `0401`; non valori NFC ordinari |
| Rete ADSW100 | `0700`, `0701`, `0702`, `0703`, `0705`, `0706`, `0709`, `070A`, `070B` |
| Identificazione | `1000` |

Il codec SDCP URC conosce inoltre CURVE `0343`; ciò non significa che tutti i
responder lo supportino. Il registro dovrà riportare GET/SET/PDL standard e i
selettori, senza dedurre supporto dai soli `#define`. Questa tabella è un indice
del censimento, non riscrive la specifica ANSI e non dichiara conformità normativa.

## 4. Catalogo comune: dati sorgenti e artefatti

Proposta: **JSON come sorgente revisionata**, con schema di validazione e interi
espliciti; generatori deterministici producono un catalogo binario compatto per
microSD/app e le sole tabelle/constant necessarie ai firmware. L'app può usare lo
stesso binario; non deve avere un interprete C o una lista di modelli compilata.
Non servono due cataloghi curati manualmente.

Due livelli logici nello stesso processo di generazione:

1. Registro delle chiavi standard/proprietarie: nome semantico stabile, formati
   GET/SET/canonico NFC, tipo, unità, selector, privilegi e riferimenti sorgente.
2. Schemi modello: identità manufacturer/model, revisione, sottoinsieme supportato,
   restrizioni, default, enum/etichette del modello, eventuali capacità richieste,
   binding alle API del firmware. Una categoria prodotto non identifica lo schema.

Non tentare di estrarre automaticamente la semantica da `case`, commenti o
stringhe del codice. Prima importare e confrontare il censimento con dispatcher,
validatori ed EEPROM reali; poi scegliere la sorgente unica e generare/verificare
le tabelle. In questa fase nessun refactoring dei firmware è autorizzato.

Campi minimi di ogni definizione: namespace, manufacturer quando proprietario,
PID, nome, tipo e dimensione, byte order, min/max, unità/prefix, enum numerici e
label, accesso NFC/RDM distinto, persistente o azione, selettore e formato del
valore. Le azioni Factory/INIT e i segreti non sono abilitate dal semplice SET.
Gli editor di base sono numeri signed/unsigned 8/16/32, bool, ASCII limitata,
enum, IPv4 e IPv4/prefix; strutture non comprese restano opache/read-only.

Il catalogo dati non può estendere i privilegi del target. Anche dopo firma
valida, il target ricontrolla supporto, permessi, schema e valori nelle API comuni.
Vincoli contestuali complessi rimangono nel prodotto; niente linguaggio di script.

### Revisioni e fingerprint

- **Versione formato**: grammatica del catalogo e del messaggio SDCP.
- **Revisione schema modello**: UINT32 monotona per manufacturer/model; cambia
  con chiavi, formati, limiti, permessi, enum numerici o binding semantico.
- **Fingerprint schema**: SHA-256 del segmento binario canonico del contratto,
  generato prima della build e incorporato in flash nel target. Esclude traduzioni,
  testi presentativi, valori correnti, firmware version e contatori transazione.
- **Versione pacchetto catalogo**: release distribuita, può aggiungere modelli o
  correggere traduzioni senza cambiare il contratto dei modelli precedenti.
- **Revisione configurazione**: contatore persistente SYNC1; cambia solo per una
  modifica applicativa salvata o un'inizializzazione esplicita prevista.

Una correzione testuale non deve obbligare a riprogrammare i prodotti. L'hash del
file completo protegge anche testi e traduzioni, ma è distinto dal fingerprint
schema. Vietato rilasciare due contratti differenti con la stessa revisione
modello; verificare sempre sia revisione sia hash, non un range di versioni firmware.

## 5. Confronto delle tre architetture

Stime di **TLV + NDEF + payload + terminatore**, esclusi UID/CC, padding finale e
journal. Ipotesi: 15 valori ADSW con rete standard indicizzata, 19 valori DRV,
nessuna stringa device aggiuntiva, riferimento schema SHA-256, SYNC1. I numeri
piccoli sono calcolati dal formato proposto sotto; i cataloghi completi sono
stime, non immagini già generate/testate.

| Aspetto | Catalogo completo nel tag | Catalogo esterno aggiornabile | Ibrido: esterno + descrittori minimi nel tag |
| --- | --- | --- | --- |
| NFC ADSW100 | Circa 700..900 byte, secondo label/enum | Circa **233 byte** | Circa **285..358 byte**, assumendo 50..120 byte di fallback |
| NFC DRV 19 valori | Circa **783..851 byte** usando dimensioni dei descrittori esistenti e CHOICES fino a 64 byte | Circa **238 byte** | Circa **290..363 byte** con lo stesso fallback |
| Firmware prodotto | Serializza anche descrittori/enum e controlla budget | Codec piccolo, binding parametri, hash statico | Come esterno, più selezione e coerenza del fallback nel tag |
| Firmware URC01 | Parser/editor generico; nessun servizio catalogo necessario al nuovo modello | Loader SD, cache/indice, verifica firma, risolutore schema, editor generico | Tutto il percorso esterno più fusione di due fonti/policy di precedenza |
| Manutenzione | Catalogo segue ogni firmware prodotto; correzioni testi richiedono aggiornarlo | Registro/generatore e pacchetti centralizzati; distribuzione dati distinta dal firmware | Gestire anche concordanza e versioni dei descrittori minimi nel tag |
| Nuovo prodotto offline | Subito, se tipi supportati e catalogo entra nel tag | Solo se catalogo già installato; altrimenti lettura limitata | Solo i campi minimi autodescritti; resto richiede catalogo |
| Smartphone | NDEF + codec/editor | NDEF + codec/editor + catalogo locale/import/download | Stesse API NFC, due percorsi di risoluzione |
| Rischio principale | Capacità e durata pubblicazione, crescita enum | Catalogo assente/non corrispondente | Maggior complessità per un fallback parziale |

Non confondere l'ibrido con il semplice fallback in firmware: quest'ultimo non
occupa byte NFC e non autodescrive modelli nuovi. L'ibrido ha senso solo se la
configurabilità di alcuni campi di un prodotto sconosciuto **senza catalogo
precaricato** è un requisito forte. In tal caso definire una sezione minima
standard e un budget rigido prima di implementarlo; non aggiungere descrittori
caso per caso senza un contratto comune.
Le stime ibride includono i passaggi alle lunghezze TLV/NDEF estese: aggiungere
50 byte di payload non significa sempre aggiungere esattamente 50 byte sul tag.

### Budget verificato del riferimento e limiti fisici

Il vettore URC `docs/examples/sdcp/DRV_STG4_SYNC.t2t.bin` contiene SDCP 702 byte,
NDEF 738 e TLV completo 743; non contiene CHOICES. È una misura del vettore,
non del documento corrente completo. I descrittori occupano 536 byte, più header
sezione 4. Il buffer del servizio DRV è 880 byte; journal SDJ1 di 32 byte a offset
848 dall'inizio User, fuori dalla finestra NDEF pubblicizzata dal provisioning
DRV, pari a 816 byte. Questo layout è riutilizzabile, non obbligatorio nel wire.

Il settore 0 User del riferimento dispone di 888 byte fisici. Il limite reale è
quello del CC e delle protezioni, non la dimensione del buffer; non sommare
settore 1 o aumentare CC per far entrare il catalogo. Un documento da 900 byte
non entra in quel layout. I numeri vanno riconfermati sul chip ADSW montato.
Mappa e arbitraggio sono descritti nel
[datasheet NXP NT3H2111/2211](https://www.nxp.com/docs/en/data-sheet/NT3H2111_2211.pdf).

## 6. Formato consigliato: SDCP 2.0 compatto

È una **proposta wire**, non un ID di protocollo già distribuito. Conservare MIME
`application/vnd.studiodue.sdcp` (30 byte), un solo record NDEF MIME TNF=2,
MB=ME=1, CF=IL=0; leggere sia SR sia non-SR. La registrazione IANA del MIME non
è attestata da questa analisi. NDEF resta standard, il payload applicativo è SDCP.

Conservare header a 6 byte: `SD`, versione **0x20**, flag device=0, lunghezza totale
UINT16 big endian; CRC-32/ISO-HDLC finale di 4 byte. CRC riflesso con polinomio
EDB88320, init/xorout FFFFFFFF; controllo `123456789 -> CBF43926`. Il CRC rileva
errori, non autentica né autorizza la scrittura. Non memorizzare struct C raw.

Sezioni ordinate uniche: `id:u8, flags:u8, length:u16, data[]`; bit 0 CRITICAL.
Ignote critiche: rifiutare. Ignote non critiche: preservare byte per byte. Il
client non deve poter sopprimere una sezione sconosciuta quando riscrive il tag.

| ID proposto | Sezione | Contenuto |
| --- | --- | --- |
| `01` | IDENTITY, critica | Come oggi: manufacturer:u16, model:u16, category:u16, software:u32, UID RDM:6; totale 16 byte |
| `02` | VALUES, critica | Elenco ordinato di chiavi supportate e relativi valori, formato indicizzato sotto |
| `07` | SCHEMA_REF, critica | schema_revision:u32 + schema_fingerprint SHA-256:32; totale 36 byte |
| `80` | SYNC, critica | Corpo SYNC1 invariato: version:u8=1, kind:u8, revision:u32, request_id:u32, applied_id:u32; totale 14 byte |

IDENTITY e SCHEMA_REF sono immutabili nel client; provengono dal firmware. Brand
è presentazione, non sostituisce manufacturer/model/schema. Il supporto effettivo
è l'elenco VALUES: non occorre una seconda lista duplicata di PID. Il catalogo
fornisce i descrittori, il tag dice quali chiavi/istanze sono presenti ora.
Il target controlla l'intero elenco ammesso e la configurazione corrente prima
del commit. Niente risoluzione dello schema affidata al solo Software ID.

### Chiavi e parametri indicizzati

Record VALUES v2: `pid:u16, selector_length:u8, selector[], value_length:u8, value[]`.
L'ordine è PID, poi selettore lessicografico; la coppia PID/selettore è unica.
La lunghezza delimita anche valori sconosciuti. Un documento root appartiene a
un solo manufacturer: per PID standard il namespace è standard, per i proprietari
è quello di IDENTITY. Non introdurre documenti multim manufacturer/subdevice ora.

Il catalogo definisce selettore e valore normalizzato; **non sono sempre una
risposta GET raw**. L'adapter RDM può ricomporre il payload standard completo;
il decoder NFC non cambia la codifica RDM. Esempi da ratificare con gli adapter:

| Parametro | Chiave wire | Valore configurazione canonico NFC |
| --- | --- | --- |
| DMX Address | standard `00F0`, selettore vuoto | UINT16 BE |
| Porta Node ADSW | `17FA:9100`, selettore vuoto | UINT8 0..2 |
| DHCP interfaccia 1 | standard `0703`, selettore interface:u32=1 | bool UINT8 |
| IP statico e mask | standard `0706`, selettore interface:u32=1 | IPv4:4 ottetti + prefix:u8 |
| Default route | standard `070A`, selettore vuoto | ID interfaccia/route:u32 + gateway:4, semantica standard da validare per target |
| DNS 1 / DNS 2 | standard `070B`, selettore index:u8=0 / 1 su ADSW corrente | IPv4:4 ottetti |

Gli ID e indici sono quelli del binding reale del prodotto, non dedotti dalle
etichette "DNS 1"/"DNS 2". Nessuna confusione fra DHCP richiesto, IP statico
configurato e indirizzo corrente ottenuto da DHCP. `0705` è stato corrente,
non un SET di configurazione. `0709` APPLY è una procedura RDM, non un parametro
persistente da aggiungere a EEPROM: la transazione NFC valida e commette il
candidato completo attraverso le API comuni.

Per ADSW IP+mask diventano un campo NFC composto, conservando entrambi i valori
EEPROM; la conversione mask/prefix richiede maschera contigua. Il firmware legacy
ha un unico insieme IP/mask e un flag DHCP: il catalogo non deve fingere due
configurazioni persistenti indipendenti che non esistono. Definire e testare il
binding NFC sui valori CFG salvati e la relazione con staging/APPLY RDM prima
di dichiarare il comportamento equivalente. Non modificare adesso handler RDM.

Questo risolve il limite di SDCP 1.0 (un valore per PID) senza assegnare sei nuovi
PID proprietari solo per la rete. Si evita anche di riutilizzare numeri uguali
con significati diversi nello stesso namespace.

### Calcolo delle stime compatte

Parte fissa SDCP: header/CRC 10 + IDENTITY 20 + SCHEMA_REF 40 + SYNC 18 = **88 byte**.
VALUES aggiunge header 4 e, per ogni elemento, 4 + selettore + valore.

- ADSW: Address + nove PID 9100..9108 + DHCP + static IPv4/prefix + route + due DNS
  = 15 elementi; valori 35 byte complessivi, selettori 10, overhead record 60.
  SDCP = 88 + 4 + 35 + 10 + 60 = **197 byte**.
- DRV: 19 elementi, valori canonici attuali 34 byte, nessun selettore.
  SDCP = 88 + 4 + 34 + 76 = **202 byte**.

Con payload fino a 221 byte: header NDEF SR 3 + MIME 30 + TLV 2 + terminatore 1
= overhead 36, quindi 233/238 byte. Arrotondamento a scritture blocchi da 16:
240 byte in entrambi i casi, se il driver scrive blocchi interi. Oltre 255 byte
di payload l'overhead lungo è 41. Nome/seriale testuale, MAC aggiuntivo, descrittori
ibridi o nuovi parametri aumentano le dimensioni: nessuno è incluso nelle stime.

### Perché non limitarsi a SDCP 1.0

Togliere DESCRIPTORS e aggiungere solo un riferimento esterno è possibile, ma
non risolve le istanze multiple dello stesso PID né i controlli specifici URC.
Si potrebbe mantenere v1 e inventare chiavi proprietarie NFC, pagando però un
secondo dizionario e adapter inutili. Non avendo obbligo di compatibilità NFC
dei prototipi, v2 è più chiaro. MIME, NDEF, CRC, driver RF/I²C, journal e semantica
SYNC rimangono riutilizzabili; i parser riconoscono 0x20 e rifiutano versioni diverse.
La precedente proposta GENERIC_CONFIG v1 non è più quella raccomandata.

## 7. Catalogo su microSD, fallback e funzionamento offline

Layout proposto, ancora non creato:

- sorgenti JSON e generatori nel repository di riferimento del registro;
- pacchetti versionati in `/StudioDue/Catalogs/<release>/` su URC01;
- manifest firmato con lista file, dimensioni e SHA-256, contratto formato e key ID;
- indice `(manufacturer, model, schema_revision, fingerprint)` verso il blocco modello;
- pochi schemi completi essenziali in flash come fallback, generati dalla stessa sorgente.

Evitare un parser JSON generale su ogni accesso UI: leggere l'indice e solo il
blocco modello necessario, con limiti espliciti di dimensione, numero elementi,
lunghezza stringhe, selettori e profondità. Il formato binario del catalogo avrà
header/versione e record delimitati; fissarne la specifica dettagliata dopo la
scelta architetturale. La stima NFC non dipende dal suo layout interno.

Il fallback deve corrispondere esattamente allo schema, non essere una tabella
universale con range ipotizzati. Nuovo modello/schema non trovato: mostrare
«Catalogo richiesto non disponibile» e manufacturer/model/revisione/hash; offrire
import/aggiornamento, mantenendo la consultazione raw. Schema presente ma tipo o
campo non gestito: quel parametro resta non modificabile, preservato nella
REQUEST; gli altri sono editabili solo se tutte le feature obbligatorie sono note.
Non usare automaticamente il catalogo più recente con hash differente.

URC può ricevere il pacchetto via file su microSD/trasferimento compatibile con
lo storage esistente; il download Internet è una fase successiva. L'app conserva
il catalogo nello storage privato e consente import/download dello stesso pacchetto.
Nessuna rete necessaria alla configurazione con catalogo già verificato. Cataloghi
vecchi validi rimangono disponibili per gli apparecchi installati; aggiornare il
pacchetto non elimina gli schemi precedenti ancora necessari.

Durante l'editing fissare la versione del catalogo per tutta la sessione.
Aggiornamenti, estrazione SD o cambio identità non devono sostituire sotto mano
le regole usate per una REQUEST già preparata. La perdita della SD deve produrre
un errore gestito o consentire soltanto il modello già completamente in cache;
mai leggere dati parziali come se fossero un catalogo nuovo.

## 8. Integrità, autenticità e aggiornamenti

SHA-256/fingerprint e CRC non sono firme. Proposta concreta: manifest con firma
**Ed25519** e chiave pubblica di fiducia incorporata in URC01/app; chiave privata
solo nel processo di rilascio, mai nel tag o nei firmware distribuiti. Riferimento
per algoritmo e vettori: [RFC 8032](https://www.rfc-editor.org/rfc/rfc8032).
Libreria verificata e costo RAM/flash/tempo su URC da valutare; non implementare
un algoritmo crittografico scritto apposta per questo progetto.

Il generatore produce un manifest deterministico, ad esempio JSON canonico
[RFC 8785](https://www.rfc-editor.org/rfc/rfc8785), firmato sui **byte esatti**.
Il verificatore embedded non deve riserializzare JSON per verificare la firma:
verifica i byte ricevuti, poi esegue parsing stretto, rifiuta duplicati/ambiguità,
path traversal, overflow, file mancanti, dimensioni eccedenti e formati sconosciuti.
Il manifest vincola tutti i file del pacchetto, inclusi testi/indice. Hash del
contratto ricostruito dal blocco modello deve coincidere con quello nel tag.

Procedura proposta: import/download in area staging; verifica firma, hash e
struttura completa; sincronizza su disco; attiva come nuova generazione solo
quando tutti i controlli passano. Conserva la generazione precedente. Un rename
FAT isolato non garantisce atomicità a power-cut: al boot riesaminare manifest e
marker/generazioni e usare l'ultima copia interamente valida. Non cancellare il
catalogo attivo per fare spazio prima della verifica del nuovo.

Il download futuro usa HTTPS, ma la firma serve anche agli aggiornamenti offline
su SD. Key ID, rotazione e revoca devono essere previsti: aggiornamenti di fiducia
firmati da una chiave già fidata, niente chiave pubblica arbitraria inclusa nel
pacchetto e accettata automaticamente. Protezione da downgrade: distinguere
release di sicurezza accettata da schemi storici conservati. Un utente può aver
bisogno dello schema vecchio senza reinstallare un pacchetto vulnerabile.

Il contatore persistente client delle release accettate e la protezione delle
chiavi non risiedono nella EEPROM ADSW. Se sul prototipo URC il contatore vive
solo sulla SD rimovibile, non si può promettere anti-rollback contro sostituzione
volontaria della SD; progettare lo storage di fiducia prima di dichiararlo sicuro.
Una revoca appena emessa non è conoscibile offline prima di importarla: dichiarare
la release di fiducia installata, non inventare una garanzia di revoca in tempo reale.

La firma del catalogo non autentica un tag né autorizza un client sul dispositivo.
Non introdurre implicitamente autenticazione dei SET o sicurezza crittografica
NFC end-to-end: sono requisiti diversi, esclusi da questa proposta.

## 9. SYNC1 e commit: garanzie da conservare

Kind 0 SNAPSHOT, kind 1 REQUEST; revisione 0 non inizializzata. Da SNAPSHOT il
client conserva base revision e applied ID, usa request = next(applied) saltando
zero, modifica solo valori ammessi e ricalcola CRC. REQUEST pendente immutabile:
nessun nuovo editing scrivibile o retry cieco sul timeout. Rileggere il tag prima
del write per rilevare dati cambiati; un tag passivo non offre compare-and-swap
fra due client concorrenti, quindi un solo writer per sessione.

Write/read-back RF significa **TAG_STORED / PENDING**, non APPLIED. Il target,
a campo RF assente e dopo letture stabili, verifica identità/schema/base/ID e
valori, applica tramite le API comuni e salva/verifica EEPROM. Solo dopo il
commit pubblica SNAPSHOT con applied ID nuovo e revisione avanzata. Il client
conferma dopo nuova lettura: identità, tag della sessione, schema, applied ID,
revisione successiva e valori richiesti coerenti. Una normalizzazione divergente
richiede mostrare i valori reali, non un falso esito identico alla richiesta.

Replay di ID commesso con medesimo CRC: soltanto ripubblicare ACK. Stesso ID con
CRC diverso: conflitto; base obsoleta: rifiuto. Anche cambiamenti locali salvati
ai parametri NFC devono avanzare/invalidate la base di revisione. Nessuna modifica
per sole scansioni o ACK. Lo stato dettagliato di rifiuto non è già un campo SYNC1:
se necessario via NFC, definirlo come estensione esplicita con relativo budget.

A dispositivo spento la REQUEST può essere conservata nel tag; il MCU la applica
solo all'accensione. Con alimentazione presente resta il workflow DRV: allontanare
il lettore, permettere il commit senza campo RF, poi riavvicinarlo per l'ACK.
Non si introducono SRAM pass-through o protocolli esclusivi URC01.

## 10. EEPROM ADSW100 installati: compatibilità obbligatoria

La nuova libertà sui prototipi NFC non autorizza a cambiare la mappa o i dati
commerciali ADSW. `0x00..0x27` restano invariati, versione struttura 3; brand,
policy menu, ruolo porta e ogni parametro legacy restano protetti. Metadati NFC
autorizzati dall'utente solo a `0x28..0x37`; spazio libero nei percorsi esaminati,
non necessariamente cancellato sulle unità installate. Nessun doppio slot,
CRC di configurazione o inizializzazione automatica per errori I²C/versioni ignote.

| Offset proposto | Byte | Metadato dinamico |
| --- | ---: | --- |
| `28..29` | 2 | Discriminante `NF` |
| `2A` | 1 | Versione metadati, indipendente da SDCP/catalogo |
| `2B` | 1 | Stato di commit/recupero; codici da validare con i test |
| `2C..2F` | 4 | Revisione configurazione SYNC |
| `30..33` | 4 | Ultima richiesta associata |
| `34..37` | 4 | CRC32 della richiesta, per deduplicazione; non CRC configurazione |

La mappa metadati resta proposta, non implementata. Sono 56 byte applicativi
inclusi legacy/riservati; `0x38..0x7F` rimangono 72 byte non assegnati. Revisione
schema, fingerprint, Model ID, catalogo e chiavi crittografiche statiche appartengono
alla flash, non a questi 16 byte. Non cambiano per un salvataggio utente.

**Limite confermato:** DRV ha record EEPROM ridondanti/verificabili; ADSW salva
in place. I 16 byte non rendono atomico un SET multiparametro. Occorre predisporre
metadati di intenzione verificati prima della configurazione, verificare il
salvataggio e sigillare infine il commit; gestire anche i salvataggi locali,
non soltanto quelli NFC. Un reset in stato preparato richiede confronto completo
con richiesta/tag/valori salvati. Se coincidono, completare i soli metadati;
se non dimostrabile, bloccare l'import e segnalare recupero necessario. Non
promettere rollback completo o disponibilità automatica dopo ogni power-cut.
Il journal tag aiuta la pubblicazione, non rende atomica la EEPROM applicativa.

Metadati indefiniti o corrotti non autorizzano ad applicare una vecchia REQUEST.
Al primo avvio NFC nuovo, EEPROM legacy valida è la sorgente autorevole; tag
JSON/SDCP prototipo si sostituisce solo con provisioning esplicito e non diventa
una configurazione da importare. Tag estranei conservati. Le implementazioni
NFC prototipo possono essere dismesse, ma il nuovo firmware deve rifiutarne
l'applicazione automatica. Il reset pannello attuale che cancella/riscrive JSON
andrà adattato durante l'implementazione autorizzata, non ora.

## 11. Smartphone e percorso di implementazione da approvare

Tutte e tre le soluzioni usano le stesse API NDEF del telefono: il catalogo è
una scelta applicativa, non una nuova tecnologia RF. Riferimenti ufficiali:
[Apple NFCNDEFTag](https://developer.apple.com/documentation/corenfc/nfcndeftag)
e [Android Ndef](https://developer.android.com/reference/android/nfc/tech/Ndef).
Controllare scrivibilità/capacità e rileggere dopo write; usare una sessione app
appropriata. NDEF MIME non garantisce l'apertura automatica dell'app in background.
Non formattare il tag né cambiare CC/lock/password; verificare su hardware che
le API NDEF dei telefoni preservino il journal esterno all'area dichiarata.

Dopo scelta dell'architettura:

- [ ] Ratificare il registro per namespace e separare assegnazioni attuali, riservate, obsolete e proposte; cercare conflitti anche fuori dai quattro progetti.
- [ ] Fissare SDCP 2.0, selettori, normalizzazioni rete e limiti; generare vettori indipendenti e dimensioni byte per byte.
- [ ] Definire sorgente JSON, contratto binario catalogo e generatore; confrontare automaticamente il risultato con gli handler e i validatori reali.
- [ ] Prototipare loader/verifica catalogo URC con budget RAM/flash e tempi cooperativi, riutilizzando storage esistente.
- [ ] Verificare modelli nuovi solo tramite pacchetto dati, anche con Internet assente; tipo sconosciuto read-only, catalogo mancante e SD estratta.
- [ ] Provare pacchetto alterato, firma/hash errati, rollback, chiave ignota/revocata, import incompleto e power-cut durante attivazione.
- [ ] Validare ogni interruzione del commit metadati/configurazione ADSW, replay, conflitti, I²C failure, reboot prima/dopo ACK e salvataggi da pannello/RDM.
- [ ] Portare il servizio NFC senza cambiare DMX/RDM/Art-Net/sACN, relè o hold RDM Node di 30 secondi; build ARM e regressioni disponibili.
- [ ] Collaudare NDEF, campo RF, rimozione anticipata e conferma con URC01, iOS e Android; acceso/spento.
- [ ] Aggiornare AGENTS.md, EEPROM_MIGRATION.md, AUDIT.md e ROADMAP_V2.md con la soluzione approvata e i risultati effettivi.

Questa fase ha eseguito censimento statico, confronto dei formati e calcolo delle
stime; non build firmware o nuove prove RF. Restano da approvare la scelta esterno
vs ibrido e il formato v2; da misurare il costo del verificatore catalogo e da
validare il commit persistente ADSW. Nessuna necessità di conservare le whitelist
o il payload NFC dei prototipi, nessuna modifica automatica ai loro sorgenti.
