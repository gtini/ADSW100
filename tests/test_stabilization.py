#!/usr/bin/env python3
"""Test host mirati sulle implementazioni reali, con HAL/lwIP simulati.
Eseguire: python3 tests/test_stabilization.py (richiede un compilatore C host).
I mock non qualificano timing, DMA, interrupt o hardware Ethernet/USB.
"""
from pathlib import Path
import os
import re
import concurrent.futures
import sys
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
COMMON = r'''
#ifndef TEST_H_FILES
#define TEST_H_FILES
#define INC_H_FILES_H_
#define __MAIN_H
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint16_t u16_t;
typedef uint8_t u8_t;
typedef int err_t;
typedef struct { uint32_t addr; } ip_addr_t;
typedef ip_addr_t ip4_addr_t;
struct netif { uint8_t hwaddr_len, hwaddr[6]; ip_addr_t ip; };
struct udp_pcb { int dummy; };
struct pbuf { uint16_t len, tot_len; void *payload; };
static struct netif interface;
static struct netif *netif_default = &interface;
static uint32_t tick;
static uint32_t HAL_GetTick(void) { return tick; }
#define USB_Printf(...) ((void)0)
#define LWIP_IPV4 1
#define IPADDR_TYPE_ANY 0
#define IP_ANY_TYPE NULL
#define ERR_OK 0
#define PBUF_TRANSPORT 0
#define PBUF_RAM 0
#define IGMP_ADD_MAC_FILTER 1
#define ip_addr_cmp(a,b) ((a)->addr == (b)->addr)
#define netif_ip4_addr(n) (&(n)->ip)
#define ip4_addr1(a) ((a)->addr & 255)
#define ip4_addr2(a) (((a)->addr >> 8) & 255)
#define ip4_addr3(a) (((a)->addr >> 16) & 255)
#define ip4_addr4(a) (((a)->addr >> 24) & 255)
#define IP4_ADDR(a,b,c,d,e) ((a)->addr = (uint32_t)(uint8_t)(b) | (uint32_t)(uint8_t)(c)<<8 | (uint32_t)(uint8_t)(d)<<16 | (uint32_t)(uint8_t)(e)<<24)
#define lwip_ntohl(a) __builtin_bswap32(a)
static char *ip4addr_ntoa_r(const ip4_addr_t *a, char *s, int n) { snprintf(s,n,"%u.%u.%u.%u",ip4_addr1(a),ip4_addr2(a),ip4_addr3(a),ip4_addr4(a)); return s; }
static char *ipaddr_ntoa(const ip_addr_t *a) { (void)a; return "mock"; }
static struct udp_pcb pcb;
static struct udp_pcb *udp_new_ip_type(int t) { (void)t; return &pcb; }
static int udp_bind(struct udp_pcb *p, void *ip, int port) { (void)p; (void)ip; (void)port; return 0; }
#define udp_recv(...) ((void)0)
#define udp_remove(...) ((void)0)
static uint8_t reply[700];
static int reply_len;
static int udp_sendto(struct udp_pcb *p, struct pbuf *b, const ip_addr_t *ip, uint16_t port) { (void)p;(void)ip;(void)port; memcpy(reply,b->payload,b->tot_len); reply_len=b->tot_len; return 0; }
static struct pbuf *pbuf_alloc(int a, unsigned n, int b) { (void)a;(void)b; struct pbuf *p=malloc(sizeof *p); p->payload=malloc(n);p->tot_len=p->len=n;return p; }
static void pbuf_free(struct pbuf *p) { free(p->payload);free(p); }
static unsigned pbuf_copy_partial(struct pbuf *p, void *out, unsigned n, unsigned off) { memcpy(out,(uint8_t *)p->payload+off,n);return n; }
static int igmp_joingroup_netif(struct netif *n,const ip4_addr_t *g) { (void)n;(void)g;return 0; }
static int igmp_leavegroup_netif(struct netif *n,const ip4_addr_t *g) { (void)n;(void)g;return 0; }
typedef struct { unsigned dummy; } GPIO_TypeDef;
typedef struct { uint32_t ISR,RDR,CR1,CR3; } UART_Registers;
typedef struct { UART_Registers *Instance; unsigned RxState,ReceptionType,ErrorCode;void *RxISR; } UART_HandleTypeDef;
#define USART_ISR_FE 1u
#define USART_ISR_NE 2u
#define USART_ISR_ORE 4u
#define USART_ISR_RXNE_RXFNE 8u
#define UART_CLEAR_FEF 1u
#define UART_CLEAR_NEF 2u
#define UART_CLEAR_OREF 4u
#define USART_CR3_EIE 1u
#define USART_CR1_RXNEIE_RXFNEIE 1u
#define HAL_UART_STATE_READY 0
#define HAL_UART_STATE_BUSY_RX 1
#define HAL_UART_RECEPTION_STANDARD 0
#define HAL_UART_ERROR_NONE 0
#define UART_MASK_COMPUTATION(h) ((void)(h))
#define READ_REG(x) (x)
#define ATOMIC_SET_BIT(x,b) ((x)|=(b))
#define __HAL_UART_CLEAR_FLAG(h,f) ((h)->Instance->ISR &= ~(f))
typedef enum {HAL_OK,HAL_ERROR,HAL_BUSY,HAL_TIMEOUT} HAL_StatusTypeDef;
typedef struct {unsigned dummy;} I2C_HandleTypeDef;
I2C_HandleTypeDef hi2c1;
static uint8_t memory[256];
static bool fail_read,fail_write;
static unsigned writes;
static int fail_at = -1;
static unsigned io_count;
static bool io_fail(void) { return fail_at >= 0 && io_count++ == (unsigned)fail_at; }
#define I2C_MEMADD_SIZE_8BIT 1
static void HAL_Delay(unsigned n) {(void)n;}
static HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h,unsigned a,unsigned t,unsigned ms) {(void)h;(void)a;(void)t;(void)ms;return io_fail()?HAL_ERROR:HAL_OK;}
static HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h,unsigned a,unsigned m,unsigned sz,uint8_t *b,unsigned n,unsigned ms) {(void)h;(void)a;(void)sz;(void)ms;if(fail_read || io_fail())return HAL_ERROR;memcpy(b,memory+m,n);return HAL_OK;}
static HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h,unsigned a,unsigned m,unsigned sz,uint8_t *b,unsigned n,unsigned ms) {(void)h;(void)a;(void)sz;(void)ms;if(fail_write || io_fail())return HAL_ERROR;assert(m+n<=128);memcpy(memory+m,b,n);writes++;return HAL_OK;}
static uint32_t uid[3]={1,2,3};
#define UID_BASE ((uintptr_t)uid)
typedef enum {GPIO_PIN_RESET,GPIO_PIN_SET} GPIO_PinState;
static GPIO_TypeDef gpio;
static uint16_t coils;
static void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint16_t pin,GPIO_PinState state) { (void)p; if(state)coils|=pin;else coils&=(uint16_t)~pin; }
static void HAL_GPIO_TogglePin(GPIO_TypeDef *p,uint16_t pin) { (void)p; coils^=pin; }
static uint32_t __get_PRIMASK(void) {return 0;}
#define __disable_irq() ((void)0)
#define __set_PRIMASK(k) ((void)(k))
#define DMX_LED_GPIO_Port (&gpio)
#define DMX_LED_Pin 0
#define ETH_LED_GPIO_Port (&gpio)
#define ETH_LED_Pin 0
#include "eeprom.h"
#include "defs.h"
#include "artnet.h"
#include "sacn.h"
#include "relay_driver.h"
#include "dmx_512.h"
uint16_t dmx_address=1;
uint8_t dmx_mode,active_proto,off_timeout_s,proto_priority;
uint8_t use_dhcp,lcd_backlight;
uint32_t ip_address,net_mask,gateway,dns1,dns2;
uint16_t dmx_timeout;
#ifndef ART_TEST
uint16_t artnet_universe;
#endif
#ifndef SAC_TEST
uint16_t sacn_universe=1;
uint8_t sacn_transport,sacn_min_prio;
#endif
static void NET_ScheduleApply(void) {}
#endif
'''
ART = r'''
int main(void) {
 const uint8_t *out; uint8_t a[512]={200,1},b[512]={1,220}; ip_addr_t ip={1},ip2={2},ip3={3};
 tick=100; ArtNet_OnDataPacket(0,1,a,2,&ip,6454); assert(ArtNet_GetSlotsView(0,&out)==2 && out[0]==200);
 ArtNet_OnDataPacket(0,1,b,2,&ip,6454); assert(ArtNet_GetSlotsView(0,&out)==2 && out[0]==200);
 ArtNet_OnDataPacket(0,2,b,2,&ip2,6454); assert(ArtNet_GetSlotsView(0,&out)==2 && out[0]==200 && out[1]==220);
 ArtNet_OnDataPacket(0,2,b,2,&ip3,6454); assert(sources[0].ip.addr!=3 && sources[1].ip.addr!=3);
 for(unsigned u=1;u<200;u++) ArtNet_OnDataPacket(u,1,a,2,&ip,6454);
 assert(ArtNet_FindUniverse(0,false));
 tick+=3000; assert(ArtNet_GetSlotsView(0,&out)==0 && out==NULL);
 artnet_universe=0x1234; assert(ArtNet_GetSlotsView(0x1234,&out)==0);
 send_artpoll_reply(&ip,6454); assert(reply_len==239 && reply[14]==0x36 && reply[15]==0x19 && reply[18]==0x12 && reply[19]==3 && reply[190]==4);
 uint8_t pkt[530]={0};memcpy(pkt,"Art-Net",8);pkt[9]=0x50;pkt[11]=14;pkt[14]=0x34;pkt[15]=0x12;pkt[16]=2;pkt[17]=1;
 struct pbuf *p=pbuf_alloc(0,530,0);memcpy(p->payload,pkt,530);artnet_udp_recv(NULL,&pcb,p,&ip,6454);assert(!ArtNet_FindUniverse(0x1234,false));
 pkt[17]=0;p=pbuf_alloc(0,530,0);memcpy(p->payload,pkt,530);artnet_udp_recv(NULL,&pcb,p,&ip,6454);assert(ArtNet_GetSlotsView(0x1234,&out)==512);
 for (unsigned id=0;id<BRAND_COUNT;id++) {
  brand=id;send_artpoll_reply(&ip,6454);assert(reply[24]==0xFA && reply[25]==0x17);
  assert(reply[20]==0xFF && reply[21]==0xFF);
  assert(strcmp((char *)reply+26,Brand_Current()->model)==0);
  assert(strstr((char *)reply+44,Brand_Current()->manufacturer));
 }
 assert(Brand_GetInfo(3)==NULL && Brand_GetInfo(255)==NULL);
 ArtNet_StopListening();assert(ArtNet_GetSlotsView(0x1234,&out)==0);
 return 0;
}
'''
SAC = r'''
struct netif gnetif;
static void send_packet(uint8_t seq,uint8_t prio,uint8_t cid,uint8_t value,uint8_t options) {
 uint8_t b[128]={0}; b[1]=16;memcpy(b+4,"ASC-E1.17",9);b[16]=0x70;b[17]=112;b[21]=4;b[37]=cid;
 b[38]=0x70;b[39]=90;b[43]=2;b[108]=prio;b[111]=seq;b[112]=options;b[114]=1;
 b[115]=0x70;b[116]=13;b[117]=2;b[118]=0xA1;b[122]=1;b[124]=3;b[126]=value;
 struct pbuf *p=pbuf_alloc(0,sizeof b,0);memcpy(p->payload,b,sizeof b);ip_addr_t ip={cid};sacn_rx_cb(NULL,&pcb,p,&ip,5568);
}
int main(void) {
 sacn_universe=1;const uint8_t *out;tick=10;
 send_packet(1,100,1,50,0);assert(sACN_GetSlotsView(1,&out)==2 && out[0]==50);
 send_packet(1,100,1,200,0);assert(sACN_GetSlotsView(1,&out)==2 && out[0]==50);
 send_packet(2,90,2,250,0);assert(sACN_GetSlotsView(1,&out)==2 && out[0]==50);
 send_packet(2,100,3,180,0);assert(sACN_GetSlotsView(1,&out)==2 && out[0]==180);
 send_packet(3,100,3,0,0x40);assert(sACN_GetSlotsView(1,&out)==2 && out[0]==50);
 sacn_min_prio=101;assert(sACN_GetSlotsView(1,&out)==0);sacn_min_prio=0;
 send_packet(2,200,4,255,0x80);assert(sACN_GetSlotsView(1,&out)==2 && out[0]==50);
 tick=2510;assert(sACN_GetSlotsView(1,&out)==0);
 send_packet(255,100,1,10,0);send_packet(0,100,1,20,0);assert(sACN_GetSlotsView(1,&out)==2 && out[0]==20);
 sacn_universe=2;assert(sACN_GetSlotsView(2,&out)==0);sacn_universe=1;
 send_packet(1,201,1,255,0);assert(sACN_GetSlotsView(1,&out)==0);
 return 0;
}
'''
RELAY = r'''
volatile uint8_t dmxRxBuffer[512];volatile int16_t dmxRxCount=512;volatile _DMX_FLAGS dmxFlags;
uint16_t ArtNet_GetSlotsView(uint16_t u,const uint8_t **p) { (void)u;*p=NULL;return 0; }
uint16_t sACN_GetSlotsView(uint16_t u,const uint8_t **p) { (void)u;*p=NULL;return 0; }
int main(void) {
 InitRelay();dmx_timeout=1000;dmxRxBuffer[0]=255;RelayTask();Relay_PulseService1ms();assert(coils&1);
 tick=10;Relay_ManualEnter();dmx_address=512;RelayTask();tick=39;Relay_PulseService1ms();assert(coils&1);
 tick=40;Relay_PulseService1ms();assert((coils&1)==0);
 tick=70;Relay_PulseService1ms();assert(coils&2);tick=110;Relay_PulseService1ms();assert(coils==0);
 tick=140;Relay_PulseService1ms();Relay_ManualToggle(0);Relay_PulseService1ms();assert(coils&1);
 tick=180;Relay_PulseService1ms();assert(coils==0);
 /* Uscita manuale e perdita DMX: attesa OFF in tempo reale. */
 tick=210;Relay_PulseService1ms();Relay_ManualExit();off_timeout_s=2;dmx_timeout=0;RelayTask();Relay_PulseService1ms();
 tick=2209;Relay_PulseService1ms();assert(coils==0);tick=2210;Relay_PulseService1ms();assert(coils&2);
 tick=2250;Relay_PulseService1ms();assert(coils==0);
 /* Impulso oltre wrap tick. */
 InitRelay();Relay_ManualEnter();Relay_ManualToggle(0);tick=UINT32_MAX-10;Relay_PulseService1ms();assert(coils&1);
 tick=29;Relay_PulseService1ms();assert((coils&1)==0);return 0;
}
'''
CFG = r'''
int main(void) {
 char json[768],magic[64],ip[16],mask[16],gw[16];uint8_t mode,net,sub;uint16_t uni,prio;bool dhcp;
 assert(cfg_to_json_vars(1,123,0,0,100,false,"192.168.1.20","255.255.255.0","192.168.1.1","ADSW100-001122334455",json,sizeof json));
 assert(json_to_cfg_vars(json,magic,sizeof magic,&mode,&uni,&net,&sub,&prio,&dhcp,ip,mask,gw));
 assert(apply_config_vars(mode,uni,net,sub,prio,dhcp,ip,mask,gw));assert((ip_address&255)==192 && (ip_address>>24)==20);
 char out[16];ip32be_to_str(ip_address,out);assert(strcmp(out,"192.168.1.20")==0);
 uint32_t before=ip_address;assert(!apply_config_vars(1,123,0,0,100,false,"10.0.0.1junk",mask,gw));assert(before==ip_address);
 char *version=strstr(json,"\"ver\":1");version[6]='2';assert(!json_to_cfg_vars(json,magic,sizeof magic,&mode,&uni,&net,&sub,&prio,&dhcp,ip,mask,gw));version[6]='1';
 assert(!cfg_json_valid("{\"ver\":1,\"ver\":1}"));assert(!cfg_json_valid("{}trailing"));
 unsigned v;assert(!json_find_uint("{\"x\":999999999999999999999}","x",&v));
 return 0;
}
'''

