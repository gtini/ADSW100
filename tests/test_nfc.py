#!/usr/bin/env python3
"""NFC firmware reale con HAL NTAG simulata; ASan/UBSan, nessuna prova RF fisica."""
from pathlib import Path
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib
import test_stabilization as existing

ROOT = Path(__file__).resolve().parents[1]
MOCK = r'''
#define I2C1_EV_IRQn 11
#define I2C1_ER_IRQn 12
#define HAL_I2C_ERROR_AF 4u
static void HAL_NVIC_DisableIRQ(int irq) {(void)irq;}
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *h);
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *h);
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *h);
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h);
static uint8_t tag[1024], it_prefix[4], *it_buffer;
static unsigned it_length,it_reg,it_operation, tag_reg, session_reg, tag_program_until;
static unsigned tag_writes, bus_resets;
static bool it_stall, rf_field, panel_active, rdm_busy, rdm_save_pending, nack_once;
static unsigned ntag_address=2;
static uint8_t mock_session_mode;
static bool mock_hal_ready, mock_irq_enabled;

static unsigned bad_address;
static unsigned HAL_I2C_GetState(I2C_HandleTypeDef *h) {return h->State;}
static unsigned __get_BASEPRI(void) {return 0;}
static struct {unsigned IDR;} gpio_i2c={.IDR=192};
#define GPIOB (&gpio_i2c)
static void HAL_I2C_EV_IRQHandler(I2C_HandleTypeDef *h) {(void)h;}
static void HAL_I2C_ER_IRQHandler(I2C_HandleTypeDef *h) {(void)h;}
bool UI_ConfigActive(void) {return panel_active;}
static HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *h) {(void)h;return HAL_OK;}
static HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *h) {(void)h;it_operation=0;++bus_resets;return HAL_OK;}
static HAL_StatusTypeDef HAL_I2C_Master_Transmit_IT(I2C_HandleTypeDef *h,unsigned addr,uint8_t *p,unsigned n) {
 (void)h;bad_address=addr!=ntag_address*2;h->ErrorCode=0;h->XferCount=n-1;assert(!it_operation);assert(n<=4);memcpy(it_prefix,p,n);it_length=n;it_operation=1;return HAL_OK;
}
static HAL_StatusTypeDef HAL_I2C_Master_Receive_IT(I2C_HandleTypeDef *h,unsigned addr,uint8_t *p,unsigned n) {
 (void)h;assert(addr==ntag_address*2 && !it_operation);it_buffer=p;it_length=n;it_operation=2;return HAL_OK;
}
static HAL_StatusTypeDef HAL_I2C_Mem_Read_IT(I2C_HandleTypeDef *h,unsigned addr,unsigned reg,unsigned sz,uint8_t *p,unsigned n) {
 (void)h;(void)addr;(void)reg;(void)sz;(void)p;(void)n;assert(0);return HAL_ERROR;
}
static HAL_StatusTypeDef HAL_I2C_Mem_Write_IT(I2C_HandleTypeDef *h,unsigned addr,unsigned reg,unsigned sz,uint8_t *p,unsigned n) {
 (void)h;(void)sz;assert(addr==ntag_address*2 && n==16 && reg<=55 && !it_operation);
 assert(!rf_field);it_reg=reg;it_buffer=p;it_length=n;it_operation=3;return HAL_OK;
}
static bool RDM_Enabled(void) {return true;}
static bool RDM_SavePending(void) {return rdm_save_pending;}
static bool DMX512_BusBusy(void) {return rdm_busy;}
'''
HARNESS = r'''
static void Step(void) {
 ++tick;
 if(bad_address && it_operation){bad_address=0;it_operation=0;hi2c1.ErrorCode=HAL_I2C_ERROR_AF;HAL_I2C_ErrorCallback(&hi2c1);}
 if(nack_once && it_operation){nack_once=false;it_operation=0;hi2c1.ErrorCode=HAL_I2C_ERROR_AF;HAL_I2C_ErrorCallback(&hi2c1);}
 if(it_operation && !it_stall) {
  unsigned op=it_operation;it_operation=0;
  if(op==1) {
   assert((int32_t)(tick-tag_program_until)>=0);
   tag_reg=it_prefix[0];session_reg=it_length>=2?it_prefix[1]:0;
   HAL_I2C_MasterTxCpltCallback(&hi2c1);
  } else if(op==2) {
   if(tag_reg==0xFE) {assert(it_length==1);*it_buffer=session_reg==6?(rf_field?1:0):mock_session_mode;}
   else if(tag_reg==0x38) memcpy(it_buffer,tag+0x380,it_length);
   else if(tag_reg>=0x39) memset(it_buffer,0,it_length);
   else {assert(tag_reg*16+it_length<=sizeof tag);memcpy(it_buffer,tag+tag_reg*16,it_length);}
   HAL_I2C_MasterRxCpltCallback(&hi2c1);
  } else {
   if(it_reg==0){assert(it_buffer[0]==ntag_address*2);memcpy(tag+1,it_buffer+1,it_length-1);}
   else memcpy(tag+it_reg*16,it_buffer,it_length);
   ++tag_writes;tag_program_until=tick+4;
   HAL_I2C_MemTxCpltCallback(&hi2c1);
  }
 }
 NFC_Service();
}
static void Run(unsigned ms) {for(unsigned i=0;i<ms;i++)Step();}
static void Reset(void) {
 fail_at=-1;fail_read=fail_write=false;io_count=0;it_operation=0;it_stall=false;rf_field=false;panel_active=false;rdm_busy=false;rdm_save_pending=false;nack_once=false;
 ee_nfc_bus_reserved=false;nfc_transaction=false;nfc_local_live=false;
 tag_program_until=0;tick=10;NFC_SetEnabled(true);NFC_Init();
}
static void Setup(void) {
 ntag_address=2;bad_address=0;mock_session_mode=0;hi2c1.Instance=&i2c_regs;
 nfc_cut_bytes=-1;fail_at=-1;fail_read=fail_write=false;ee_nfc_bus_reserved=false;nfc_transaction=false;nfc_local_live=false;it_operation=0;
 memset(memory,255,sizeof memory);writes=0;
 assert(EE_InitMemory()==HAL_OK);assert(EE_LoadConfig()==HAL_OK);
 uint8_t eui[]={0xFC,0x0F,0xE7,0x16,0x45,0x70};memcpy(memory+0xFA,eui,6);
 memset(tag,0,sizeof tag);uint8_t header[]={4,0x39,0x28,0x62,0xBA,0x12,0x90,0,0x44,0,0,0,0xE1,0x10,0xEF,0};memcpy(tag,header,16);tag[0x38F]=255;
 Reset();
}
static unsigned Load(const char *path,uint8_t *out) {
 memset(out,0,896);FILE *f=fopen(path,"rb");assert(f);unsigned n=fread(out,1,896,f);assert(!ferror(f));fclose(f);return n;
}
static void OwnIdentity(const uint8_t *wrapped) {
 nfc_capacity=816; // Capacita dichiarata per i test diretti del codec.
 unsigned at=wrapped[1]==255?4:2;unsigned h=wrapped[at]==0xD2?3:6;
 memcpy(nfc_identity,wrapped+at+h+30+10,16);
}
static void Meta(unsigned revision,unsigned request,unsigned crc,unsigned state) {
 ee_nfc_meta_t m={.revision=revision,.request=request,.crc=crc,.state=state};assert(EE_NfcStore(&m)==HAL_OK);
}
int main(int argc,char **argv) {
 assert(argc==8);
 mock_hal_ready=false;TestMsp0();TestMsp1();assert(!mock_irq_enabled);
 mock_hal_ready=true;TestInit2();assert(mock_irq_enabled);
 puts("STARTUP IRQ: IRQ mascherati durante MSP/HAL, abilitati dopo init completo");
 Setup();
 uint8_t fixture[896]={0};unsigned n=Load(argv[1],fixture);assert(n==201);OwnIdentity(fixture);nfc_capacity=816;
 uint32_t saved[CFG_PARAMETER_COUNT];assert(NfcSaved(saved));ee_nfc_meta_t meta={.revision=1,.state=EE_NFC_COMMITTED};
 assert(NfcBuild(saved,&meta));assert(!memcmp(nfc_image,fixture,n));
 puts("SNAPSHOT: byte-identico al vettore ufficiale SDCP2/NDEF");
 FILE *index=fopen(argv[2],"r");assert(index);char file[1024];int expected;unsigned cases=0;
 while(fscanf(index,"%1023s %d",file,&expected)==2) {
  memset(fixture,0,sizeof fixture);Load(file,fixture);nfc_document_t document;
  bool valid=NfcParse(fixture,888,&document);assert(valid==(bool)expected);++cases;
 }
 fclose(index);assert(cases==19);puts("CODEC: tutti i vettori ADSW ufficiali positivi/negativi");
 Setup();uint8_t legacy[40];memcpy(legacy,memory,40);Run(12000);
 assert(!strcmp(nfc_status,"SYNCED"));assert(!memcmp(legacy,memory,40));assert(tag_writes==14);
 nfc_document_t doc;assert(NfcParse(tag+16,888,&doc));assert(doc.kind==0 && doc.revision==1);
 unsigned tw=tag_writes;Run(6000);assert(tag_writes==tw);puts("SERVICE: boot blank, snapshot, verifica tag e scansioni senza riscritture");
 // URC01 esige coda zero. Solo il filler FF nei blocchi User completi viene azzerato.
 Setup();tag[16+500]=0xFF;Run(16000);
 assert(!strcmp(nfc_status,"SYNCED") && tag[16+500]==0 && !tag[16+879]);
 Setup();tag[16+883]=0xFF;tw=tag_writes;Run(12000);
 assert(!strcmp(nfc_status,"CAPACITY_ERROR") && tag_writes==tw && tag[16+883]==0xFF);
 // Configurazioni commerciali e luminosità legacy non normalizzate dal publisher.
 for(unsigned b=0;b<3;b++) {
  Setup();dmx_address=420;dmx_mode=3;artnet_universe=32767;sacn_universe=63999;
  sacn_min_prio=200;off_timeout_s=17;lcd_backlight=150;use_dhcp=0;
  ip_address=0x1401A8C0;net_mask=0x00FFFFFF;gateway=0x0101A8C0;dns1=0x08080808;dns2=0x01010101;
  assert(EE_SaveConfig()==HAL_OK);assert(EE_SaveIdentity(b,b==1)==HAL_OK);assert(EE_LoadConfig()==HAL_OK);
  memcpy(legacy,memory,40);Run(12000);assert(!memcmp(legacy,memory,40));
  assert(NfcParse(tag+16,888,&doc) && doc.values[CFG_DMX_ADDRESS]==420 && doc.values[CFG_DISPLAY_AUTO_OFF]==0);
  assert(brand==b && menu_code_required==(b==1) && lcd_backlight==150);
 }
 puts("LEGACY: tre brand, Videlio/code1 e tutti i byte0..27 preservati, inclusa luminosita150");
 // H7: prefisso1byte già precaricato (XferCount0), STOP completato, main occupato80ms.
 Setup();tick+=10;NFC_Service();assert(it_operation==1);Step();
 assert(nfc_bus_phase==2 && hi2c1.XferCount==0);
 tick+=80;NFC_Service();assert(nfc_bus_phase==3 && it_operation==2);
 assert(nfc_diagnostic.timeouts==0 && s_nfc_addr==2);Run(12000);
 assert(nfc_confirmed_addr==2 && !strcmp(nfc_status,"SYNCED"));
 puts("H7 STOP: pausa main80ms non è timeout RX; scadenza30ms riparte al comando RX");
 // Un timeout vero non cambia l'indirizzo già verificato, né lo conferma da solo.
 Setup();tick+=10;NFC_Service();it_stall=true;Run(100);
 assert(nfc_diagnostic.timeouts>0 && s_nfc_addr==2 && nfc_confirmed_addr==0);
 char diag[256];NFC_FormatDiagnostics(diag,sizeof diag);
 assert(strstr(diag,"addr=-- probe=02 hal=04"));it_stall=false;Run(12000);
 assert(nfc_confirmed_addr==2);it_stall=true;Run(2000);
 assert(nfc_confirmed_addr==2 && s_nfc_addr==2);it_stall=false;Run(12000);
 NFC_I2C_EventIRQ();NFC_I2C_ErrorIRQ();assert(nfc_ev_irqs==1 && nfc_er_irqs==1);
 NFC_FormatBusDiagnostics(diag,sizeof diag);assert(strstr(diag,"last_hal=04") && strstr(diag,"liveirq=1/1"));
 puts("ADDRESS: tentato/confermato distinti, timeout senza rotazione, diagnostica e bridge IRQ");
 // ACK di un device diverso non conferma NTAG e non riceve il comando FE064000.
 Setup();tag[0]=5;Run(10000);assert(!nfc_confirmed_addr && !nfc_capacity);
 assert(nfc_diagnostic.releases==0 && nfc_diagnostic.nacks>0 && nfc_diagnostic.timeouts==0);
 puts("PROBE: header non NTAG rifiutato senza scrivere registri sessione al device sconosciuto");
 // Il pannello può salvare senza competere con un'altra operazione NFC.
 Setup();Run(3);panel_active=true;Run(200);assert(!ee_nfc_bus_reserved);
 assert(CFG_SetValue(CFG_DMX_ADDRESS,3));assert(EE_WriteU16(24,3)==HAL_OK);
 panel_active=false;Run(12000);assert(!strcmp(nfc_status,"SYNCED"));
 // Regressione pannello: Init tag pendente non deve impedire i salvataggi locali.
 Setup();assert(EE_InitWithNfc(true)==HAL_OK);Reset();panel_active=true;
 assert(EE_WriteU8(EE_OFFS_LCD_BACKLIGHT,0)==HAL_OK);
 assert(EE_WriteU8(EE_OFFS_OFF_TIMEOUT_S,0)==HAL_OK);
 assert(memory[0x22]==0 && memory[0x23]==0);
 assert(EE_NfcRead(&meta)==1 && meta.state==EE_NFC_LOCAL);
 // Il riavvio prima della pubblicazione NFC non deve bloccare un nuovo salvataggio esplicito.
 Reset();panel_active=true;assert(EE_WriteU8(EE_OFFS_OFF_TIMEOUT_S,1)==HAL_OK);
 assert(memory[0x23]==1 && EE_NfcRead(&meta)==1 && meta.revision==3);
 puts("PANEL REGRESSION: Init pendente e modifica locale dopo reset non bloccano EEPROM");
 // La build OFF usa lo stesso codice e non avvia IT né accede al tag.
 Setup();NFC_SetEnabled(false);NFC_Init();tw=tag_writes;Run(2000);
 assert(!active && !it_operation && !ee_nfc_bus_reserved && !strcmp(nfc_status,"DISABLED"));
 assert(tag_writes==tw);assert(EE_WriteU8(EE_OFFS_OFF_TIMEOUT_S,0)==HAL_OK);
 uint8_t rtc_byte;assert(RTC_I2C_ReadRegs(0,&rtc_byte,1));
 // Disabilitare durante STOP/READ drena anche la sessione prima di cedere il bus.
 NFC_SetEnabled(true);Run(3);assert(ee_nfc_bus_reserved);
 assert(!RTC_I2C_ReadRegs(0,&rtc_byte,1));assert(EE_WriteU8(0x23,1)==HAL_BUSY);
 NFC_SetEnabled(false);Run(100);assert(!active && !it_operation && !ee_nfc_bus_reserved);
 assert(RTC_I2C_WriteRegs(7,&rtc_byte,1));assert(EE_WriteU8(0x23,1)==HAL_OK);
 char live[320],ee_diag[256];unsigned io_before=io_count,write_before=writes;
 NFC_RecordBoot(1,0x1234);NFC_RecordBoot(4,0);
 NFC_FormatLiveDiagnostics(live,sizeof live);EE_FormatDiagnostics(ee_diag,sizeof ee_diag);
 assert(strstr(live,"enabled=0 owner=0") && strstr(live,"boot=4 reset=00001234"));
 assert(strstr(ee_diag,"addr=23 len=1 rc=0") && io_count==io_before && writes==write_before);
 puts("ISOLATION: NFC OFF, drain STOP/READ, EEPROM/RTC e diagnostica senza I/O");
 // Disabilitare durante pubblicazione completa il readback, non lascia un NDEF troncato.
 Setup();for(unsigned t=0;t<10000 && !nfc_writing;t++) Step();assert(nfc_writing);
 NFC_SetEnabled(false);Run(12000);assert(!ee_nfc_bus_reserved && !nfc_writing && !active);
 assert(!strcmp(nfc_status,"DISABLED") && NfcParse(tag+16,888,&doc));
 assert(EE_NfcRead(&meta)==1 && meta.state==EE_NFC_COMMITTED);
 // Taglio durante invalidazione locale: nessuna modifica ad altri parametri o replay automatico.
 for(int cut=0;cut<24;cut++) {
  Setup();assert(EE_InitWithNfc(true)==HAL_OK);memcpy(legacy,memory,40);
  nfc_cut_bytes=cut;
  if(!setjmp(nfc_power_cut)) (void)EE_WriteU8(0x23,0);
  nfc_cut_bytes=-1;nfc_local_live=false;nfc_transaction=false;
  for(unsigned i=0;i<40;i++) if(i!=0x23) assert(memory[i]==legacy[i]);
  uint8_t after_config[40];memcpy(after_config,memory,40);
  memcpy(nfc_memory,fixture,896);NfcProcess();
  assert(!memcmp(after_config,memory,40));
  if(memory[0x23]!=legacy[0x23]) assert(EE_NfcRead(&meta)==1 && meta.state==EE_NFC_LOCAL);
 }
 puts("LOCAL CUT: 24 scenari di interruzione, parametri preservati, nessun replay; OFF completa la pubblicazione");
 nfc_cuts=0;
 // Campo RF: nessuna scrittura, poi sincronizzazione dopo allontanamento.
 Setup();tw=tag_writes;rf_field=true;Run(5000);assert(tag_writes==tw);rf_field=false;Run(10000);assert(!strcmp(nfc_status,"SYNCED"));

 // RF torna dopo invalidazione NDEF: completa solo se i byte User sono quelli attesi.
 for (unsigned changed=0; changed<3; ++changed) {
  Setup();tw=tag_writes;
  for(unsigned t=0;t<10000 && (tag_writes<tw+2 || it_operation);t++) Step();
  assert(tag_writes>=tw+2 && nfc_writing);
  rf_field=true;Run(1400);
  assert(nfc_resuming && !strcmp(nfc_status,"RF_BUSY"));
  tw=tag_writes;
  if(changed==1) tag[48]^=1;
  if(changed==2) Reset();
  rf_field=false;Run(14000);
  if(changed) assert(tag_writes==tw && !strcmp(nfc_status,"FOREIGN_OR_INVALID"));
  else assert(tag_writes>tw && !strcmp(nfc_status,"SYNCED") && NfcParse(tag+16,888,&doc));
 }
 puts("RF: ripresa dopo invalidazione solo con User attesa; alterazione RF e reboot non sovrascrivono");
 // Simula SET RDM in attesa del salvataggio: il bus rimane libero dopo il drain.
 Setup();Run(12000);Load(argv[3],fixture);OwnIdentity(fixture);
 rdm_save_pending=true;Run(400);assert(!ee_nfc_bus_reserved);
 assert(CFG_SetValue(CFG_DMX_ADDRESS,22));assert(EE_SaveConfig()==HAL_OK);
 assert(EE_NfcRead(&meta)==1 && meta.state==EE_NFC_LOCAL && meta.revision==2);
 memcpy(tag+16,fixture,208);rdm_save_pending=false;Run(12000);
 assert(dmx_address==22 && memory[25]==22 && !strcmp(nfc_status,"TRANSACTION_BASE"));
 puts("RDM: commit differito con bus libero e base NFC precedente rifiutata");
 // JSON/estraneo: nessuna applicazione né cancellazione.
 Setup();memcpy(tag+16,"foreign JSON",12);tw=tag_writes;memcpy(legacy,memory,40);Run(10000);
 assert(tag_writes==tw && !memcmp(legacy,memory,40));assert(memory[40]==255);assert(!strcmp(nfc_status,"FOREIGN_OR_INVALID"));
 // JSON NDEF legacy formalmente valido e magic del dispositivo: preservato, mai importato.
 Setup();n=Load(argv[6],fixture);memcpy(tag+16,fixture,n);tw=tag_writes;memcpy(legacy,memory,40);Run(8000);
 assert(tag_writes==tw && !memcmp(legacy,memory,40) && memory[40]==255);
 // CC insufficiente e lock: nessuna scrittura/tag provisioning automatico.
 Setup();tag[14]=25;tw=tag_writes;Run(6000);assert(tag_writes==tw && memory[40]==255);
 assert(!strcmp(nfc_status,"CAPACITY_ERROR"));
 Setup();tag[10]=1;tw=tag_writes;Run(6000);assert(tag_writes==tw && memory[40]==255);
 assert(!strcmp(nfc_status,"PROTECTED_TAG"));

 // Schema sperimentale proprio: non applicarlo al boot, rigenerarlo solo con Init.
 { FILE *list=fopen(argv[2],"r");assert(list); char file[1024]; int valid;
   while(fscanf(list,"%1023s %d",file,&valid)==2 && !strstr(file,"experimental_schema")) {}
   assert(strstr(file,"experimental_schema") && valid==0);fclose(list);
   Setup();n=Load(file,fixture);
   uint8_t *old=fixture+35;memcpy(old+10,nfc_identity,16);
   unsigned crc_at=NfcU16(old+4)-4;NfcPut32(old+crc_at,NfcCrc(old,crc_at));
   memcpy(tag+16,fixture,n);
   uint8_t header[16];memcpy(header,tag,16);tw=tag_writes;Run(12000);
   assert(tag_writes==tw && !memcmp(tag+16,fixture,n) && memory[40]==255);
   assert(!strcmp(nfc_content,"OWNED_UNSUPPORTED"));
   assert(EE_InitWithNfc(true)==HAL_OK && EE_LoadConfig()==HAL_OK);Reset();
   // Un edit prima della pubblicazione revoca E8: riproduce C3 con vecchio NDEF intatto.
   panel_active=true;assert(CFG_SetValue(CFG_OFF_TIME,12));
   assert(EE_WriteU8(EE_OFFS_OFF_TIMEOUT_S,12)==HAL_OK);
   assert(EE_NfcRead(&meta)==1 && meta.state==EE_NFC_LOCAL);
   panel_active=false;Run(12000);
   NFC_FormatDiagnostics(diag,sizeof diag);
   assert(strstr(diag,"nfc=FOREIGN_OR_INVALID") && strstr(diag,"tag=OWNED_UNSUPPORTED"));
   assert(tag_writes==tw && !memcmp(tag+16,fixture,n) && memory[EE_OFFS_OFF_TIMEOUT_S]==12);
   assert(EE_InitWithNfc(true)==HAL_OK && EE_LoadConfig()==HAL_OK);Reset();Run(16000);
   assert(!strcmp(nfc_status,"SYNCED") && !memcmp(tag,header,16));
   assert(NfcParse(tag+16,888,&doc) && doc.kind==0 && doc.values[CFG_DMX_ADDRESS]==1);
 }
 // Anche Init esplicito lascia intatti lock dinamici, AUTH0 e modalità SRAM.
 for(unsigned guard=0;guard<3;guard++) {
  Setup();assert(EE_InitWithNfc(true)==HAL_OK && EE_LoadConfig()==HAL_OK);Reset();
  if(guard==0) tag[0x388]=1;
  if(guard==1) tag[0x38F]=4;
  if(guard==2) mock_session_mode=0x02;
  uint8_t original_tag[1024];memcpy(original_tag,tag,sizeof tag);tw=tag_writes;Run(10000);
  assert(tag_writes==tw && !memcmp(original_tag,tag,sizeof tag));
  assert(!strcmp(nfc_status,"PROTECTED_TAG"));
 }
 // Metadati riconoscibili ma interrotti: non inizializzare neppure con tag vuoto.
 Setup();memory[40]='N';memory[41]='F';memory[42]=1;memory[43]=0;tw=tag_writes;
 memcpy(legacy,memory,40);Run(8000);assert(tag_writes==tw && !memcmp(legacy,memory,40) && memory[43]==0);
 // Init esplicito: JSON proprio anche con NULL TLV iniziali, default EEPROM mai dal JSON.
 Setup();n=Load(argv[6],fixture);memcpy(tag+16+48,fixture,n);assert(EE_InitWithNfc(true)==HAL_OK);
 assert(EE_LoadConfig()==HAL_OK);Reset();Run(16000);assert(!strcmp(nfc_status,"SYNCED"));rf_field=true;Run(350);
 assert(EE_NfcRead(&meta)==1 && meta.state==EE_NFC_COMMITTED && dmx_address==1 && use_dhcp==1);
 assert(NfcParse(tag+16,888,&doc) && doc.kind==0 && doc.values[CFG_SOURCE_MODE]==CFG_DEFAULT_DMX_MODE);
 tw=tag_writes;Reset();Run(10000);assert(tag_writes==tw);
 // Riproduce il dump reale: solo un blocco non nullo, 48 byte dopo il CC.
 static const uint8_t fragment[]={0x03,0xCA,0xD2,0x25,0xA2,'a','p','p','l','i','c','a','t','i','o','n'};
 for(unsigned scenario=0;scenario<6;scenario++) {
  Setup();memcpy(tag+16+0x30,fragment,sizeof fragment);
  if(scenario==2) tag[16+0x30+15]^=1;
  if(scenario==3) tag[16+0x200]=1;
  if(scenario==4) tag[14]=0x66;
  if(scenario==5) tag[0x38F]=4; // AUTH0 attivo, nessun accesso in scrittura.
  if(scenario) {assert(EE_InitWithNfc(true)==HAL_OK && EE_LoadConfig()==HAL_OK);Reset();}
  uint8_t original_tag[1024];memcpy(original_tag,tag,sizeof tag);
  memcpy(legacy,memory,40);tw=tag_writes;
  // Anche il recupero noto attende la rimozione di URC01.
  rf_field=true;Run(1800);assert(tag_writes==tw);
  rf_field=false;Run(18000);
  panel_active=true;Run(100);assert(!ee_nfc_bus_reserved);
  assert(!memcmp(legacy,memory,40));
  if(scenario==1) {
   assert(tag_writes>tw && !strcmp(nfc_status,"SYNCED") && NfcParse(tag+16,888,&doc));
   assert(!doc.kind && !doc.applied && doc.values[CFG_DMX_ADDRESS]==CFG_DEFAULT_DMX_ADDR);
   assert(EE_NfcRead(&meta)==1 && meta.state==EE_NFC_COMMITTED);
   assert(!memcmp(original_tag,tag,16) && !memcmp(original_tag+0x380,tag+0x380,sizeof tag-0x380));
   tw=tag_writes;Reset();Run(9000);assert(tag_writes==tw);
  } else {
   assert(tag_writes==tw && !memcmp(original_tag,tag,sizeof tag));
   if(scenario) assert(EE_NfcRead(&meta)==1 && meta.state==EE_NFC_INIT_TAG);
  }
 }
 puts("INIT FRAGMENT: dump reale ripubblicato con E8; immagini simili e tag protetti preservati");
 // Nuovo tag2k: CC vergine programmato solo dall'Init, mantenendo UID/indirizzo/lock.
 Setup();memset(tag+12,0,4);uint8_t uid_before[12];memcpy(uid_before,tag,12);
 assert(EE_InitWithNfc(true)==HAL_OK);assert(EE_LoadConfig()==HAL_OK);Reset();Run(22000);
 assert(!strcmp(nfc_status,"SYNCED") && !memcmp(tag,uid_before,12));
 assert(tag[12]==0xE1 && tag[13]==0x10 && tag[14]==0x66 && tag[15]==0);
 // Stesso provisioning all'indirizzo DRV0x55: non scrivere UID04 come nuovo indirizzo.
 Setup();ntag_address=0x55;memset(tag+12,0,4);assert(EE_InitWithNfc(true)==HAL_OK);
 assert(EE_LoadConfig()==HAL_OK);Reset();Run(24000);assert(!strcmp(nfc_status,"SYNCED") && s_nfc_addr==0x55 && tag[0]==4);
 // Init non cancella contenuto estraneo e il consenso resta pendente.
 Setup();memcpy(tag+16,"foreign",7);assert(EE_InitWithNfc(true)==HAL_OK);assert(EE_LoadConfig()==HAL_OK);
 Reset();tw=tag_writes;Run(9000);assert(tag_writes==tw && !memcmp(tag+16,"foreign",7));
 assert(!strcmp(nfc_status,"INIT_UNRECOGNIZED") && !strcmp(nfc_content_detail,"TLV"));
 // Consenso E8 valido, tag vuoto, ma parametro RAM non salvato.
 Setup();assert(EE_InitWithNfc(true)==HAL_OK && EE_LoadConfig()==HAL_OK);Reset();
 assert(CFG_SetValue(CFG_OFF_TIME,12));tw=tag_writes;Run(9000);
 assert(tag_writes==tw && !strcmp(nfc_status,"INIT_CONFIG_MISMATCH"));
 rf_field=true;Run(350);assert(EE_NfcRead(&meta)==1 && meta.state==EE_NFC_INIT_TAG);
 puts("INIT: JSON proprio sostituito senza import, CC vergine2k, UID/lock preservati, consenso consumato una volta");
 // Timeout hardware: nessun lock I2C perpetuo, recupero tramite nuova scansione.
 Setup();it_stall=true;Run(200);assert(bus_resets>0);it_stall=false;Run(18000);assert(!strcmp(nfc_status,"SYNCED"));
 Setup();nack_once=true;Run(12000);assert(!strcmp(nfc_status,"SYNCED"));
 puts("SERVICE: RF busy, contenuto estraneo e timeout I2C con recovery");
 // REQUEST ufficiale, poi replay identico: configurazione salvata una sola volta.
 Setup();n=Load(argv[3],fixture);OwnIdentity(fixture);Meta(1,0,0,EE_NFC_COMMITTED);
 memcpy(nfc_memory,fixture,896);rdm_busy=true;unsigned blocked_writes=writes;
 NfcProcess();assert(!strcmp(nfc_status,"WAIT_RDM") && writes==blocked_writes && dmx_address==1);
 rdm_busy=false;NfcProcess();assert(dmx_address==2);assert(EE_NfcRead(&meta)==1 && meta.request==1 && meta.revision==2);
 unsigned before=writes;NfcProcess();assert(writes==before);assert(nfc_replays>0);
 assert(NfcParse(nfc_image,888,&doc) && doc.kind==0 && doc.applied==1 && doc.revision==2);
 FILE *ack=fopen(argv[7],"wb");assert(ack);
 assert(fwrite(nfc_image,1,nfc_length,ack)==nfc_length);assert(fclose(ack)==0);
 puts("REQUEST: commit/readback, ACK, deduplicazione senza secondo salvataggio");
 // Stesso ID con contenuto diverso: conflitto e nessuna riscrittura.
 uint8_t altered[896];memcpy(altered,fixture,896);NfcPut16(altered+35+34,3);
 NfcPut32(altered+35+161,NfcCrc(altered+35,161));memcpy(nfc_memory,altered,896);
 before=writes;NfcProcess();assert(!strcmp(nfc_status,"DUPLICATE_ID") && writes==before && dmx_address==2);
 // Commit configurazione completato, sigillo metadati ancora REQUEST: solo finalizzazione.
 Setup();Load(argv[3],fixture);OwnIdentity(fixture);nfc_document_t pending;
 assert(NfcParse(fixture,888,&pending));EE_NfcTransaction(true);assert(CFG_SetValue(CFG_DMX_ADDRESS,2));
 assert(EE_SaveConfig()==HAL_OK);EE_NfcTransaction(false);Meta(2,1,pending.crc,EE_NFC_REQUEST);
 memcpy(nfc_memory,fixture,896);memcpy(legacy,memory,40);NfcProcess();
 assert(EE_NfcRead(&meta)==1 && meta.state==EE_NFC_COMMITTED && !memcmp(legacy,memory,40));
 // Transazioni complete attraverso il trasporto: RF write, assenza campo, EEPROM, ACK.
 Setup();Meta(1,0,0,EE_NFC_COMMITTED);Load(argv[3],fixture);OwnIdentity(fixture);
 memcpy(tag+16,fixture,240);Run(11000);
 assert(NfcParse(tag+16,888,&doc) && doc.kind==0 && doc.applied==1 && dmx_address==2);
 for(unsigned cycle=0;cycle<20;cycle++) {
  rf_field=true;Run(350);assert(!ee_nfc_bus_reserved);
  assert(EE_NfcRead(&meta)==1 && NfcSaved(saved));assert(NfcBuild(saved,&meta));
  uint8_t *d=nfc_image+35;NfcPut16(d+34,cycle&1?2:1);
  for(unsigned at=6;at<161;at+=4+NfcU16(d+at+2))if(d[at]==128) {
   d[at+5]=1;NfcPut32(d+at+10,NfcNext(meta.request));break;
  }
  NfcPut32(d+161,NfcCrc(d,161));memcpy(tag+16,nfc_image,240);rf_field=false;Run(10000);rf_field=true;Run(350);
  assert(EE_NfcRead(&meta)==1 && meta.request==cycle+2 && meta.state==EE_NFC_COMMITTED);
  assert(NfcParse(tag+16,888,&doc) && doc.kind==0 && doc.applied==cycle+2);
 }
 puts("TRANSPORT: 21 REQUEST/ACK consecutivi, revisioni e readback persistenti");
 // Locale: revisione invalidata prima del parametro; richiesta vecchia rifiutata.
 Setup();Meta(1,0,0,EE_NFC_COMMITTED);assert(CFG_SetValue(CFG_DMX_ADDRESS,3));assert(EE_WriteU16(24,3)==HAL_OK);
 assert(EE_NfcRead(&meta)==1 && meta.state==EE_NFC_LOCAL && meta.revision==2);
 Load(argv[3],fixture);OwnIdentity(fixture);memcpy(nfc_memory,fixture,896);tick+=300;NfcProcess();assert(dmx_address==3 && !strcmp(nfc_status,"TRANSACTION_BASE"));
 // Reset nello stato locale preparato: non generare una base che accetti dati obsoleti.
 nfc_local_live=false;Meta(2,0,0,EE_NFC_LOCAL);before=writes;NfcProcess();assert(writes==before);
 puts("CONFLICT: scrittura locale, richiesta obsoleta e stato interrotto conservativo");
 // REQUEST con estensione opaca: conserva i byte nella conferma.
 Setup();Load(argv[5],fixture);OwnIdentity(fixture);Meta(1,0,0,EE_NFC_COMMITTED);memcpy(nfc_memory,fixture,896);
 NfcProcess();assert(dmx_address==2);assert(NfcParse(nfc_image,888,&doc));
 assert(doc.kind==0 && doc.extension_count==1 && doc.extension_sizes[0]==8);
 assert(doc.extensions[0][0]==127 && !memcmp(doc.extensions[0]+4,"test",4));
 puts("EXTENSION: sezione non critica conservata byte per byte nel nuovo ACK");
 // Errori I/O in ogni primitiva di commit; dopo reset non riscrivere i parametri.
 for(int failure=0;failure<170;failure++) {
  Setup();Load(argv[3],fixture);OwnIdentity(fixture);Meta(1,0,0,EE_NFC_COMMITTED);memcpy(nfc_memory,fixture,896);
  fail_at=failure;io_count=0;NfcProcess();fail_at=-1;
  uint8_t after[40];memcpy(after,memory,40);ee_nfc_meta_t previous_meta;int previous_valid=EE_NfcRead(&previous_meta);
  bool unstarted=previous_valid==1 && previous_meta.state==EE_NFC_COMMITTED && previous_meta.request==0;
  nfc_local_live=false;nfc_transaction=false;
  if(EE_LoadConfig()==HAL_OK)NfcProcess();
  if(!unstarted)assert(!memcmp(after,memory,40));
 }
 puts("I2C: 170 scenari di errore del commit, deduplicazione dopo reboot");
 // Commit multiparametro, indirizzi BE su wire/valori raw lwIP e reload reale.
 Setup();Load(argv[4],fixture);OwnIdentity(fixture);Meta(1,0,0,EE_NFC_COMMITTED);
 memcpy(nfc_memory,fixture,896);memcpy(legacy,memory,40);NfcProcess();
 assert(NfcParse(nfc_image,888,&doc) && !doc.kind && doc.applied==1);
 assert(EE_LoadConfig()==HAL_OK && NfcSaved(saved) && NfcSameValues(saved,doc.values));
 assert(dmx_address==66 && dmx_port_mode==2 && dmx_mode==3 && artnet_universe==32767 && sacn_universe==63999);
 assert(lcd_backlight==1 && off_timeout_s==7 && use_dhcp==0 && ip_address==0x220AA8C0 &&
        net_mask==0x00FFFFFF && gateway==0x010AA8C0 && dns1==0x08080808 && dns2==0x08080808);
 for(unsigned i=0;i<40;i++) if(i<3 || i==0x1b || i==0x25 || i==0x26) assert(memory[i]==legacy[i]);
 puts("MULTI: tutti i valori ricaricati da EEPROM e byte legacy estranei invariati");
 // Interruzione immediata dopo ogni byte scritto, senza eseguire il cleanup del firmware.
 for(int cut=0;cut<128;cut++) {
  Setup();Load(argv[4],fixture);OwnIdentity(fixture);Meta(1,0,0,EE_NFC_COMMITTED);memcpy(nfc_memory,fixture,896);
  if(!setjmp(nfc_power_cut)){nfc_cut_bytes=cut;NfcProcess();}
  nfc_cut_bytes=-1;
  uint8_t after[40];memcpy(after,memory,40);ee_nfc_meta_t previous_meta;int previous_valid=EE_NfcRead(&previous_meta);
  bool unstarted=previous_valid==1 && previous_meta.state==EE_NFC_COMMITTED && previous_meta.request==0;
  nfc_transaction=false;nfc_local_live=false;
  if(EE_LoadConfig()==HAL_OK)NfcProcess();
  if(!unstarted)assert(!memcmp(after,memory,40));
 }
 assert(nfc_cuts>30);printf("POWER-CUT: %u tagli a livello byte, recovery senza cleanup e senza riscritture legacy\n",nfc_cuts);
 // Anche il consenso Init sopravvive al reset solo dopo verifica dei default.
 unsigned init_cuts=0;
 for(int cut=0;cut<90;cut++) {
  Setup();assert(CFG_SetValue(CFG_DMX_ADDRESS,99));assert(EE_SaveConfig()==HAL_OK);
  unsigned old_cuts=nfc_cuts;
  if(!setjmp(nfc_power_cut)){nfc_cut_bytes=cut;(void)EE_InitWithNfc(true);}
  nfc_cut_bytes=-1;init_cuts+=nfc_cuts-old_cuts;nfc_transaction=false;nfc_local_live=false;
  int state=EE_NfcRead(&meta);
  if(state==1 && meta.state==EE_NFC_INIT_TAG) {
   assert(EE_LoadConfig()==HAL_OK && dmx_address==CFG_DEFAULT_DMX_ADDR);
  } else {
   uint8_t after[40];memcpy(after,memory,40);memset(nfc_memory,0,sizeof nfc_memory);
   if(EE_LoadConfig()==HAL_OK)NfcProcess();
   assert(!memcmp(after,memory,40));
  }
 }
 assert(init_cuts>20);printf("INIT POWER-CUT: %u tagli byte, consenso non attivo prima del readback default\n",init_cuts);
 // Reset pannello completo, verificato, non deve lasciare LOCAL pendente al reboot.
 Setup();Meta(1,0,0,EE_NFC_COMMITTED);assert(EE_ResetUserMemory()==HAL_OK);
 assert(EE_NfcRead(&meta)==1 && meta.state==EE_NFC_COMMITTED);

 return 0;
}
'''

