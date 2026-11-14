/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "iwdg.h"
#include "lwip.h"
#include "rtc.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "defs.h"
#include "h_files.h"
#include "lwip/ip4_addr.h"
#include "stm32h7xx_hal_eth.h"

#include <stdio.h>

#ifndef ETH_MAC_ADDRESS0
#define ETH_MAC_ADDRESS0 0U
#endif

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile bool timer_1ms_flag = false;

extern ETH_HandleTypeDef heth;			// handle ETH generato da Cube

extern volatile uint8_t g_use_dhcp;		// definita in network.c

extern struct netif gnetif;

static uint32_t lcd_inactivity_s = 0;

extern ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT];
extern u8_t memp_memory_RX_POOL_base[];


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void MPU_Config(void)
{
    MPU_Region_InitTypeDef MPU_InitStruct = {0};

    /* Disabilita MPU prima di configurare */
    HAL_MPU_Disable();

    /* --- REGIONE 0: D1 SRAM (0x24000000, 512KB) - cacheable -------------------- */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
    MPU_InitStruct.BaseAddress      = 0x24000000;
    MPU_InitStruct.Size             = MPU_REGION_SIZE_512KB;
    MPU_InitStruct.SubRegionDisable = 0x00;
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;   // Normal memory
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* --- REGIONE 1: D2 SRAM (0x30000000, 256KB) - NON CACHEABLE (ETH/DMA) ------ */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER1;
    MPU_InitStruct.BaseAddress      = 0x30000000;
    MPU_InitStruct.Size             = MPU_REGION_SIZE_256KB;  // SRAM1+SRAM2
    MPU_InitStruct.SubRegionDisable = 0x00;
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;   // Normal memory
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* --- REGIONE 2: D3 SRAM (0x38000000, 64KB) - NON CACHEABLE (.noinit, ecc.) -- */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER2;
    MPU_InitStruct.BaseAddress      = 0x38000000;
    MPU_InitStruct.Size             = MPU_REGION_SIZE_64KB;   // D3 SRAM = 64KB
    MPU_InitStruct.SubRegionDisable = 0x00;
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;   // Normal memory
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* Abilita MPU con default privilegiato */
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

const char *ResetTag_ToString(reset_tag_t tag)
{
    switch (tag) {
    case RST_TAG_NONE:       return "None";
    case RST_TAG_HARDFAULT:  return "HardFault";
    case RST_TAG_MEMFAULT:   return "MemFault";
    case RST_TAG_BUSFAULT:   return "BusFault";
    case RST_TAG_USAGEFAULT: return "UsageFault";
    case RST_TAG_HFR:        return "HardFactoryReset";
    case RST_TAG_ERRORH:     return "Error_Handler";
    default:                 return "Unknown";
    }
}

reset_tag_t ResetTag_Read(void)
{
    uint32_t raw = RTC->BKP0R;
    if (raw > RST_TAG_ERRORH) return RST_TAG_NONE;
    return (reset_tag_t)raw;
}

void ResetTag_Write(reset_tag_t tag)
{
    RTC->BKP0R = (uint32_t)tag;
    __DSB();
    __ISB();
}

reset_crash_info_t ResetTag_ReadCrashInfo(void)
{
    reset_crash_info_t info;
    info.pc = RTC->BKP1R;   // PC salvato
    info.lr = RTC->BKP2R;   // LR salvato
    return info;
}

void ResetTag_ClearCrashInfo(void)
{
    RTC->BKP1R = 0;
    RTC->BKP2R = 0;
    __DSB();
    __ISB();
}

void ResetTag_SaveCrashInfo(uint32_t pc, uint32_t lr)
{
    RTC->BKP1R = pc;
    RTC->BKP2R = lr;
    __DSB();
    __ISB();
}

static int is_plausible_eui48(const uint8_t *m)
{
    uint8_t acc_or = 0, acc_and = 0xFF;

    for (int i=0; i<6; i++)
    {
    	acc_or |= m[i];
    	acc_and &= m[i];
    }

    if (acc_or == 0x00 || acc_and == 0xFF) return 0;  // tutti 00 o tutti FF
    if (m[0] & 0x01) return 0;                        // deve essere unicast
    return 1;
}

static int is_plausible_eui64(const uint8_t *v)
{
    uint8_t acc_or = 0, acc_and = 0xFF;

    for (int i=0;i<8;i++)
    {
    	acc_or |= v[i];
    	acc_and &= v[i];
    }

    if (acc_or == 0x00 || acc_and == 0xFF) return 0;  // tutti 00 o tutti FF
    return 1; // accetta sia canonical (…FF:FE…) sia non-canonical
}

void hardware_check(void)
{
	uint8_t e48[6] = {0};
	uint8_t e64[8] = {0};
	uint8_t mac[6];

	USB_Printf("\r\n=== Hardware Check ===\r\n");

    EE_BusScan();     // mostra i device sul bus
    EE_DumpTail();    // dump 0xF0..0xFF per diagnosi rapida

    // 1) Ping EEPROM I2C (indirizzo 7-bit definito in EE_I2C_ADDR_7B)

    if (HAL_I2C_IsDeviceReady(&hi2c1, EE_I2C_ADDR_7B << 1, 2, 10) != HAL_OK)
    {
        USB_Printf("EEPROM: NOT FOUND at 0x%02X\n", EE_I2C_ADDR_7B);
        USB_Printf("=== End HW Check ===\n");
        return;
    }
    USB_Printf("EEPROM: found at 0x%02X\n", EE_I2C_ADDR_7B);

    // 2) Rilevamento EUI: prima E48 (0xFA..0xFF), poi E64 (0xF8..0xFF)

    int have_e48 = (EE_Read(0xFA, e48, 6) == HAL_OK) && is_plausible_eui48(e48);
    int have_e64 = (EE_Read(0xF8, e64, 8) == HAL_OK) && is_plausible_eui64(e64);

    if (have_e48)
    {
        USB_Printf("EEPROM 24AA025E48: OK (EUI-48 @0xFA)\n");
        USB_Printf("EUI-48: %02X-%02X-%02X-%02X-%02X-%02X\n",
                   e48[0],e48[1],e48[2],e48[3],e48[4],e48[5]);
    }
    else if (have_e64)
    {
        USB_Printf("EEPROM 24AA02E64: OK (EUI-64 @0xF8)\n");
        USB_Printf("EUI-64: %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n",
                   e64[0],e64[1],e64[2],e64[3],e64[4],e64[5],e64[6],e64[7]);
    }
    else
    {
        USB_Printf("EEPROM: no EUI found (neither E48 nor E64 plausible)\n");
    }

    // 3) MAC-48 locale per la NIC (preferisci E48; se assente, E64 derivato; altrimenti LAA)

    if (EE_GetLocalMAC48(mac) == HAL_OK)
    {
        USB_Printf("MAC-48 (source: %s): %02X:%02X:%02X:%02X:%02X:%02X\n",
                   have_e48 ? "E48" : (have_e64 ? "E64-derived" : "LAA"),
                   mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    }
    else
    {
        USB_Printf("MAC-48: ERROR (no valid source)\n");
    }

    // 4) Config EEPROM

    if (EE_LoadConfig() == HAL_OK)
    {
        USB_Printf("Configuration loaded\n");
    }
    else
    {
        USB_Printf("EE_LoadConfig FAILED\n");
    }

    // NFC test

    NFC_I2C_DumpFirstBlocks(&hi2c1);
    NFC_Test();

    LAN9303_DumpLedCfg();

    USB_Printf("=== End HW Check ===\n");
}

void LCD_Backlight_Wake(void)
{
    lcd_inactivity_s = 0;
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);
}

void LCD_Backlight_Service(void)
{
    // Se modalità "sempre accesa"
    if (lcd_backlight > 0)
    {
        HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);
        lcd_inactivity_s = 0;
        return;
    }

    // Modalità auto-off
    if (lcd_inactivity_s >= 30)
    {
        HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_RESET); // spegni
    }
    else
    {
        HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);   // accesa
        lcd_inactivity_s++;
    }
}