DMX = r'''
static UART_Registers registers;
static UART_HandleTypeDef uart={.Instance=&registers};
static void receive(unsigned flags,unsigned value) {registers.ISR=flags;registers.RDR=value;DMX512UARTInterrupt(&uart);}
int main(void) {
 InitDMX512(&uart);tick=10;receive(USART_ISR_FE,0);receive(8,0);receive(8,200);receive(8,210);
 assert(dmxRxBuffer[0]==0);receive(USART_ISR_FE,0);DMX512_Service();assert(dmxRxCount==2 && dmxRxBuffer[0]==200 && dmx_timeout==1000);
 receive(8,0);receive(8,100);receive(USART_ISR_FE,0);DMX512_Service();assert(dmxRxCount==1 && dmxRxBuffer[0]==100 && dmxRxBuffer[1]==0);
 receive(8,0);receive(8,255);receive(USART_ISR_ORE,0);receive(USART_ISR_FE,0);DMX512_Service();assert(dmxRxBuffer[0]==100);
 tick=1010;DMX512_Service();assert(dmx_timeout==0 && dmxRxCount==0 && dmxRxBuffer[0]==0);
 return 0;
}
'''
EE = r'''
int main(void) {
 memset(memory,255,sizeof memory);assert(EE_InitMemory()==HAL_OK);assert(memory[2]==EE_CFG_VERSION && memory[0x1B]==255);
 assert(EE_LoadConfig()==HAL_OK && dmx_address==1 && dmx_mode==2);unsigned count=writes;
 assert(EE_LoadConfig()==HAL_OK && writes==count);
 uint8_t v=0;assert(EE_Write(128,&v,1)==HAL_ERROR && EE_Write(127,&v,2)==HAL_ERROR && EE_Write(0xFA,&v,1)==HAL_ERROR && writes==count);
 fail_read=true;dmx_address=100;assert(EE_LoadConfig()==HAL_ERROR && dmx_address==100);fail_read=false;
 fail_write=true;dmx_address=200;assert(EE_SaveConfig()!=HAL_OK);fail_write=false;
 assert(EE_SaveConfig()==HAL_OK);dmx_address=1;assert(EE_LoadConfig()==HAL_OK && dmx_address==200);
 assert(memory[2]==EE_CFG_VERSION && memory[0x1B]==255 && memory[0xFA]==255);return 0;
}
'''

