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
struct netif { uint8_t hwaddr_len, hwaddr[6]; ip_addr_t ip,mask; };
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
#define lwip_htonl(a) __builtin_bswap32(a)
#define dhcp_supplied_address(n) (0)
#define netif_ip4_netmask(n) (&(n)->mask)
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
typedef struct { uint32_t ISR,RDR,CR1,CR3,TDR,RQR; } UART_Registers;
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
static uint32_t hardware_uid[3]={1,2,3};
#define UID_BASE ((uintptr_t)hardware_uid)
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
#include "cfg_io.h"
#include "rdm.h"
static void NET_ScheduleApply(void) {}
static void LCD_Backlight_Wake(void) {}
#if !defined(EE_TEST) && !defined(BRAND_EE_TEST) && !defined(UI_TEST) && !defined(CDC_TEST) && !defined(NODE_RDM_TEST)
uint8_t dmx_port_mode;
#endif
/* Registri/tempo simulati per il proprietario USART/TIM7. */
static uint32_t SystemCoreClock=480000000;
static struct {uint32_t CTRL,CYCCNT;} dwt_registers;
#define DWT (&dwt_registers)
static struct {uint32_t DEMCR;} debug_registers;
#define CoreDebug (&debug_registers)
#define CoreDebug_DEMCR_TRCENA_Msk 1u
#define DWT_CTRL_CYCCNTENA_Msk 1u
static struct {uint32_t CR1,ARR,CNT,EGR,SR,DIER,PSC;} tim7_registers;
#define TIM7 (&tim7_registers)
#define TIM7_IRQn 55
#define TIM_EGR_UG 1u
#define TIM_DIER_UIE 1u
#define TIM_CR1_URS 4u
#define TIM_SR_UIF 1u
#define __DSB() ((void)0)
#define TIM_CR1_OPM 8u
#define TIM_CR1_CEN 1u
#define USART_CR1_RE 16u
#define USART_ISR_TXE_TXFNF 32u
#define USART_ISR_TC 16u
#define USART_CR1_TXEIE_TXFNFIE 32u
#define USART_CR1_TCIE 64u
#define UART_CLEAR_TCF 16u
#define USART_RQR_RXFRQ 1u
#define GPIO_MODE_OUTPUT_PP 1u
#define GPIO_MODE_AF_PP 2u
#define GPIO_SPEED_FREQ_HIGH 3u
#define GPIO_AF4_USART1 4u
#define SET_BIT(r,b) ((r)|=(b))
#define CLEAR_BIT(r,b) ((r)&=~(b))
#define RDM_DIR_GPIO_Port (&gpio)
#define RDM_DIR_Pin 32768u
#define RDM_TXD_GPIO_Port (&gpio)
#define RDM_TXD_Pin 16384u
#define RDM_RXD_GPIO_Port (&gpio)
#define RDM_RXD_Pin 8192u
#define LCD_BL_GPIO_Port (&gpio)
#define LCD_BL_Pin 4096u
typedef struct {uint32_t Pin,Mode,Speed,Alternate;} GPIO_InitTypeDef;
static bool line_high=true;
static GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p,uint16_t pin) {(void)p;(void)pin;return line_high?GPIO_PIN_SET:GPIO_PIN_RESET;}
static void HAL_GPIO_Init(GPIO_TypeDef *p,GPIO_InitTypeDef *pin) {(void)p;(void)pin;}
#define __HAL_RCC_TIM7_CLK_ENABLE() ((void)0)
static uint32_t HAL_RCC_GetPCLK1Freq(void) {return 120000000;}
static void HAL_NVIC_SetPriority(int irq,int a,int b) {(void)irq;(void)a;(void)b;}
static unsigned timer_pending_clears;
static void HAL_NVIC_ClearPendingIRQ(int irq) {(void)irq;++timer_pending_clears;}
static void HAL_NVIC_EnableIRQ(int irq) {(void)irq;}
#ifdef DMX_TEST
static void RdmTestReceive(const uint8_t *q,uint16_t n,uint32_t end) {(void)q;(void)n;(void)end;}
#define RDM_Receive RdmTestReceive
uint16_t ArtNet_GetSlotsView(uint16_t u,const uint8_t **p) {(void)u;*p=NULL;return 0;}
uint16_t sACN_GetSlotsView(uint16_t u,const uint8_t **p) {(void)u;*p=NULL;return 0;}
#endif
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
 for(unsigned i=0;i<128;i++) if(i!=EE_OFFS_VERSION && i!=EE_OFFS_BRAND && i!=EE_OFFS_MENU_CODE && i!=EE_OFFS_DMX_PORT_MODE) assert(memory[i]==original[i]);
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
 memory[EE_OFFS_MENU_CODE]=EE_MENU_CODE_TAG;memory[2]=4;assert(EE_LoadConfig()==HAL_ERROR && writes==n);
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
 /* Menu DMX: candidato, annullamento, persistenza e universi condivisi. */
 s_dmx_sel=1;g_ui.editing=false;g_ui.state=UI_PAGE_DMX;
 handle_page_dmx_event(EVT_BTN_SHORT);handle_page_dmx_event(EVT_ENC_RIGHT);assert(dmx_port_mode==0);
 handle_page_dmx_event(EVT_BTN_LONG);assert(dmx_port_mode==0);
 handle_page_dmx_event(EVT_BTN_SHORT);handle_page_dmx_event(EVT_ENC_RIGHT);handle_page_dmx_event(EVT_BTN_SHORT);
 assert(dmx_port_mode==1 && EE_LoadConfig()==HAL_OK && dmx_port_mode==1);
 s_dmx_sel=2;handle_page_dmx_event(EVT_BTN_SHORT);for(unsigned i=0;i<3;i++)handle_page_dmx_event(EVT_ENC_RIGHT);handle_page_dmx_event(EVT_BTN_SHORT);
 assert(artnet_universe==3 && sacn_universe==1);
 s_dmx_sel=1;handle_page_dmx_event(EVT_BTN_SHORT);handle_page_dmx_event(EVT_ENC_RIGHT);handle_page_dmx_event(EVT_BTN_SHORT);
 assert(dmx_port_mode==2);s_dmx_sel=2;handle_page_dmx_event(EVT_BTN_SHORT);handle_page_dmx_event(EVT_ENC_RIGHT);handle_page_dmx_event(EVT_BTN_SHORT);
 assert(sacn_universe==2 && artnet_universe==3 && EE_LoadConfig()==HAL_OK && dmx_port_mode==2);
 return 0;
}
"""

def ui_functions():
    """Compila le tre funzioni reali isolate con mock, senza riscriverne la logica."""
    source = (ROOT/'Core/Src/user_interface.c').read_text()
    text = r"""
