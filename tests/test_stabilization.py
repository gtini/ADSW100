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
#define I2C_MEMADD_SIZE_8BIT 1
static void HAL_Delay(unsigned n) {(void)n;}
static HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h,unsigned a,unsigned t,unsigned ms) {(void)h;(void)a;(void)t;(void)ms;return HAL_OK;}
static HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h,unsigned a,unsigned m,unsigned sz,uint8_t *b,unsigned n,unsigned ms) {(void)h;(void)a;(void)sz;(void)ms;if(fail_read)return HAL_ERROR;memcpy(b,memory+m,n);return HAL_OK;}
static HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h,unsigned a,unsigned m,unsigned sz,uint8_t *b,unsigned n,unsigned ms) {(void)h;(void)a;(void)sz;(void)ms;if(fail_write)return HAL_ERROR;assert(m+n<=128);memcpy(memory+m,b,n);writes++;return HAL_OK;}
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
 memset(memory,255,sizeof memory);assert(EE_InitMemory()==HAL_OK);assert(memory[2]==255 && memory[0x1B]==255);
 assert(EE_LoadConfig()==HAL_OK && dmx_address==1 && dmx_mode==2);unsigned count=writes;
 assert(EE_LoadConfig()==HAL_OK && writes==count);
 uint8_t v=0;assert(EE_Write(128,&v,1)==HAL_ERROR && EE_Write(127,&v,2)==HAL_ERROR && EE_Write(0xFA,&v,1)==HAL_ERROR && writes==count);
 fail_read=true;dmx_address=100;assert(EE_LoadConfig()==HAL_ERROR && dmx_address==100);fail_read=false;
 fail_write=true;dmx_address=200;assert(EE_SaveConfig()!=HAL_OK);fail_write=false;
 assert(EE_SaveConfig()==HAL_OK);dmx_address=1;assert(EE_LoadConfig()==HAL_OK && dmx_address==200);
 assert(memory[2]==255 && memory[0x1B]==255 && memory[0xFA]==255);return 0;
}
'''

def run():
    with tempfile.TemporaryDirectory(prefix='adsw100-tests-') as folder:
        tmp = Path(folder)
        (tmp / 'h_files.h').write_text(COMMON)
        for name in ['stm32h7xx_hal.h', 'main.h', 'ethernetif.h']:
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
                 ('DMX_TEST', 'dmx_512.c', DMX), ('EE_TEST', 'eeprom.c', EE)]
        for define, filename, body in cases:
            harness = tmp / 'test.c'
            harness.write_text(f'#define {define}\n#include "h_files.h"\n#include "{ROOT / "Core/Src" / filename}"\n' + body)
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