BRAND_EE = r"""
static void expect_preserved(const uint8_t *original) {
 for(unsigned i=0;i<128;i++) if(i!=EE_OFFS_VERSION && i!=EE_OFFS_BRAND && i!=EE_OFFS_MENU_CODE) assert(memory[i]==original[i]);
}
int main(void) {
 uint8_t legacy[256];
 memset(memory,255,sizeof memory);assert(EE_LoadConfig()==HAL_ERROR && writes==0);
 assert(EE_InitMemory()==HAL_OK && EE_LoadConfig()==HAL_OK);
 assert(brand==BRAND_AXN && menu_code_required==0);
 assert(memory[EE_OFFS_BRAND]==EE_BRAND_TAG && memory[EE_OFFS_MENU_CODE]==EE_MENU_CODE_TAG);
 dmx_address=407;dmx_mode=3;artnet_universe=12345;sacn_universe=45000;sacn_min_prio=135;
 off_timeout_s=17;proto_priority=5;ip_address=0x12345678;dns1=0x87654321;
 assert(EE_SaveConfig()==HAL_OK);memcpy(legacy,memory,256);
 legacy[EE_OFFS_VERSION]=2;legacy[EE_OFFS_BRAND]=255;legacy[EE_OFFS_MENU_CODE]=255;
 for(unsigned version=0;version<256;version++) {
  memcpy(memory,legacy,256);memory[2]=version;uint8_t original[128];memcpy(original,memory,128);
  assert(EE_LoadConfig()==HAL_OK && brand==BRAND_VIDELIO && menu_code_required==1);
  assert(dmx_address==407 && dmx_mode==3 && artnet_universe==12345 && sacn_universe==45000 && sacn_min_prio==135);
  assert(off_timeout_s==17 && proto_priority==5 && ip_address==0x12345678 && dns1==0x87654321);
  expect_preserved(original);unsigned n=writes;assert(EE_LoadConfig()==HAL_OK && writes==n);
 }
 /* Guasto a ogni operazione I/O della migrazione, compresi read-back e commit. */
 bool reached_end=false;
 for(int cut=0;cut<40;cut++) {
  memcpy(memory,legacy,256);brand=BRAND_AXN;menu_code_required=1;io_count=0;fail_at=cut;
  HAL_StatusTypeDef st=EE_LoadConfig();unsigned operations=io_count;fail_at=-1;
  expect_preserved(legacy);
  if(st==HAL_OK) {assert(brand==BRAND_VIDELIO && menu_code_required==1);}
  else {assert(brand==BRAND_AXN && menu_code_required==1);}
  assert(EE_LoadConfig()==HAL_OK && brand==BRAND_VIDELIO && menu_code_required==1);
  expect_preserved(legacy);unsigned n=writes;assert(EE_LoadConfig()==HAL_OK && writes==n);
  if(operations<=(unsigned)cut) {reached_end=true;break;}
 }
 assert(reached_end);
 for(unsigned id=0;id<BRAND_COUNT;id++) for(unsigned code=0;code<2;code++) {
  if(id==BRAND_GENERIC && code) {assert(EE_SaveIdentity(id,code)==HAL_ERROR);continue;}
  uint8_t active_brand=brand,active_code=menu_code_required;
  assert(EE_SaveIdentity(id,code)==HAL_OK);assert(brand==active_brand && menu_code_required==active_code);
  assert(EE_LoadConfig()==HAL_OK && brand==id && menu_code_required==code);
  assert(strcmp(Brand_Current()->model,id==BRAND_VIDELIO?"ADSW100":"NODIM01")==0);expect_preserved(legacy);
 }
 active_proto=2;dmx_address=408;assert(EE_SaveIdentity(BRAND_AXN,1)==HAL_OK);assert(active_proto==2 && dmx_address==408);dmx_address=407;
 /* Tutte le transizioni ammesse restano recuperabili dopo un errore I2C,
    incluso Generic/OFF senza stati pendenti Generic/ON. */
 for(unsigned from=0;from<BRAND_COUNT;from++) for(unsigned fc=0;fc<2;fc++) {
  if(!Brand_IsIdentityValid(from,fc)) continue;
  assert(EE_SaveIdentity(from,fc)==HAL_OK && EE_LoadConfig()==HAL_OK);
  uint8_t original[256];memcpy(original,memory,256);
  for(unsigned to=0;to<BRAND_COUNT;to++) for(unsigned tc=0;tc<2;tc++) {
   if(!Brand_IsIdentityValid(to,tc))continue;
   for(int cut=0;cut<40;cut++) {
    memcpy(memory,original,256);io_count=0;fail_at=cut;
    (void)EE_SaveIdentity(to,tc);unsigned operations=io_count;fail_at=-1;
    expect_preserved(original);assert(EE_LoadConfig()==HAL_OK && Brand_IsIdentityValid(brand,menu_code_required));
    unsigned before=writes;assert(EE_LoadConfig()==HAL_OK && writes==before);
    if(operations<=(unsigned)cut) break;
   }
  }
 }
 assert(EE_SaveIdentity(BRAND_AXN,1)==HAL_OK && EE_LoadConfig()==HAL_OK);
 unsigned n=writes;assert(EE_SaveIdentity(3,0)==HAL_ERROR && EE_SaveIdentity(255,1)==HAL_ERROR && EE_SaveIdentity(0,2)==HAL_ERROR && writes==n);
 for(unsigned raw=0;raw<256;raw++) {
  if(raw>=EE_BRAND_TAG && raw<EE_BRAND_TAG+BRAND_COUNT) continue;
  memory[EE_OFFS_BRAND]=raw;assert(EE_LoadConfig()==HAL_ERROR && writes==n);
 }
 memory[EE_OFFS_BRAND]=EE_BRAND_TAG;
 for(unsigned raw=0;raw<256;raw++) {
  if(raw==EE_MENU_CODE_TAG || raw==(EE_MENU_CODE_TAG|1)) continue;
  memory[EE_OFFS_MENU_CODE]=raw;assert(EE_LoadConfig()==HAL_ERROR && writes==n);
 }
 memory[EE_OFFS_MENU_CODE]=EE_MENU_CODE_TAG;memory[2]=3;assert(EE_LoadConfig()==HAL_ERROR && writes==n);
 memcpy(memory,legacy,256);memory[0]=0;assert(EE_LoadConfig()==HAL_ERROR && writes==n);
 return 0;
}
"""