def ndef_tlv(raw):
    mime=b'application/vnd.studiodue.sdcp'
    record=(bytes([0xD2,len(mime),len(raw)]) if len(raw)<=255 else
            bytes([0xC2,len(mime)])+struct.pack('>I',len(raw)))+mime+raw
    size=bytes([len(record)]) if len(record)<255 else bytes([255])+struct.pack('>H',len(record))
    return bytes([3])+size+record+bytes([254])


def read_sections(data):
    sections={}
    at=6
    while at<len(data)-4:
        sid,flags,size=struct.unpack_from('>BBH',data,at)
        sections[sid]=(flags,data[at+4:at+4+size])
        at+=4+size
    assert at==len(data)-4
    return sections


def read_values(data):
    at=0
    while at<len(data):
        pid,size=struct.unpack_from('>HB',data,at)
        at+=3
        selector=bytes(data[at:at+size])
        at+=size
        size=data[at]
        at+=1
        yield at,pid,selector,bytes(data[at:at+size])
        at+=size
    assert at==len(data)


def make_document(sections):
    body=b''.join(struct.pack('>BBH',sid,flags,len(value))+value
                  for sid,(flags,value) in sorted(sections.items()))
    raw=bytes([0x53,0x44,0x20,0])+struct.pack('>H',len(body)+10)+body
    return raw+struct.pack('>I',zlib.crc32(raw))


