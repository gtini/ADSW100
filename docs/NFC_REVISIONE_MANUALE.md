# NFC SDCP2 — revisione manuale,17novembre2026

Decisione utente successiva alle istruzioni storiche sulla fondazione congelata:
rimosso il digest schema. Sezione07=sola revisione BE32, esattamente4byte.
Compatibilita': Manufacturer17FA, Model0060, Schema Revision1. Modifiche
incompatibili del contratto richiedono incremento manuale; sole descrizioni o
versione firmware diversa senza modifiche NFC non lo richiedono.

Rimosso nfc_schema_hash da nfc_catalog.h, che resta un contratto manuale delle
istanze usate dal servizio. Nessuna costante ROM aggiuntiva o generazione C
in compilazione; rimosso anche il controllo automatico header-vs-JSON dal test.
CRC32 messaggi, selettori, valori, base e replay SYNC1 restano verificati.
Una REQUEST con revisione schema diversa non viene applicata. Il vecchio formato
sperimentale con sezione36byte viene rifiutato, senza conversioni/erase automatici.

Nessuna modifica EEPROM, CFG, I2C, boot, relè, DMX, RDM o configurazioni Videlio.
Conservazione e validazione dei dati persistenti precedenti restano quelle del
servizio esistente; nessuna inizializzazione causata dall'aggiornamento firmware.

Verifiche su copia temporanea: test_nfc.py ASan/UBSan PASS,21 REQUEST/ACK,
170 errori commit,68 tagli REQUEST e24 tagli Init; buildARM150unita' PASS senza
warning. Nessuna programmazione o prova fisica RF/reset/power-cut eseguita.