UI = r"""
int main(void) {
 memset(memory,255,sizeof memory);assert(EE_InitMemory()==HAL_OK && EE_LoadConfig()==HAL_OK);
 menu_code_required=0;g_ui.state=UI_HOME;UI_RequestMenu();assert(g_ui.state==UI_MENU && g_ui.sel==0);
 menu_code_required=1;g_ui.state=UI_HOME;UI_RequestMenu();assert(g_ui.state==UI_PAGE_UNLOCK);
 s_unlock_digits[0]=UNLOCK_CODE_1;s_unlock_digits[1]=UNLOCK_CODE_2;s_unlock_digits[2]=UNLOCK_CODE_3;s_unlock_digits[3]=UNLOCK_CODE_4;s_unlock_pos=3;
 handle_page_unlock_event(EVT_BTN_SHORT);assert(g_ui.state==UI_MENU);
 UI_RequestMenu();s_unlock_pos=3;handle_page_unlock_event(EVT_BTN_SHORT);assert(g_ui.state==UI_HOME);
 /* System Setup conserva i quattro campi originali: nessun edit identità. */
 uint8_t saved_brand=brand,saved_code=menu_code_required;
 g_ui.editing=false;s_sys_sel=0;
 for(unsigned i=0;i<8;i++) {handle_page_sys_event(EVT_ENC_RIGHT);assert(s_sys_sel<4);}
 assert(brand==saved_brand && menu_code_required==saved_code);
 assert(EE_SaveIdentity(BRAND_VIDELIO,1)==HAL_OK && EE_LoadConfig()==HAL_OK);
 dmx_address=100;assert(EE_SaveConfig()==HAL_OK);
 assert(EE_ResetUserMemory()==HAL_OK && EE_LoadConfig()==HAL_OK);
 assert(brand==BRAND_VIDELIO && menu_code_required==1 && dmx_address==1);
 return 0;
}
"""