void task_1ms(void)
{
	static uint16_t counter_10ms = 0, link_tick_100ms = 0, counter_3s = 0, counter_1s;
	static uint16_t iwdg_div = 0;


	HAL_GPIO_TogglePin(RL_ENABLE_GPIO_Port, RL_ENABLE_Pin);  // Toggle pin watchdog 74LVC1G123

	RelayTask();
	EncoderTask();
	NetworkTask();

    if (++counter_10ms >= 10)
    {
    	counter_10ms = 0;
    	UserInterfaceTask();

    }

	if (++link_tick_100ms >= 100)
	{
		link_tick_100ms = 0;
		Network_LinkCheck_100ms();
	}

    if (++counter_1s >= 1000)
    {
        counter_1s = 0;

        //RTC_ShowTime_OnLCD();

        //NFC_Service();

        LCD_Backlight_Service();

        // --- Stato rete corrente: SOLO da LAN9303 (PHY), non da netif ---
        extern int32_t LAN9303_GetLinkState(void);

        int32_t st = LAN9303_GetLinkState();

        bool link_up = false;

        switch (st)
        {
            case USER_PHY_STATUS_100MBITS_FULLDUPLEX:
            case USER_PHY_STATUS_100MBITS_HALFDUPLEX:
            case USER_PHY_STATUS_10MBITS_FULLDUPLEX:
            case USER_PHY_STATUS_10MBITS_HALFDUPLEX:
                link_up = true;
                break;

            case USER_PHY_STATUS_LINK_DOWN:
            case USER_PHY_STATUS_AUTONEGO_NOTDONE:
            case USER_PHY_STATUS_READ_ERROR:
            case USER_PHY_STATUS_WRITE_ERROR:
            case USER_PHY_STATUS_ADDRESS_ERROR:
            case USER_PHY_STATUS_RESET_TIMEOUT:
            case USER_PHY_STATUS_ERROR:
            default:
                link_up = false;
                break;
        }

        uint32_t ip_be = netif_ip4_addr(&gnetif)->addr;   // formato big-endian

        // Cache per evitare ridisegni inutili
        static bool     prev_link = false;
        static uint32_t prev_ip   = 0;

        if (link_up != prev_link || ip_be != prev_ip)
        {
            UI_OnNetRuntime(link_up, ip_be);
            prev_link = link_up;
            prev_ip   = ip_be;
        }

        Network_DiagTick_1s();
    }

	if (++counter_3s >= 3000)
	{
		counter_3s = 0;
		LAN9303_Port0RxErrorsDelta();
	}
	// ===== Refresh IWDG ogni ~100ms =====
	    if (++iwdg_div >= 100)    // 100 chiamate * 1ms = 100ms
	    {
	        iwdg_div = 0;
	        HAL_IWDG_Refresh(&hiwdg1);
	    }
}

