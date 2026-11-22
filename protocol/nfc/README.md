# Studio Due NFC — SDCP 2.0 sperimentale, revisione manuale

Contratto aggiornato il17novembre2026: nessun hash/fingerprint di schema, firma,
manifest o chiave. Nessuna compatibilità con i formati sperimentali precedenti.

Compatibilità catalogo: Manufacturer ID + Model ID + Schema Revision uint32 nonzero.
L'autore incrementa manualmente la revisione per modifiche incompatibili ai PID,
selettori, tipi o significato/contratto dei valori. Etichette/descrizioni o un
nuovo firmware senza variazioni NFC mantengono la revisione.

## Wire

Big endian. Header6byte: `53 44 20 00 total_length:u16`; CRC32 finale4byte.
Lunghezza10..4096, massimo16sezioni ordinate/uniche. Sezione:
`id:u8 flags:u8 length:u16 data[]`. Flag1=CRITICAL, altri bit vietati.
Le quattro sezioni seguenti sono obbligatorie e critiche:

| ID | Corpo |
|---|---|
| 01 | manufacturer:u16 model:u16 category:u16 software:u32 UID_RDM:6 —16byte |
| 02 | VALUES: pid:u16 selector_length:u8 selector[] value_length:u8 value[] |
| 07 | schema_revision:u32 —esattamente4byte |
| 80 | SYNC1: version:u8=1 kind:u8 revision:u32 request_id:u32 applied_id:u32 —14byte |

VALUES:1..64istanze ordinate per PID e selettore lessicografico, nessun duplicato.
Selettore0/1/4byte, valore0..64byte. PID standard0001..7FFF; proprietari8000..FFDF
nel namespace Manufacturer dell'identità. Nessun PIDzero o FFE0..FFFF.
UID con stesso Manufacturer, Device ID non broadcast. Software ID non seleziona
il catalogo. La vecchia sezione07da36byte è rifiutata, non migrata.

SNAPSHOT kind0:request=applied. REQUEST kind1:request=next(applied), wrapFFFFFFFF→1.
Revisione transazione nonzero, base esatta. CRC non è un numero di transazione.
Target: verificare identità, revisione schema corrente, base/replay, valori e
permessi prima di salvare; pubblicare ACK soltanto dopo commit verificato.
Una REQUEST di schema diverso non si applica. Non convertirla automaticamente.
Sezioni ignote critiche rifiutate; noncritiche preservate dal client nelle REQUEST.
RAW può modificare solo i byte dei valori, con consenso, senza cambiarne lunghezze,
chiavi o struttura. Un catalogo assente non disabilita automaticamente RAW.

NDEF Type2 standard: un solo MIME `application/vnd.studiodue.sdcp`, TNF2,
MB=ME=1, CF=IL=0. SR e nonSR ammessi. TLV03 corto/lungo, FE, poi padding zero
entro la capacità CC effettiva. Mai cambiare CC/lock per far entrare un messaggio.
CRC-32/ISO-HDLC riflesso EDB88320, init/xoroutFFFFFFFF, MSB-first sul wire NFC;
check123456789=CBF43926. Non riutilizzare checksum RDM.

## Sorgenti e strumenti

`sdcp.py` e `test_sdcp.py` sono codec/validatori host facoltativi, non un passaggio
di compilazione. `registry.json` e gli overlay `catalogs/` sono fixture della
fondazione importata da ADSW100. I JSON autonomi in `json/` sono modificabili
manualmente e possono essere selezionati nel Builder ST2FW. Nessun registro
parallelo firmware generato. Il dispositivo valida i valori tramite le proprie API.

`python3 -B protocol/nfc/test_sdcp.py` verifica wire, CRC, NDEF, namespace,
revisione manuale e transazioni. `vectors/vectors.json` contiene i vettori aggiornati,
non dispositivi reali. SDCP1/profili e ST2 legacy non definiscono questo contratto.

Distribuzione ordinaria: JSON incorporato nel pacchetto ST2FW v3. Vedere
`../../docs/ST2FW_V3_INTEGRAZIONE.md`. Nessun .sdcat o JSON separato richiesto
sulla microSD. I decoder non interpretano JSON come codice firmware.