def verify_catalog(snapshot):
    catalog=json.loads((ROOT/'protocol/nfc/catalogs/ADSW100.json').read_text())
    assert list(catalog)==['format','manufacturer','model','revision','category','name','parameter_count','parameters']
    sections=read_sections(snapshot)
    assert tuple(catalog[k] for k in ('manufacturer','model','category'))==struct.unpack_from('>HHH',sections[1][1])
    assert sections[7]==(1,struct.pack('>I',catalog['revision']))
    assert len(catalog['parameters'])==catalog['parameter_count']==14
    expected=[]
    for p in catalog['parameters']:
        assert p['manufacturer']==(catalog['manufacturer'] if p['pid']>=0x8000 else 0)
        assert p['access']==1
        for selector in p['selectors']:
            expected.append((p['pid'],selector.to_bytes((0,1,4)[p['selector']], 'big'),bytes.fromhex(p['default'])))
    assert expected==[(pid,selector,val) for _,pid,selector,val in read_values(sections[2][1])]


def verify_urc_reference(reference, folder):
    """Interoperabilità con il parser e il client C reali URC01, letti dal checkout indicato."""
    catalog=ROOT/'protocol/nfc/catalogs/ADSW100.json'
    src=r'''
#include "nfc_catalog.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static size_t read_at(void *ctx,uint32_t offset,uint8_t *out,size_t capacity) {
 FILE *f=ctx;assert(!fseek(f,(long)offset,SEEK_SET));return fread(out,1,capacity,f);
}
static size_t load(const char *path,uint8_t out[896]) {
 FILE *f=fopen(path,"rb");assert(f);size_t n=fread(out,1,896,f);assert(!ferror(f));fclose(f);return n;
}
int main(int argc,char **argv) {
 assert(argc==5);
 uint8_t image[896],request_image[896],ack_image[896],encoded[896];
 SD2_Document snap,request,ack;
 size_t n=load(argv[1],image), r=load(argv[2],request_image), a=load(argv[3],ack_image);
 assert(SD2_DecodeMemory(image,n,&snap)==SD2_OK && snap.count==15);
 assert(SD2_DecodeMemory(request_image,r,&request)==SD2_OK);
 assert(SD2_DecodeMemory(ack_image,a,&ack)==SD2_OK);
 FILE *f=fopen(argv[4],"rb");assert(f);
 NC_Context ctx;
 assert(NC_Begin(&ctx,read_at,f));unsigned count=0;uint8_t next;
 while((next=NC_Next(&ctx))==1)++count;
 assert(next==2 && count==14 && ctx.instances==15 && NC_Matches(&ctx,&snap));
 const uint8_t *values[15];unsigned total=0;
 for(unsigned i=0;i<count;i++) {
  assert(NC_Select(&ctx,i) && ctx.parameter.access==1);
  for(unsigned j=0;j<ctx.parameter.selector_count;j++) {
   SD2_Value row;assert(SD2_ValueAt(&snap,total,&row)==SD2_OK);
   assert(row.pid==ctx.parameter.pid);
   assert(NC_ValidValue(&ctx.parameter,row.value.bytes,row.value.length));
   assert(row.value.length==ctx.parameter.default_length &&
          !memcmp(row.value.bytes,ctx.parameter.default_value,row.value.length));
   values[total++]=row.value.bytes;
  }
 }
 uint8_t new_dmx[]={0,2};values[0]=new_dmx;
 size_t used=0;
 assert(SD2_Request(&snap,values,encoded,sizeof encoded,&used)==SD2_OK);
 assert(used==request.encoded.length && !memcmp(encoded,request.encoded.bytes,used));
 uint8_t equal;
 assert(SD2_Acknowledged(&request,&ack,&equal) && equal);
 fclose(f);puts("URC01 C: catalogo streaming, 15 istanze, REQUEST identica e ACK ADSW verificato");
}
'''
    (folder/'urc.c').write_text(src)
    sources=[reference/'Core/Src'/name for name in ('sdcp2.c','nfc_catalog.c','crc32.c')]
    exe=folder/'urc'
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all','-I'+str(reference/'Core/Inc'),
                    str(folder/'urc.c'),*map(str,sources),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(folder/'ADSW100_snapshot.bin'),str(folder/'ADSW100_request.bin'),
                    str(folder/'actual-ack.bin'),str(catalog)],check=True)