// Inizializza il sistema
// Reset totale: EEPROM ai default, NTAG pulito + MAGIC/JSON riscritto.
void HardFactoryReset(void)
{
    // 1) EEPROM ai default
    if (EE_InitMemory() == HAL_OK)
        USB_Printf("HFR: EEPROM defaults OK\r\n");
    else
    {
        USB_Printf("HFR: EE_InitMemory() FAILED\r\n");
        return;
    }

    // 2) Ricarica i default in RAM soltanto dopo lettura verificata.
    if (EE_LoadConfig() != HAL_OK)
    {
        USB_Printf("HFR: EEPROM reload FAILED\r\n");
        return;
    }

    // 3) NFC reset + riscrittura MAGIC/JSON
    if (NFC_I2C_InitQuickReaddress(&hi2c1))
    {
        bool ok_n = NFC_InitMemory(&hi2c1);
        USB_Printf("HFR: NTAG init %s\r\n", ok_n ? "OK" : "FAIL");

        uint8_t mac[6];
        EE_GetLocalMAC48(mac);
        char magic[64];
        build_magic_from_eui(magic, sizeof(magic), mac);

        uint8_t dm, net, su; uint16_t uni, pr; bool dh;
        char ip[16], mk[16], gw[16];
        get_runtime_vars(&dm, &uni, &net, &su, &pr, &dh, ip, mk, gw);

        char json[768];
        if (cfg_to_json_vars(dm, uni, net, su, pr, dh, ip, mk, gw, magic, json, sizeof json)
            && ndef_write_mime_json(&hi2c1, json))
        {
            USB_Printf("HFR: NTAG written (MIME+JSON+MAGIC)\r\n");
        }
        else USB_Printf("HFR: NTAG write FAILED\r\n");
    }
    else {
        USB_Printf("HFR: no NTAG on I2C -> skipping NFC reset\r\n");
    }

    USB_Printf("HFR: completed, system will reboot now...\r\n");
    HAL_Delay(100);

    ResetTag_Write(RST_TAG_HFR);

    HAL_Delay(10);

    NVIC_SystemReset();
}