def ui_functions():
    """Compila le tre funzioni reali isolate con mock, senza riscriverne la logica."""
    source = (ROOT/'Core/Src/user_interface.c').read_text()
    text = r"""
#include "user_interface.h"
static struct { ui_state_t state;bool editing,force_redraw;uint8_t sel; } g_ui;
static uint8_t s_unlock_pos,s_unlock_digits[4],s_sys_sel;
static void LCD_DrawToast(const char *s) {(void)s;}
static void LCD_EditMarker(uint8_t r,bool e) {(void)r;(void)e;}
static void HardFactoryReset(bool preserve) {assert(preserve);}
"""
    for signature in ['static void UI_RequestMenu(void)', 'static void handle_page_unlock_event(ui_event_t ev)',
                      'static void handle_page_sys_event(ui_event_t ev)']:
        start = source.index(signature)
        end = source.index('{', start)
        depth = 1
        end += 1
        while depth:
            if source[end] == '{': depth += 1
            elif source[end] == '}': depth -= 1
            end += 1
        text += source[start:end]+'\n'
    return text

CDC_MOCK = r"""
#undef USB_Printf
#define USBD_STATE_CONFIGURED 1
#define USBD_OK 0
#define USBD_BUSY 1
typedef struct {uint32_t TxState;} USBD_CDC_HandleTypeDef;
typedef struct {uint8_t dev_state;void *pClassData;} USBD_HandleTypeDef;
static USBD_CDC_HandleTypeDef cdc;
USBD_HandleTypeDef hUsbDeviceFS={USBD_STATE_CONFIGURED,&cdc};
static char sent[256];
static unsigned tx_calls;
static uint8_t CDC_Transmit_FS(uint8_t *b,uint16_t n) {
 if(cdc.TxState)return USBD_BUSY;
 assert(n<sizeof sent);memcpy(sent,b,n);sent[n]=0;cdc.TxState=1;tx_calls++;return USBD_OK;
}
"""
CDC = r"""
static void command(const char *line,const char *expected) {
 cdc.TxState=0;unsigned before=writes;
 USB_Debug_OnRxBytes((const uint8_t *)line,(uint32_t)strlen(line));assert(writes==before);
 USB_ServiceTask();assert(s_service_response_pending);USB_ServiceTask();assert(strcmp(sent,expected)==0);
 cdc.TxState=0;
}
int main(void) {
 memset(memory,255,sizeof memory);assert(EE_InitMemory()==HAL_OK && EE_LoadConfig()==HAL_OK);
 assert(brand==BRAND_AXN && menu_code_required==0);tick=1;
 command("brand?\r\n","brand=0\r\n");command("menu_code?\n","menu_code=0\r\n");
 command("brand=1\n","OK REBOOT\r\n");assert(brand==BRAND_AXN && menu_code_required==0);
 command("brand?\n","brand=1\r\n");command("menu_code?\n","menu_code=1\r\n");
 command("menu_code=0\n","OK REBOOT\r\n");command("menu_code?\n","menu_code=0\r\n");
 command("brand=2\n","OK REBOOT\r\n");command("menu_code=1\n","ERROR\r\n");
 command("brand=0\n","OK REBOOT\r\n");command("menu_code?\n","menu_code=0\r\n");
 command("menu_code=1\n","OK REBOOT\r\n");assert(EE_LoadConfig()==HAL_OK && menu_code_required==1);
 unsigned before=writes;command("brand=3\n","ERROR\r\n");command("brand=-1\n","ERROR\r\n");
 command("brand=9999999999\n","ERROR\r\n");command("menu_code=2\n","ERROR\r\n");assert(writes==before);
 command("brand=001\n","OK REBOOT\r\n");command("menu_code?\n","menu_code=1\r\n");
 fail_write=true;command("brand=0\n","ERROR\r\n");fail_write=false;command("brand?\n","brand=1\r\n");
 fail_read=true;command("brand?\n","ERROR\r\n");fail_read=false;
 char overflow[220];memset(overflow,'X',sizeof overflow);memcpy(overflow+200,"brand=0\n",8);overflow[208]=0;
 before=writes;command(overflow,"ERROR\r\n");assert(writes==before);
 const uint8_t nul[]={'b','r','a','n','d','=','0',0,'\n'};USB_Debug_OnRxBytes(nul,sizeof nul);USB_ServiceTask();USB_ServiceTask();assert(strcmp(sent,"ERROR\r\n")==0);cdc.TxState=0;
 tick=29999;command("brand=0\n","OK REBOOT\r\n");
 /* Frammenti completi soltanto al limite: rifiutati. */
 USB_Debug_OnRxBytes((const uint8_t *)"brand=",6);USB_ServiceTask();assert(!s_service_response_pending);
 tick=30000;USB_Debug_OnRxBytes((const uint8_t *)"1\n",2);before=writes;USB_ServiceTask();USB_ServiceTask();assert(strcmp(sent,"LOCKED\r\n")==0 && writes==before);cdc.TxState=0;
 command("brand=2\n","LOCKED\r\n");command("menu_code=1\n","LOCKED\r\n");
 command("brand?\n","brand=0\r\n");command("menu_code?\n","menu_code=0\r\n");
 hUsbDeviceFS.dev_state=0;USB_ServiceTask();hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED;
 tick=1;command("brand=1\n","LOCKED\r\n"); /* Simula wrap: finestra non riaperta. */
 /* BUSY non perde la risposta e non permette a log di scavalcarla. */
 USB_Debug_OnRxBytes((const uint8_t *)"brand?\n",7);USB_ServiceTask();unsigned sends=tx_calls;
 cdc.TxState=1;USB_ServiceTask();USB_Send("debug");assert(tx_calls==sends && s_service_response_pending);
 cdc.TxState=0;USB_ServiceTask();assert(strcmp(sent,"brand=0\r\n")==0);
 return 0;
}
"""