def run():
    with tempfile.TemporaryDirectory(prefix='adsw-nfc-tests-') as folder:
        folder = Path(folder)
        common = existing.COMMON.replace('#include <stdlib.h>', '#include <stdlib.h>\n#include <setjmp.h>\nstatic jmp_buf nfc_power_cut;\nstatic int nfc_cut_bytes=-1;\nstatic unsigned nfc_cuts;').replace('typedef struct {unsigned dummy;} I2C_HandleTypeDef;',
                                         'typedef struct {unsigned CR1,CR2,ISR;} I2C_TestRegs; static I2C_TestRegs i2c_regs; typedef struct {unsigned ErrorCode,XferCount,State,Lock; I2C_TestRegs *Instance;} I2C_HandleTypeDef;')
        common = common.replace('memcpy(memory+m,b,n);writes++;', 'for(unsigned i=0;i<n;i++){if(nfc_cut_bytes==0){nfc_cuts++;longjmp(nfc_power_cut,1);}if(nfc_cut_bytes>0)nfc_cut_bytes--;memory[m+i]=b[i];}writes++;')
        # Un IRQ residuo in MSP deve restare mascherato finché HAL non è pronto.
        common = common.replace('static void HAL_NVIC_EnableIRQ(int irq) {(void)irq;}',
                                'static void HAL_NVIC_EnableIRQ(int irq) {(void)irq; extern void TestEnable(void); TestEnable();}')
        # I prototipi reali sono inclusi prima degli stub delle dipendenze.
        (folder/'h_files.h').write_text(common)
        for name in ('stm32h7xx_hal.h','main.h'):
            (folder/name).write_text('')
        (folder/'lwip').mkdir()
        for name in ('udp.h','pbuf.h','netif.h','ip4_addr.h','ip_addr.h','igmp.h','dhcp.h'):
            (folder/'lwip'/name).write_text('')
        src = '#include "h_files.h"\n#include "nfc_i2c.h"\n'
        src += MOCK.replace('static bool RDM_Enabled', 'bool RDM_Enabled').replace('static bool RDM_SavePending','bool RDM_SavePending').replace('static bool DMX512_BusBusy','bool DMX512_BusBusy')
        src += 'void TestEnable(void) {assert(mock_hal_ready);mock_irq_enabled=true;}\n'
        import re
        i2c_source=(ROOT/'Core/Src/i2c.c').read_text()
        for block,fn in [('I2C1_MspInit 0','TestMsp0'),('I2C1_MspInit 1','TestMsp1'),('I2C1_Init 2','TestInit2')]:
            content=re.search(r'/\* USER CODE BEGIN '+block+r' \*/(.*?)/\* USER CODE END '+block+r' \*/',i2c_source,re.S).group(1)
            src += f'static void {fn}(void) {{{content}}}\n'
        for name in ('cfg_io.c','eeprom.c','nfc_i2c.c'):
            src += f'\n#include "{ROOT / "Core/Src" / name}"\n'
        src += '#include \"rtc_i2c.h\"\n' + (ROOT/'Core/Src/rtc_i2c.c').read_text().split('bool RTC_I2C_Init(void)')[0]
        (folder/'test.c').write_text(src+HARNESS)
        exe=folder/'test'
        subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-g','-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all','-Werror=implicit-function-declaration',
                        '-Werror=enum-conversion','-I'+str(folder),'-I'+str(ROOT/'Core/Inc'),
                        str(folder/'test.c'),'-o',str(exe)],check=True)
        vectors=json.loads((ROOT/'protocol/nfc/vectors/vectors.json').read_text())['vectors']
        entries=[]
        for v in vectors:
            raw=bytes.fromhex(v['sdcp'])
            path=folder/(v['name']+'.bin')
            path.write_bytes(ndef_tlv(raw))
            entries.append(f'{path} {int(v["valid"])}\n')
        snapshot=next(bytes.fromhex(v['sdcp']) for v in vectors if v['name']=='ADSW100_snapshot')
        request=next(bytes.fromhex(v['sdcp']) for v in vectors if v['name']=='ADSW100_request')
        verify_catalog(snapshot)
        replacements={0x00F0:bytes.fromhex('0042'),0x0703:bytes.fromhex('00'),
                      0x0706:bytes.fromhex('C0A80A2218'),0x070A:bytes.fromhex('00000000C0A80A01'),
                      0x070B:bytes.fromhex('08080808'),0x9100:bytes.fromhex('02'),0x9101:bytes.fromhex('03'),
                      0x9102:bytes.fromhex('7FFF'),0x9103:bytes.fromhex('F9FF'),0x9104:bytes.fromhex('01'),
                      0x9105:bytes.fromhex('C8'),0x9106:bytes.fromhex('05'),0x9107:bytes.fromhex('00'),
                      0x9108:bytes.fromhex('07')}
        sections=read_sections(request)
        raw=bytearray(sections[2][1])
        for at,pid,_,value in read_values(raw):
            replacement=replacements[pid]
            assert len(replacement)==len(value)
            raw[at:at+len(value)]=replacement
        sections[2]=(1,raw)
        (folder/'multi.bin').write_bytes(ndef_tlv(make_document(sections)))
        sections=read_sections(request)
        sections[127]=(0,b'test')
        (folder/'extended.bin').write_bytes(ndef_tlv(make_document(sections)))
        legacy_json=json.dumps(dict(magic='ADSW100-FC0FE7164570',ver=1,data=dict(dmx_mode=0,universe=1,net=0,subuni=0,prio=0,dhcp=False,ip='10.0.0.1',mask='255.255.255.0',gw='10.0.0.254')),separators=(',',':')).encode()
        mime=b'application/vnd.FGM.adsw100.cfg'
        record=bytes([0xD2,len(mime),len(legacy_json)])+mime+legacy_json
        (folder/'legacy.bin').write_bytes(b'\3'+(bytes([len(record)]) if len(record)<255 else b'\xFF'+struct.pack('>H',len(record)))+record+b'\xFE')
        (folder/'index').write_text(''.join(entries))
        subprocess.run([str(exe),str(folder/'ADSW100_snapshot.bin'),str(folder/'index'),
                        str(folder/'ADSW100_request.bin'),str(folder/'multi.bin'),str(folder/'extended.bin'),str(folder/'legacy.bin'),str(folder/'actual-ack.bin')],check=True)
        if len(sys.argv)>1:
            verify_urc_reference(Path(sys.argv[1]).resolve(),folder)

if __name__=='__main__':run()