/* ========== Unlock nuovo dispositivo (FGM+MAC) ========== */

static void mac_to_hex_nosep(const uint8_t mac[6], char out[13])
{
  static const char H[] = "0123456789ABCDEF";
  for (int i=0; i<6; i++)
  {
    out[i*2+0] = H[(mac[i]>>4) & 0xF];
    out[i*2+1] = H[ mac[i]     & 0xF];
  }
  out[12] = 0;
}

/* Ritorna 1 = ok (prosegui boot), 0 = bloccato */
static int Startup_SimpleUnlock(void)
{
  /* Se già inizializzata, niente attesa */
  if (EE_ReadU16(EE_OFFS_INIT) == EE_INIT_VALUE)
  {
    return 1;
  }

  /* calcola stringa attesa: "FGM" + MAChex */
  uint8_t mac[6]; EE_GetLocalMAC48(mac);
  char mac_hex[13]; mac_to_hex_nosep(mac, mac_hex);

  char expected[4+12]; /* "FGM" + 12 */
  expected[0]='F'; expected[1]='G'; expected[2]='M'; expected[3]=0;
  strcat(expected, mac_hex);

  /* UX: messaggi */
  USB_Printf("\r\n=== Bootloader Ready ===\r\n");
  USB_Printf("wait for boot sequence:\r\n");
  USB_Printf("  %s\r\n", mac_hex);

  LCD_Clear();
  LCD_DrawText("AXN Bootloader", 10, 0, 0, 0, 0);
  LCD_DrawText("Wait for boot", 0, 20, 0, 0, 0);

  /* aspetta una riga per 30s */
  extern int USB_ReadLineTimeout(char *out, size_t maxlen, uint32_t timeout_ms);
  char line[128];
  uint32_t t0 = HAL_GetTick();
  while ((HAL_GetTick() - t0) < 30000U)
  {
    HAL_IWDG_Refresh(&hiwdg1);

    int n = USB_ReadLineTimeout(line, sizeof(line), 50);
    if (n > 0)
    {
      while (n>0 && (line[n-1]==' ')) { line[--n]=0; }
      char *p = line; while (*p==' ') p++;

      if (strcmp(p, expected) == 0)
      {
        HardFactoryReset();
        USB_Printf("Unlock FAILED: factory reset returned without reboot.\r\n");
        return 0;
      }
      else
      {
        USB_Printf("Bad token");
      }
    }
  }

  /* timeout -> fermo in attesa firmware */
  USB_Printf("Ready for firmware\r\n");
  while (1)
  {
	  HAL_IWDG_Refresh(&hiwdg1);
    /* aggiungere qui eventuale bootloader/DFU */
  }
  /* never */
  // return 0;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6)
    {
        // Tick applicativo a 1ms
        Relay_PulseService1ms();
        timer_1ms_flag = true;
    }
}

