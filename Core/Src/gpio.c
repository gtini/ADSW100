/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins
     PH0-OSC_IN (PH0)   ------> RCC_OSC_IN
     PH1-OSC_OUT (PH1)   ------> RCC_OSC_OUT
     PA13 (JTMS/SWDIO)   ------> DEBUG_JTMS-SWDIO
     PA14 (JTCK/SWCLK)   ------> DEBUG_JTCK-SWCLK
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, ETH_LED_Pin|RL_ENABLE_Pin|STAT_LED_Pin|CH01_ON_Pin
                          |CH01_OFF_Pin|DMX_LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, CH08_ON_Pin|CH08_OFF_Pin|CH07_OFF_Pin|CH09_ON_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(ETH_RST_GPIO_Port, ETH_RST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, RDM_DIR_Pin|CH02_ON_Pin|CH02_OFF_Pin|CH03_ON_Pin
                          |CH03_OFF_Pin|CH04_ON_Pin|CH04_OFF_Pin|CH05_ON_Pin
                          |CH11_ON_Pin|CH11_OFF_Pin|CH12_ON_Pin|CH12_OFF_Pin
                          |LCD_RST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, CH05_OFF_Pin|CH06_ON_Pin|CH06_OFF_Pin|CH07_ON_Pin
                          |CH09_OFF_Pin|CH10_ON_Pin|CH10_OFF_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LCD_CS0_Pin|LCD_CD_Pin|LCD_BL_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : ETH_LED_Pin RL_ENABLE_Pin STAT_LED_Pin CH01_ON_Pin
                           CH01_OFF_Pin DMX_LED_Pin */
  GPIO_InitStruct.Pin = ETH_LED_Pin|RL_ENABLE_Pin|STAT_LED_Pin|CH01_ON_Pin
                          |CH01_OFF_Pin|DMX_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : IN_01_Pin IN_02_Pin IN_03_Pin */
  GPIO_InitStruct.Pin = IN_01_Pin|IN_02_Pin|IN_03_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : CH08_ON_Pin CH08_OFF_Pin CH07_OFF_Pin CH09_ON_Pin */
  GPIO_InitStruct.Pin = CH08_ON_Pin|CH08_OFF_Pin|CH07_OFF_Pin|CH09_ON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : ETH_RST_Pin LCD_CS0_Pin LCD_CD_Pin LCD_BL_Pin */
  GPIO_InitStruct.Pin = ETH_RST_Pin|LCD_CS0_Pin|LCD_CD_Pin|LCD_BL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : ETH_INT_Pin ENC_A_Pin */
  GPIO_InitStruct.Pin = ETH_INT_Pin|ENC_A_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : RDM_DIR_Pin CH02_ON_Pin CH02_OFF_Pin CH03_ON_Pin
                           CH03_OFF_Pin CH04_ON_Pin CH04_OFF_Pin CH05_ON_Pin
                           CH11_ON_Pin CH11_OFF_Pin CH12_ON_Pin CH12_OFF_Pin
                           LCD_RST_Pin */
  GPIO_InitStruct.Pin = RDM_DIR_Pin|CH02_ON_Pin|CH02_OFF_Pin|CH03_ON_Pin
                          |CH03_OFF_Pin|CH04_ON_Pin|CH04_OFF_Pin|CH05_ON_Pin
                          |CH11_ON_Pin|CH11_OFF_Pin|CH12_ON_Pin|CH12_OFF_Pin
                          |LCD_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : CH05_OFF_Pin CH06_ON_Pin CH06_OFF_Pin CH07_ON_Pin
                           CH09_OFF_Pin CH10_ON_Pin CH10_OFF_Pin */
  GPIO_InitStruct.Pin = CH05_OFF_Pin|CH06_ON_Pin|CH06_OFF_Pin|CH07_ON_Pin
                          |CH09_OFF_Pin|CH10_ON_Pin|CH10_OFF_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : ENC_B_Pin */
  GPIO_InitStruct.Pin = ENC_B_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ENC_B_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ENC_P_Pin */
  GPIO_InitStruct.Pin = ENC_P_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(ENC_P_GPIO_Port, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