#include "user_interface.h"
static struct { ui_state_t state;bool editing,force_redraw;uint8_t sel; } g_ui;
static uint8_t s_unlock_pos,s_unlock_digits[4],s_sys_sel,s_dmx_sel;
static uint32_t s_dmx_edit_value;
static void LCD_DrawToast(const char *s) {(void)s;}
static void LCD_EditMarker(uint8_t r,bool e) {(void)r;(void)e;}
static void HardFactoryReset(bool preserve) {assert(preserve);}
"""
    for signature in ['static void UI_RequestMenu(void)', 'static void handle_page_unlock_event(ui_event_t ev)',
                      'static void handle_page_sys_event(ui_event_t ev)', 'static cfg_parameter_t UI_DmxParameter(void)',
                      'static void handle_page_dmx_event(ui_event_t ev)']:
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
void RDM_FormatDiagnostics(char *out,size_t capacity) {snprintf(out,capacity,"rdm=1 uid=17FA:A3010203 rx=0 late=0\r\n");}
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
 command("rdm?\n","rdm=1 uid=17FA:A3010203 rx=0 late=0\r\n");
 hUsbDeviceFS.dev_state=0;USB_ServiceTask();hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED;
 tick=1;command("brand=1\n","LOCKED\r\n"); /* Simula wrap: finestra non riaperta. */
 /* BUSY non perde la risposta e non permette a log di scavalcarla. */
 USB_Debug_OnRxBytes((const uint8_t *)"brand?\n",7);USB_ServiceTask();unsigned sends=tx_calls;
 cdc.TxState=1;USB_ServiceTask();USB_Send("debug");assert(tx_calls==sends && s_service_response_pending);
 cdc.TxState=0;USB_ServiceTask();assert(strcmp(sent,"brand=0\r\n")==0);
 return 0;
}
"""

