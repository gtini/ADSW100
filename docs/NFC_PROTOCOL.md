# ADSW100 NFC SDCP2 e collaudo URC01

Il dispositivo usa il contratto SDCP2 di URC01 / DRV_STG4 con chiave
`17FA:0060`, revisione manuale 1. Il [JSON manuale](../protocol/nfc/catalogs/ADSW100.json)
rispetta l'ordine richiesto dal parser streaming di URC01. La [specifica wire](../protocol/nfc/README.md)
descrive il formato. Il tag porta soltanto identità, valori e stato SYNC1.
Le build CubeIDE usano direttamente la tabella C. L'immagine ST2FW v3 può
trasportare il JSON opzionale con CRC del catalogo separato da quello del BIN.

| PID / istanza | Valore pubblicato, RW | Default |
| --- | --- | --- |
| Standard 00F0 | indirizzo DMX u16, 1–512 | 1 |
| Standard 0703, interfaccia 1 u32 | DHCP bool | ON |
| Standard 0706, interfaccia 1 u32 | IPv4 statico 4 byte + prefisso 0–32 | 0.0.0.0/0 |
| Standard 070A | ID route u32 riservato a zero + gateway IPv4 | 0.0.0.0 |
| Standard 070B, indici u8 0 e 1 | DNS primario e secondario IPv4 | 0.0.0.0 |
| 17FA:9100 | ruolo porta u8 IN, Art-Net Node, sACN Node | IN |
| 17FA:9101 | sorgente u8 DMX, Art-Net, sACN, Auto | sACN |
| 17FA:9102 | universo Art-Net u16, 0–32767 | 0 |
| 17FA:9103 | universo sACN u16, 1–63999 | 1 |
| 17FA:9104 | trasporto u8 multicast/unicast | multicast |
| 17FA:9105 | priorità sACN u8, 0–200 | 0 |
| 17FA:9106 | ordine Auto u8, sei permutazioni | DMX → Art-Net → sACN |
| 17FA:9107 | Auto OFF display bool; OFF equivale a backlight sempre accesa | ON |
| 17FA:9108 | tempo spegnimento relè u8, 0–30 secondi | 30 s |

Gli indirizzi wire usano gli ottetti IPv4. Il prefisso si traduce in netmask
contigua; i campi persistenti conservano la rappresentazione legacy raw lwIP.
Il dispositivo usa CFG per validazione e applicazione. Il byte backlight
legacy resta intatto quando il booleano non cambia. Brand, codice menu,
operazioni di sblocco ed EUI completo restano fuori dai valori NFC. Il UID
RDM è parte dell'identità del documento.

Il parser C URC01 ha letto il catalogo, confrontato tutte le 15 istanze,
creato una REQUEST uguale alla fixture e riconosciuto l'ACK ADSW100 su host.
Il client reale cerca il catalogo nel pacchetto v3 su SD. Fino al primo
ST2FW ADSW100, il JSON è pronto per Builder e debug host. Sul dispositivo
URC01 di serie è disponibile la lettura RAW e non va dato per disponibile
l'editor a catalogo. I sorgenti ADSW100 attuali non includono SDI1/SDC1 né
il layout di update applicativo richiesto per un pacchetto programmabile.

Init con E8 può anche sostituire il frammento da banco verificato con ST-LINK.
L'eccezione richiede l'immagine User esatta, indirizzo 02 e CC E110EF00. Se
rimane il consenso E8 della prova precedente, la nuova build riprende la
pubblicazione lasciando il dispositivo su Home e senza campo RF. Un'immagine
diversa resta soggetta al riconoscimento ordinario.

## Banco

1. Programmare manualmente il file [Release ADSW100.hex](/private/tmp/adsw100-nfc-init-repair/Release/ADSW100.hex)
   via USB DFU ROM con **DMX scollegato prima di entrare in DFU**. Ricollegarlo
   dopo l'avvio. Annotare brand, indirizzi e configurazione prima dell'aggiornamento.
2. Aprire CDC. Usare `boot?`, `nfc?`, `nfc_i2c?`, `i2c?` ed `ee?`.
   NFC è normalmente ON. Verificare indirizzo confermato, capacità e stato.
   La build diagnostica aggiunge a `nfc?` il campo `tag` dell'ultima scansione
   stabile. `OWNED_UNSUPPORTED` indica un documento riconosciuto come proprio
   che il parser corrente non accetta. `UNRECOGNIZED` include contenuto
   estraneo o danneggiato. Su tag già sincronizzato, controllare i valori
   preservando la EEPROM.
3. Su un'unità di prova autorizzata, usare **Init Memory** dal pannello per
   provare la sostituzione di un documento sperimentale proprio. Questa è
   un'operazione esplicita sui parametri applicativi. Allontanare il lettore
   RF, lasciare il dispositivo sulla schermata Home e attendere `SYNCED`
   prima di salvare dal pannello o via RDM. Un salvataggio durante E8 revoca
   l'intento e passa a C3. Confrontare CC valido, UID e protezioni prima e dopo.
4. Con URC01 leggere lo SNAPSHOT e verificare identità e schema. Per provare
   l'editor a catalogo serve il futuro pacchetto ADSW100. Per il controllo
   su host usare `python3 -B tests/test_nfc.py "/percorso/del/checkout/URC01"`.
   Con un client di prova SDCP2 che conosce il contratto, cambiare DMX Address
   e poi un gruppo con OFF Time, backlight e IPv4 statico/prefisso/DNS.
5. Dopo la scrittura RF rimuovere il lettore. Verificare il cambio dalla
   REQUEST allo SNAPSHOT con `applied=request`, revisione incrementata,
   valori corretti sul pannello e persistenza dopo riaccensione. Re-inviare
   la stessa richiesta per controllare che non venga applicata due volte.
6. Cambiare un parametro dal pannello o da RDM e riproporre la REQUEST
   precedente. Verificare il rifiuto senza perdita della modifica locale.
   Provare anche un tag protetto, un documento estraneo e un campo RF che
   ricompare a scrittura in corso. Su un tag parziale dopo power-cut,
   conservare il dump e acquisire `nfc?` prima di qualsiasi manutenzione.

Il collaudo hardware deve ancora verificare interazione RF/I²C, permanenza
dei lock, configurazione di ciascuna variante NTAG, durata dei commit e
ripetizione dei test con URC01. Una coda FF nel blocco misto 38h richiede
intervento sul tag, perché il firmware non scrive quel blocco. Una
pubblicazione interrotta da perdita di alimentazione può lasciare NDEF
invalido; il target protegge l'EEPROM e non riscrive un documento
di cui non può stabilire la provenienza.
