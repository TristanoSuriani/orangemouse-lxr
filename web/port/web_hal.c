/*
 * web_hal.c
 *
 * The parts of the STM32 HAL (and of main.c / stm32f7xx_it.c) that the
 * firmware modules use, for the WebAssembly build. Peripherals have no
 * hardware behind them: register accesses go to the arrays declared in
 * lxr_web_port.h, and the few HAL calls are stubs or talk to the port
 * (front panel shift registers, random numbers, time).
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */
#include "main.h"
#include "web_port.h"
#include <string.h>

uint8_t web_periph[WEB_PERIPH_SIZE] __attribute__((aligned(16)));
uint8_t web_periph_ahb2[WEB_PERIPH_AHB2_SIZE] __attribute__((aligned(16)));
uint8_t web_periph_core[WEB_PERIPH_CORE_SIZE] __attribute__((aligned(16)));

// ---- peripheral handles normally defined by main.c --------------------------
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
I2S_HandleTypeDef hi2s2;
I2S_HandleTypeDef hi2s3;
DMA_HandleTypeDef hdma_spi2_tx;
DMA_HandleTypeDef hdma_spi3_tx;
RNG_HandleTypeDef hrng;
SD_HandleTypeDef hsd1;
DMA_HandleTypeDef hdma_sdmmc1_rx;
DMA_HandleTypeDef hdma_sdmmc1_tx;
SPI_HandleTypeDef hspi1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim14;
UART_HandleTypeDef huart3;
PCD_HandleTypeDef hpcd_USB_OTG_FS;

float debugWatch = 0;

// normally in stm32f7xx_it.c: incremented by the 4 kHz TIM14 interrupt
INDTCMZ volatile uint32_t systick_ticks;

// ---- time -----------------------------------------------------------------
static volatile uint32_t web_halTick;  // 1 kHz, like the HAL SysTick
static uint32_t web_cycleCount;

void web_hal_incTick(void)
{
	web_halTick++;
}

uint32_t HAL_GetTick(void)
{
	return web_halTick;
}

// There is no concurrency to wait for: the ticks only advance between calls
// into the firmware, so a delay would never end. Skip it.
void HAL_Delay(uint32_t Delay)
{
	(void)Delay;
}

uint32_t HAL_RCC_GetHCLKFreq(void)
{
	return 216000000u;
}

uint32_t HAL_RCC_GetPCLK1Freq(void)
{
	return 54000000u;
}

DWT_Type *web_dwt_access(void)
{
	DWT_Type *dwt = (DWT_Type *)(uintptr_t)DWT_BASE;
	// ~1 us per access at 216 MHz: busy waits finish quickly
	web_cycleCount += 216u;
	dwt->CYCCNT = web_cycleCount;
	return dwt;
}

void Error_Handler(void)
{
	web_log("Error_Handler called");
}

// ---- timers ----------------------------------------------------------------
// TIM2 renders audio blocks in an interrupt while the main loop is blocked on
// the SD card; the port renders blocks itself (see web_main.c).
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim)
{
	(void)htim;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim)
{
	(void)htim;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *htim)
{
	(void)htim;
	return HAL_OK;
}

// ---- random numbers ---------------------------------------------------------
static uint32_t web_rngState = 0x2545F491u;

uint32_t web_rng_next(void)
{
	// xorshift32
	uint32_t x = web_rngState;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	web_rngState = x;
	return x;
}

void web_rng_seed(uint32_t seed)
{
	web_rngState = seed ? seed : 0x2545F491u;
}

HAL_StatusTypeDef HAL_RNG_GenerateRandomNumber(RNG_HandleTypeDef *hrng, uint32_t *random32bit)
{
	(void)hrng;
	*random32bit = web_rng_next();
	return HAL_OK;
}

uint32_t HAL_RNG_GetRandomNumber(RNG_HandleTypeDef *hrng)
{
	(void)hrng;
	return web_rng_next();
}

// ---- front panel shift registers (SPI1, see IO/DIO.c) ------------------------
// 74HC165 inputs: the button states set by the browser UI.
// 74HC595 outputs: the LEDs, read back by the browser UI.
uint8_t web_buttonState[NUM_INPUTS_BYTES];
uint8_t web_ledState[NUM_OUTPUTS_BYTES];

HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *hspi)
{
	(void)hspi;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
	(void)hspi;
	(void)Timeout;
	if (Size > NUM_INPUTS_BYTES)
		Size = NUM_INPUTS_BYTES;
	memcpy(pData, web_buttonState, Size);
	return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
	(void)hspi;
	(void)Timeout;
	if (Size > NUM_OUTPUTS_BYTES)
		Size = NUM_OUTPUTS_BYTES;
	memcpy(web_ledState, pData, Size);
	return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi, uint8_t *pTxData, uint8_t *pRxData,
                                          uint16_t Size, uint32_t Timeout)
{
	HAL_SPI_Transmit(hspi, pTxData, Size, Timeout);
	return HAL_SPI_Receive(hspi, pRxData, Size, Timeout);
}

// ---- GPIO -------------------------------------------------------------------
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState)
{
	if (PinState != GPIO_PIN_RESET)
		GPIOx->ODR |= GPIO_Pin;
	else
		GPIOx->ODR &= ~(uint32_t)GPIO_Pin;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
	return (GPIOx->IDR & GPIO_Pin) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
	GPIOx->ODR ^= GPIO_Pin;
}