NODE_RDM = r"""
struct netif gnetif;
static uint8_t art_levels[512],sac_levels[512];
static bool art_valid=true,sac_valid=true;
uint16_t ArtNet_GetSlotsView(uint16_t universe,const uint8_t **p) {(void)universe;*p=art_valid?art_levels:NULL;return art_valid?512:0;}
uint16_t sACN_GetSlotsView(uint16_t universe,const uint8_t **p) {(void)universe;*p=sac_valid?sac_levels:NULL;return sac_valid?512:0;}
static UART_Registers registers;
static UART_HandleTypeDef uart={.Instance=&registers};
static uint64_t time_us;
static void advance_us(unsigned us) {time_us+=us;DWT->CYCCNT=(uint32_t)(time_us*480u);tick=(uint32_t)(time_us/1000);}
static void timer(void) {unsigned period=TIM7->ARR+1;advance_us(period);TIM7->SR|=TIM_SR_UIF;TIM7_IRQHandler();}
static void receive(unsigned flags,unsigned value) {registers.ISR=flags;registers.RDR=value;DMX512UARTInterrupt(&uart);}
static void finish_tx(void) {
 unsigned guard=0;
 while(tx_state && tx_state!=3 && ++guard<10) timer();assert(tx_state==3);
 while(tx_state==3) {registers.ISR=USART_ISR_TXE_TXFNF;DMX512UARTInterrupt(&uart);advance_us(44);}
 assert(tx_state==4);receive(USART_ISR_TC,0);assert(tx_state==0 && !(coils&RDM_DIR_Pin));
}
static uint8_t request[257];
static unsigned build(unsigned pid,unsigned cc,const uint8_t *payload,unsigned count,bool broadcast) {
 memset(request,0,sizeof request);request[0]=0xCC;request[1]=1;request[2]=24+count;
 if(broadcast)memset(request+3,255,6);else memcpy(request+3,uid,6);
 request[9]=0x17;request[10]=0xFA;request[14]=1;request[15]=7;request[16]=1;
 request[20]=cc;PutWord(request+21,pid);request[23]=count;if(count)memcpy(request+24,payload,count);
 uint16_t sum=0;for(unsigned i=0;i<request[2];i++)sum+=request[i];PutWord(request+request[2],sum);return request[2]+2;
}
static void transact(unsigned pid,unsigned cc,const uint8_t *payload,unsigned count) {
 unsigned n=build(pid,cc,payload,count,false);RDM_Receive(request,n,DMX512_Cycles());assert(tx_state);finish_tx();assert(RDM_PacketValid(tx_data,tx_count));
}
int main(void) {
 memset(memory,255,sizeof memory);assert(EE_InitMemory()==HAL_OK && EE_LoadConfig()==HAL_OK);
 uint8_t eui[]={0,4,0xA3,1,2,3};memcpy(memory+0xFA,eui,6);RDM_Init();assert(RDM_Enabled());
 assert(uid[0]==0x17 && uid[1]==0xFA && uid[2]==0xA3 && uid[5]==3);
 InitDMX512(&uart);
 /* Richiesta NVIC spuria: non può anticipare turnaround/BREAK/MAB. */
 uint8_t probe[2]={0xFE,0xAA};assert(DMX512_SendPacket(probe,2,true,DMX512_Cycles()));
 assert((TIM7->CR1&TIM_CR1_URS) && timer_pending_clears);
 TIM7_IRQHandler();assert(tx_state==5 && TIM7->ARR==199);
 timer();assert(tx_state==3);
 receive(USART_ISR_FE|USART_ISR_NE|USART_ISR_ORE,0);
 assert(!(registers.ISR&(USART_ISR_FE|USART_ISR_NE|USART_ISR_ORE)) && tx_state==3);
 finish_tx();assert(!TIM7->DIER && !(TIM7->CR1&TIM_CR1_CEN));
 TIM7->SR=TIM_SR_UIF;TIM7_IRQHandler();assert(!tx_state);TIM7->SR=0;
 for(unsigned b=0;b<3;b++){brand=b;transact(0x0081,0x20,NULL,0);assert(tx_data[23]==strlen(Brand_Current()->manufacturer) && !memcmp(tx_data+24,Brand_Current()->manufacturer,tx_data[23]));transact(0x0080,0x20,NULL,0);assert(!memcmp(tx_data+24,Brand_Current()->model,tx_data[23]));}brand=0;
 transact(0x0060,0x20,NULL,0);assert(tx_data[16]==0 && tx_data[23]==19 && Word(tx_data+26)==ADSW_RDM_MODEL_ID && Word(tx_data+34)==12);
 transact(0x0081,0x20,NULL,0);assert(tx_data[23]==strlen(Brand_Current()->manufacturer));
 uint8_t address[]={1,0xF0};transact(0x00F0,0x30,address,2);assert(dmx_address==496 && tx_data[16]==0);
 transact(0x00F0,0x20,NULL,0);assert(Word(tx_data+24)==496);
 address[0]=address[1]=0;transact(0x00F0,0x30,address,2);assert(tx_data[16]==2 && Word(tx_data+24)==6 && dmx_address==496);
 transact(0xFFFF,0x20,NULL,0);assert(tx_data[16]==2 && Word(tx_data+24)==0);
 transact(0x0060,0x30,NULL,0);assert(tx_data[16]==2 && Word(tx_data+24)==5);
 uint8_t identify_on=1;transact(0x1000,0x30,&identify_on,1);assert(identify);identify_on=0;transact(0x1000,0x30,&identify_on,1);assert(!identify);
 uint8_t label[]={'N','o','d','e'};transact(0x0082,0x30,label,4);transact(0x0082,0x20,NULL,0);assert(tx_data[23]==4 && memcmp(tx_data+24,label,4)==0);
 uint8_t range[12]={0};memset(range+6,255,6);unsigned n=build(1,0x10,range,12,true);
 RDM_Receive(request,n,DMX512_Cycles());assert(tx_state==5);finish_tx();assert(tx_count==24 && tx_data[7]==0xAA);
 n=build(2,0x10,NULL,0,false);RDM_Receive(request,n,DMX512_Cycles());finish_tx();assert(muted);
 n=build(1,0x10,range,12,true);RDM_Receive(request,n,DMX512_Cycles());assert(!tx_state);
 n=build(3,0x10,NULL,0,true);RDM_Receive(request,n,DMX512_Cycles());assert(!muted && !tx_state);
 for(unsigned pid=PID_DMX_PORT_MODE;pid<=PID_RELAY_OFF_TIME;pid++) {
  uint8_t p[2];PutWord(p,pid);transact(0x0051,0x20,p,2);assert(tx_data[16]==0 && Word(tx_data+24)==pid);
  transact(pid,0x20,NULL,0);assert(tx_data[16]==0);
  uint8_t value[2];unsigned size=tx_data[23];memcpy(value,tx_data+24,size);
  transact(pid,0x30,value,size);assert(tx_data[16]==0);
  memset(value,255,size);transact(pid,0x30,value,size);assert(tx_data[16]==2 && Word(tx_data+24)==6);

 }
 /* PDL errata, richiesta tardiva, sub-device e broadcast: nessuna modifica indebita. */
 unsigned bad=build(0x00F0,0x30,NULL,0,false);RDM_Receive(request,bad,DMX512_Cycles());finish_tx();assert(tx_data[16]==2 && Word(tx_data+24)==1);
 bad=build(0x00F0,0x30,address,2,false);uint32_t ended=DMX512_Cycles();advance_us(2001);RDM_Receive(request,bad,ended);assert(!tx_state && dmx_address==496);
 bad=build(0x0060,0x20,NULL,0,false);request[19]=1;PutWord(request+24,0);uint16_t sum=0;for(unsigned i=0;i<24;i++)sum+=request[i];PutWord(request+24,sum);
 RDM_Receive(request,bad,DMX512_Cycles());finish_tx();assert(tx_data[16]==2 && Word(tx_data+24)==9);
 bad=build(0x0060,0x20,NULL,0,true);RDM_Receive(request,bad,DMX512_Cycles());assert(!tx_state);
 transact(0x0709,0x20,NULL,0);assert(tx_data[16]==2 && Word(tx_data+24)==5);
 uint8_t invalid=3;transact(PID_DMX_PORT_MODE,0x30,&invalid,1);assert(tx_data[16]==2 && dmx_port_mode==0);
 uint8_t net[9]={0,0,0,1,192,168,4,20,24};uint32_t old_ip=ip_address;transact(0x0706,0x30,net,9);assert(tx_data[16]==0 && ip_address==old_ip);
 transact(0x0706,0x20,net,4);assert(memcmp(tx_data+28,net+4,5)==0 && ip_address==old_ip);
 transact(0x0709,0x30,net,4);assert(tx_data[16]==0 && (ip_address&255)==192 && net_mask==0x00FFFFFF);
 transact(0x0700,0x20,NULL,0);assert(Word(tx_data+28)==1);
 transact(0x0705,0x20,net,4);assert(tx_data[23]==10 && tx_data[33]==0);
 uint8_t dhcp_off[5]={0,0,0,1,0};assert(use_dhcp==1);transact(0x0703,0x30,dhcp_off,5);assert(use_dhcp==1);
 transact(0x0703,0x20,dhcp_off,4);assert(tx_data[28]==0);
 transact(0x0709,0x30,dhcp_off,4);assert(use_dhcp==0);
 uint32_t before_ip=ip_address;net[8]=33;transact(0x0706,0x30,net,9);assert(tx_data[16]==2 && ip_address==before_ip);
 uint8_t dns[5]={0,8,8,4,4};transact(0x070B,0x30,dns,5);transact(0x070B,0x20,dns,1);assert(tx_data[16]==0 && memcmp(tx_data+24,dns,5)==0);
 uint8_t role=1;transact(PID_DMX_PORT_MODE,0x30,&role,1);assert(dmx_port_mode==1);DMX512_Service();assert(applied_port==1);
 dmx_address=1;InitRelay();art_levels[0]=201;art_levels[511]=99;advance_us(2100000);DMX512_Service();timer();assert(tx_state==1 && (coils&RDM_DIR_Pin));
 RelayTask();assert(active_proto==1 && (relay_target_mask&1));
 finish_tx();assert(tx_data[0]==0 && tx_data[1]==201 && tx_data[512]==99 && tx_count==513);
 art_valid=false;DMX512_Service();advance_us(30000);timer();finish_tx();assert(tx_data[1]==0 && tx_data[512]==0);
 assert(CFG_SetValue(CFG_DMX_PORT_MODE,2));DMX512_Service();sac_levels[0]=45;advance_us(2100000);DMX512_Service();timer();finish_tx();assert(tx_data[1]==45);RelayTask();assert(active_proto==2 && !(relay_target_mask&1));
 /* Main fermo: scaduta la cache, TX continua ma soltanto con zeri. */
 advance_us(100000);timer();finish_tx();assert(tx_data[1]==0);
 /* Messaggio RDM completo ricevuto nella finestra Node: stesso proprietario UART. */
 n=build(0x0060,0x20,NULL,0,false);receive(USART_ISR_FE,0);
 for(unsigned i=0;i<n;i++){advance_us(44);receive(USART_ISR_RXNE_RXFNE,request[i]);}
 assert(rdm_ready);DMX512_Service();assert(tx_state);finish_tx();assert(tx_data[0]==0xCC && controller_seen);
 timer();assert(tx_state==0); /* Controller presente: niente contesa TX autonoma. */
 /* RX di un checksum errato non produce risposta. */
 n=build(0x0060,0x20,NULL,0,false);request[n-1]^=1;receive(USART_ISR_FE,0);
 for(unsigned i=0;i<n;i++){advance_us(44);receive(USART_ISR_RXNE_RXFNE,request[i]);}
 DMX512_Service();assert(!tx_state);
 /* Ritorno IN senza reset; dati completi, nessun echo del proprio TX. */
 assert(CFG_SetValue(CFG_DMX_PORT_MODE,0));DMX512_Service();assert(applied_port==0 && !(coils&RDM_DIR_Pin));
 receive(USART_ISR_FE,0);receive(8,0);receive(8,222);receive(8,12);receive(USART_ISR_FE,0);DMX512_Service();assert(dmxRxCount==2 && dmxRxBuffer[0]==222 && dmxRxBuffer[2]==0);
 advance_us(300000);RDM_Task();assert(!RDM_SavePending());assert(EE_LoadConfig()==HAL_OK && dmx_address==1);
 /* Migrazione v2 -> v3: ignora il precedente byte 0x27, conserva identità e dmx_mode. */
 dmx_mode=3;assert(EE_SaveConfig()==HAL_OK);uint8_t saved[256];memcpy(saved,memory,256);saved[2]=2;saved[0x27]=0xB2;
 for(int cut=0;cut<30;cut++) {
  memcpy(memory,saved,256);fail_at=cut;io_count=0;(void)EE_LoadConfig();unsigned ops=io_count;fail_at=-1;
  assert(EE_LoadConfig()==HAL_OK && dmx_port_mode==0 && dmx_mode==3);
  for(unsigned i=0;i<128;i++)if(i!=2 && i!=0x27)assert(memory[i]==saved[i]);
  unsigned count=writes;assert(EE_LoadConfig()==HAL_OK && writes==count);if(ops<=(unsigned)cut)break;
 }
 assert(CFG_SetValue(CFG_DMX_PORT_MODE,2));assert(EE_SaveConfig()==HAL_OK);dmx_port_mode=0;assert(EE_LoadConfig()==HAL_OK && dmx_port_mode==2);
 memory[0x27]=0xB3;assert(EE_LoadConfig()==HAL_ERROR);memory[0x27]=0xB0;
 /* TX guasto non lascia il driver abilitato senza limite. */
 dmx_port_mode=1;DMX512_Service();advance_us(2100000);DMX512_Service();timer();timer();timer();assert(tx_state==3);
 timer();assert(tx_state==0 && !(coils&RDM_DIR_Pin) && tx_faults);
 /* Un EUI non nel dominio noto disabilita soltanto RDM, mai UID di fallback. */
 char diagnostic[64];RDM_FormatDiagnostics(diagnostic,sizeof diagnostic);assert(strstr(diagnostic,"rdm=1 uid=17FA:A3010203") && late_requests==1);
 memory[0xFA]=0xFF;RDM_Init();assert(!RDM_Enabled());RDM_FormatDiagnostics(diagnostic,sizeof diagnostic);assert(strstr(diagnostic,"rdm=0 uid=0000:00000000 rx=0 late=0"));return 0;
}
"""