void LogAndClearResetFlags(void)
{
    uint32_t r = RCC->RSR;

    int iwdg = (r & RCC_RSR_IWDG1RSTF) ? 1 : 0;
    int wwdg = (r & RCC_RSR_WWDG1RSTF) ? 1 : 0;
    int pin  = (r & RCC_RSR_PINRSTF)   ? 1 : 0;
    int bor  = (r & RCC_RSR_BORRSTF)   ? 1 : 0;
    int sft  = (r & RCC_RSR_SFTRSTF)   ? 1 : 0;
    int lpwr = (r & RCC_RSR_LPWRRSTF)  ? 1 : 0;

    USB_Printf("Reset flags: IWDG=%d WWDG=%d PIN=%d BOR=%d SFT=%d LPWR=%d\r\n",
               iwdg, wwdg, pin, bor, sft, lpwr);

    __HAL_RCC_CLEAR_RESET_FLAGS();
}

/*
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{


}
*/

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

	MPU_Config();
	SCB_EnableICache();
	SCB_EnableDCache();

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */


  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USB_DEVICE_Init();
  MX_TIM6_Init();
  MX_IWDG1_Init();
  MX_LWIP_Init();
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */

  HAL_TIM_Base_Start_IT(&htim6); // abilita interrupt timer 6 (1mS)

  uint32_t t0 = HAL_GetTick();
  while ((HAL_GetTick() - t0) < 1000)
  {
	  HAL_IWDG_Refresh(&hiwdg1);
  }

  reset_tag_t last_tag = ResetTag_Read();

  USB_Printf("\r\n*** BOOT START ***\r\n");

  USB_Printf("Last reset tag: %d -> %s\r\n",
             (int)last_tag, ResetTag_ToString(last_tag));

  USB_Printf("DMARxDscrTab @ %08lX\r\n", (unsigned)&DMARxDscrTab[0]);
  USB_Printf("RX_POOL base @ %08lX\r\n", (unsigned)memp_memory_RX_POOL_base);

  LogAndClearResetFlags();

  ResetTag_Write(RST_TAG_NONE);

  if (last_tag == RST_TAG_HARDFAULT ||
      last_tag == RST_TAG_MEMFAULT  ||
      last_tag == RST_TAG_BUSFAULT  ||
      last_tag == RST_TAG_USAGEFAULT)
  {
      reset_crash_info_t ci = ResetTag_ReadCrashInfo();
      USB_Printf("Crash context: PC=%08lX LR=%08lX\r\n", (unsigned)ci.pc, (unsigned)ci.lr);
      ResetTag_ClearCrashInfo();
  }
  else
  {
      ResetTag_ClearCrashInfo();
  }

  LCD_Init();          // inizializza HW
  LCD_Clear();         // pulisci RAM
  RTC_I2C_Init();

  InitDMX512(&huart1);
  InitRelay();
  InitUserInterface();

  // Diagnostica EEPROM / MAC / cfg
  hardware_check();

  // Verifica NFC
  NFC_Service();


  // Cancella locazione Init per test !!!

  //EE_WriteU16(EE_OFFS_INIT, 0)
  //while(1){HAL_IWDG_Refresh(&hiwdg1);}


  // Esegue check all'avvio per protezione firmware

  if (!Startup_SimpleUnlock())
  {
	while(1)
	{
		HAL_IWDG_Refresh(&hiwdg1);
	}
  }

  // Post-LwIP (callback, DNS, ecc.)
  Network_InitAfterLwip();

  ETH_PrintMemMap(); // invia per debug descrittori e RX pool


  USB_Printf("STM32 USB Debug ready!");

  USB_Printf("System ready!");

  HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  while (1)
  {
	  if (timer_1ms_flag)
	  {
		  timer_1ms_flag = false;
		  DMX512_Service();
		  task_1ms();
	  }

	  MX_LWIP_Process();   // gestisce TCP/IP, DHCP, ecc.
	  NET_PollApply();     // esegue apply sACN "in contesto lwIP"

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_LSI
                              |RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 15;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */

	__disable_irq();
	ResetTag_Write(RST_TAG_ERRORH);
	NVIC_SystemReset();

	while (1) { }


  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
