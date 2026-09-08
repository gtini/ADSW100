/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define ETH_LED_Pin GPIO_PIN_2
#define ETH_LED_GPIO_Port GPIOE
#define RL_ENABLE_Pin GPIO_PIN_4
#define RL_ENABLE_GPIO_Port GPIOE
#define IN_01_Pin GPIO_PIN_5
#define IN_01_GPIO_Port GPIOE
#define IN_02_Pin GPIO_PIN_6
#define IN_02_GPIO_Port GPIOE
#define CH08_ON_Pin GPIO_PIN_3
#define CH08_ON_GPIO_Port GPIOA
#define CH08_OFF_Pin GPIO_PIN_4
#define CH08_OFF_GPIO_Port GPIOA
#define LCD_SCK_Pin GPIO_PIN_5
#define LCD_SCK_GPIO_Port GPIOA
#define ETH_RST_Pin GPIO_PIN_0
#define ETH_RST_GPIO_Port GPIOB
#define ETH_INT_Pin GPIO_PIN_1
#define ETH_INT_GPIO_Port GPIOB
#define IN_03_Pin GPIO_PIN_7
#define IN_03_GPIO_Port GPIOE
#define STAT_LED_Pin GPIO_PIN_10
#define STAT_LED_GPIO_Port GPIOE
#define CH01_ON_Pin GPIO_PIN_14
#define CH01_ON_GPIO_Port GPIOE
#define CH01_OFF_Pin GPIO_PIN_15
#define CH01_OFF_GPIO_Port GPIOE
#define RDM_TXD_Pin GPIO_PIN_14
#define RDM_TXD_GPIO_Port GPIOB
#define RDM_RXD_Pin GPIO_PIN_15
#define RDM_RXD_GPIO_Port GPIOB
#define RDM_DIR_Pin GPIO_PIN_8
#define RDM_DIR_GPIO_Port GPIOD
#define CH02_ON_Pin GPIO_PIN_9
#define CH02_ON_GPIO_Port GPIOD
#define CH02_OFF_Pin GPIO_PIN_10
#define CH02_OFF_GPIO_Port GPIOD
#define CH03_ON_Pin GPIO_PIN_11
#define CH03_ON_GPIO_Port GPIOD
#define CH03_OFF_Pin GPIO_PIN_12
#define CH03_OFF_GPIO_Port GPIOD
#define CH04_ON_Pin GPIO_PIN_13
#define CH04_ON_GPIO_Port GPIOD
#define CH04_OFF_Pin GPIO_PIN_14
#define CH04_OFF_GPIO_Port GPIOD
#define CH05_ON_Pin GPIO_PIN_15
#define CH05_ON_GPIO_Port GPIOD
#define CH05_OFF_Pin GPIO_PIN_6
#define CH05_OFF_GPIO_Port GPIOC
#define CH06_ON_Pin GPIO_PIN_7
#define CH06_ON_GPIO_Port GPIOC
#define CH06_OFF_Pin GPIO_PIN_8
#define CH06_OFF_GPIO_Port GPIOC
#define CH07_ON_Pin GPIO_PIN_9
#define CH07_ON_GPIO_Port GPIOC
#define CH07_OFF_Pin GPIO_PIN_8
#define CH07_OFF_GPIO_Port GPIOA
#define CH09_ON_Pin GPIO_PIN_15
#define CH09_ON_GPIO_Port GPIOA
#define CH09_OFF_Pin GPIO_PIN_10
#define CH09_OFF_GPIO_Port GPIOC
#define CH10_ON_Pin GPIO_PIN_11
#define CH10_ON_GPIO_Port GPIOC
#define CH10_OFF_Pin GPIO_PIN_12
#define CH10_OFF_GPIO_Port GPIOC
#define CH11_ON_Pin GPIO_PIN_0
#define CH11_ON_GPIO_Port GPIOD
#define CH11_OFF_Pin GPIO_PIN_1
#define CH11_OFF_GPIO_Port GPIOD
#define CH12_ON_Pin GPIO_PIN_2
#define CH12_ON_GPIO_Port GPIOD
#define CH12_OFF_Pin GPIO_PIN_3
#define CH12_OFF_GPIO_Port GPIOD
#define LCD_RST_Pin GPIO_PIN_4
#define LCD_RST_GPIO_Port GPIOD
#define LCD_CS0_Pin GPIO_PIN_3
#define LCD_CS0_GPIO_Port GPIOB
#define LCD_CD_Pin GPIO_PIN_4
#define LCD_CD_GPIO_Port GPIOB
#define LCD_BL_Pin GPIO_PIN_5
#define LCD_BL_GPIO_Port GPIOB
#define ENC_A_Pin GPIO_PIN_8
#define ENC_A_GPIO_Port GPIOB
#define ENC_B_Pin GPIO_PIN_9
#define ENC_B_GPIO_Port GPIOB
#define ENC_P_Pin GPIO_PIN_0
#define ENC_P_GPIO_Port GPIOE
#define DMX_LED_Pin GPIO_PIN_1
#define DMX_LED_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