def run():
    with tempfile.TemporaryDirectory(prefix='adsw100-tests-') as folder:
        tmp = Path(folder)
        (tmp / 'h_files.h').write_text(COMMON)
        for name in ['stm32h7xx_hal.h', 'main.h', 'ethernetif.h', 'usb_device.h', 'usbd_cdc_if.h', 'usbd_cdc.h']:
            (tmp / name).write_text('')
        for name in ['udp.h', 'pbuf.h', 'netif.h', 'ip4_addr.h', 'ip_addr.h', 'igmp.h', 'dhcp.h']:
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
                 ('BRAND_EE_TEST', 'eeprom.c', BRAND_EE), ('UI_TEST', 'eeprom.c', UI), ('CDC_TEST', 'usb_debug.c', CDC), ('NODE_RDM_TEST', 'dmx_512.c', NODE_RDM)]
        for define, filename, body in cases:
            harness = tmp / 'test.c'
            identity = f'#include "{ROOT / "Core/Src/cfg_io.c"}"\n' if filename in ('artnet.c', 'eeprom.c', 'usb_debug.c') or define == 'NODE_RDM_TEST' else ''
            ui = ui_functions() if define == 'UI_TEST' else ''
            if define == 'NODE_RDM_TEST':
                identity += f'#include "{ROOT / "Core/Src/eeprom.c"}"\n#include "{ROOT / "Core/Src/rdm.c"}"\n#include "{ROOT / "Core/Src/relay_driver.c"}"\n'
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