def run():
    with tempfile.TemporaryDirectory(prefix='adsw100-tests-') as folder:
        tmp = Path(folder)
        (tmp / 'h_files.h').write_text(COMMON)
        for name in ['stm32h7xx_hal.h', 'main.h', 'ethernetif.h', 'usb_device.h', 'usbd_cdc_if.h', 'usbd_cdc.h']:
            (tmp / name).write_text('')
        for name in ['udp.h', 'pbuf.h', 'netif.h', 'ip4_addr.h', 'ip_addr.h', 'igmp.h']:
            (tmp / 'lwip').mkdir(exist_ok=True)
            (tmp / 'lwip' / name).write_text('')
        gpio = []
        for ch in range(1, 13):
            # Per i test basta osservare la prima uscita; le altre usano pin distinti.
            for state, bit in [('ON', 0), ('OFF', 1)]:
                gpio += [f'#define CH{ch:02}_{state}_GPIO_Port (&gpio)',
                         f'#define CH{ch:02}_{state}_Pin {1 << (bit if ch == 1 else 2 + ((ch-2) % 14))}u']
        (tmp / 'main.h').write_text('\n'.join(gpio))
        cases = [('ART_TEST', 'artnet.c', ART), ('SAC_TEST', 'sacn.c', SAC),
                 ('RELAY_TEST', 'relay_driver.c', RELAY), ('CFG_TEST', 'cfg_io.c', CFG),
                 ('DMX_TEST', 'dmx_512.c', DMX), ('EE_TEST', 'eeprom.c', EE),
                 ('BRAND_EE_TEST', 'eeprom.c', BRAND_EE), ('UI_TEST', 'eeprom.c', UI), ('CDC_TEST', 'usb_debug.c', CDC)]
        for define, filename, body in cases:
            harness = tmp / 'test.c'
            identity = f'#include "{ROOT / "Core/Src/cfg_io.c"}"\n' if filename in ('artnet.c', 'eeprom.c', 'usb_debug.c') else ''
            ui = ui_functions() if define == 'UI_TEST' else ''
            if define == 'CDC_TEST':
                identity += f'#include "{ROOT / "Core/Src/eeprom.c"}"\n' + CDC_MOCK
            harness.write_text(f'#define {define}\n#include "h_files.h"\n' + identity + f'#include "{ROOT / "Core/Src" / filename}"\n' + ui + body)
            exe = tmp / 'test'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Werror=implicit-function-declaration',
                            '-I'+str(tmp), '-I'+str(ROOT/'Core/Inc'), str(harness), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
            print(define + ': PASS')

