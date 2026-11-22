# ADSW100 SDCP2

Il catalogo [ADSW100.json](catalogs/ADSW100.json) è la sorgente manuale
per URC01 e per il futuro pacchetto ST2FW v3. La chiave di compatibilità è
Manufacturer `17FA`, Model `0060`, Schema Revision `1`. Il formato sperimentale
precedente è riconoscibile per la migrazione esplicita, ma è respinto come richiesta.
La revisione resta 1 perché il contratto corrente delle 15 istanze è quello già
assegnato ad ADSW100 dal client URC01.

La modifica di PID, selettori, tipo, valori ammessi, accesso o semantica che
rompe la compatibilità richiede un incremento manuale della revisione, nel JSON
e in `Core/Inc/nfc_catalog.h`. Etichette e versione firmware sono indipendenti.
Conservare l'ordine dei campi del JSON di esempio. Il parser URC01 legge in
streaming e una riserializzazione con chiavi ordinate alfabeticamente fallisce.
L'unità JSON 1 indica secondi. Il prefisso IPv4 è parte del valore di 0706.

| Sezione | Contenuto, interi big endian |
| --- | --- |
| 01, critica | 16 byte. Manufacturer, Model, Category, Software ID e UID RDM |
| 02, critica | PID u16, lunghezza selettore u8, selettore, lunghezza valore u8, valore |
| 07, critica | Revisione schema u32, esattamente 4 byte |
| 80, critica | SYNC1, 14 byte. Versione 1, tipo 0 snapshot/ACK o 1 request, revisione configurazione u32, request u32, applied u32 |

Ogni sezione ha ID u8, flag u8 e lunghezza u16. ID e chiavi VALUES sono
ordinati e univoci. Il documento inizia con `53 44 20 00` e lunghezza u16
incluso CRC32 ISO-HDLC finale big endian. NDEF usa un unico record MIME
`application/vnd.studiodue.sdcp` entro TLV 03 con terminatore FE. Il
documento base è 165 byte SDCP e 201 byte NDEF/TLV (208 allineati I²C).
Le sezioni sconosciute non critiche sono ricopiate nell'ACK quando entrano
nell'area scrivibile. Le REQUEST riportano l'intera VALUES, con sostituzione
dei soli valori scelti dal client.

`python3 -B tests/test_nfc.py` esegue il firmware reale con HAL simulato,
compresi i vettori copiati dal checkout URC01 e un caso sperimentale respinto.
Passando il percorso del checkout URC01 come argomento, lo stesso test compila
anche il decoder, il generatore REQUEST e il parser streaming URC01 e verifica
l'ACK prodotto da ADSW100. Questi strumenti sono facoltativi per il collaudo host.
CubeIDE compila direttamente la tabella wire C.

Per i PID, la procedura operativa e il limite di distribuzione del JSON si veda
[il collaudo NFC](../../docs/NFC_PROTOCOL.md).