def arm_build(output):
    """Build C/ASM completa con GCC ARM di CubeIDE, senza cambiare il progetto.
    Usa gli include Debug da .cproject e il linker ADSW100; non esegue flash.
    """
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    candidates = list(Path('/Applications/STM32CubeIDE.app').rglob('arm-none-eabi-gcc'))
    cc = os.environ.get('ARM_CC') or (str(candidates[0]) if candidates else None)
    if not cc:
        raise SystemExit('Impostare ARM_CC con il percorso arm-none-eabi-gcc')
    project = (ROOT/'.cproject').read_text().split('<sourceEntries>')[0]
    includes = list(dict.fromkeys(re.findall(r'value="(\.\./[^"\n]+)"', project)))
    flags = ['-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
             '-std=gnu11', '-O0', '-g3', '-ffunction-sections', '-fdata-sections', '-Wall',
             '-DDEBUG', '-DUSE_HAL_DRIVER', '-DSTM32H743xx', '-DUSE_PWR_LDO_SUPPLY']
    flags += ['-I'+str(ROOT/p[3:]) for p in includes]
    sources = []
    for folder in ['Core', 'LWIP', 'USB_DEVICE', 'Middlewares', 'Drivers/STM32H7xx_HAL_Driver/Src']:
        sources += [p for p in (ROOT/folder).rglob('*.c') if 'template' not in p.name]
    sources += list((ROOT/'Core').rglob('*.s'))
    def compile_one(item):
        index, source = item
        obj = output/f'{index}.o'
        result = subprocess.run([cc, *flags, '-c', str(source), '-o', str(obj)], capture_output=True, text=True)
        return obj, result
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        results = list(pool.map(compile_one, enumerate(sources)))
    (output/'compile.log').write_text(''.join(p.stdout+p.stderr for _, p in results))
    failures = [p for _, p in results if p.returncode]
    if failures:
        raise SystemExit('Compilazione fallita: vedere '+str(output/'compile.log'))
    result = subprocess.run([cc, *flags[:4], '--specs=nano.specs', '--specs=nosys.specs',
                             '-T'+str(ROOT/'STM32H743VITX_FLASH.ld'),
                             '-Wl,--gc-sections,-Map='+str(output/'ADSW100.map'),
                             *[str(obj) for obj, _ in results], '-Wl,--start-group', '-lc', '-lm',
                             '-Wl,--end-group', '-o', str(output/'ADSW100.elf')], capture_output=True, text=True)
    (output/'link.log').write_text(result.stdout+result.stderr)
    if result.returncode:
        raise SystemExit('Link fallito: vedere '+str(output/'link.log'))
    print(f'ARM BUILD: PASS ({len(sources)} unità, ELF/map/log in {output})')


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--arm-build':
        arm_build(sys.argv[2])
    else:
        run()
