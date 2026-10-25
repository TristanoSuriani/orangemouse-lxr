/*
 * web_main.c
 *
 * Replaces main.c and the interrupt handlers of stm32f7xx_it.c for the
 * WebAssembly build. The browser's audio thread calls web_render() for every
 * block of 64 samples; each call
 *
 *  1. runs the timer interrupts for the time the block covers (TIM14 at
 *     4 kHz: systick_ticks and menu_tick(); SysTick at 1 kHz),
 *  2. runs one pass of the firmware's main loop (front panel, pots, menu,
 *     sound engine, MIDI, autosave),
 *  3. hands the block that the sound engine rendered into dma_buffer and
 *     dma_buffer2 (main and second stereo output) to the browser.
 *
 * Front panel input and display/LED output go through the exported web_*
 * functions below.
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */
#include "main.h"
#include "fatfs.h"
#include "lcd.h"
#include "menu/menu.h"
#include "IO/DIO.h"
#include "menu/screensaver.h"
#include "menu/copyClearTools.h"
#include "ledHandler.h"
#include "buttonHandler.h"
#include "encoder.h"
#include "IO/adcPots.h"
#include "Preset/PresetManager.h"
#include "Soundengine/Soundengine.h"
#include "Soundengine/AudioCodecManager.h"
#include "Soundengine/MIDI/Uart.h"
#include "web_port.h"
#include <math.h>
#include <string.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION " web"
#endif

#define NUM_POTS          4
#define NUM_FADERS        6
#define POT_STEP_DEGREES  3.f   // how far a pot moves per main loop pass
#define TIM14_RATE        4000.f
#define SYSTICK_DIVIDER   4     // SysTick (1 kHz) = TIM14 (4 kHz) / 4

void USART3_IRQHandler(void);

// defined in web_hal.c (main.c on the MCU)
extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim14;
extern RNG_HandleTypeDef hrng;

static float sampleRate = 44100.f;
static float tim14Phase;
static uint8_t sysTickDivider;
static uint8_t booted;

static float potAngle[NUM_POTS];
static float potPending[NUM_POTS];

// rendered audio for the browser, one block, planar: out1 L/R, out2 L/R
static float audioOut[4][OUTPUT_DMA_SIZE];

// ---- pots and faders ----------------------------------------------------------
// The four pots are endless pots with two wipers 90 degrees apart, read on
// ADC channels 2i and 2i+1; adc_checkPots() turns wiper movement into steps.
static uint16_t wiperValue(float degrees0to180)
{
	return (uint16_t)(degrees0to180 / 180.f * 4095.f) << 4;
}

static void writePotWipers(uint8_t pot)
{
	float a = potAngle[pot];
	// wiper A: triangle peaking at 90 degrees, wiper B: triangle peaking at 180
	float wiperA = a <= 90.f ? 90.f + a : (a <= 270.f ? 270.f - a : a - 270.f);
	float wiperB = a <= 180.f ? a : 360.f - a;
	adc_buf[pot * 2] = wiperValue(wiperA);
	adc_buf[pot * 2 + 1] = wiperValue(wiperB);
}

static void stepPots(void)
{
	for (uint8_t i = 0; i < NUM_POTS; i++)
	{
		if (fabsf(potPending[i]) < POT_STEP_DEGREES)
			continue;
		const float step = potPending[i] > 0 ? POT_STEP_DEGREES : -POT_STEP_DEGREES;
		potPending[i] -= step;
		potAngle[i] = fmodf(potAngle[i] + step + 360.f, 360.f);
		writePotWipers(i);
	}
}

// ---- interrupts ---------------------------------------------------------------
static void tim14Interrupt(void)
{
	systick_ticks++;
	if (lcd_process_display_interrupt)
	{
		menu_tick();
		menu_tick();
		menu_tick();
		menu_tick();
	}
}

static void sysTickInterrupt(void)
{
	web_hal_incTick();
	screensaver_timer++;
	encoder_callback();
}

static void runTimers(float seconds)
{
	tim14Phase += seconds * TIM14_RATE;
	while (tim14Phase >= 1.f)
	{
		tim14Phase -= 1.f;
		tim14Interrupt();
		if (++sysTickDivider >= SYSTICK_DIVIDER)
		{
			sysTickDivider = 0;
			sysTickInterrupt();
		}
	}
}

// ---- MIDI (USART3) ----------------------------------------------------------------
static void midiTransmit(void)
{
	// While the firmware has the TX-empty interrupt enabled it has bytes to send.
	for (int i = 0; i < 256 && (USART3->CR1 & USART_CR1_TXEIE); i++)
	{
		USART3->TDR = 0xFFFF;  // no byte written yet
		USART3->ISR = USART_ISR_TXE;
		USART3_IRQHandler();
		USART3->ISR = 0;
		if (USART3->TDR != 0xFFFF)
			web_midi_out((uint8_t)USART3->TDR);
	}
}

WEB_EXPORT void web_midi_in(uint8_t byte)
{
	USART3->RDR = byte;
	USART3->ISR = USART_ISR_RXNE;
	USART3_IRQHandler();
	USART3->ISR = 0;
}

// ---- boot -------------------------------------------------------------------------
static void bootMessage(const char *line1, const char *line2)
{
	lcd_clear();
	lcd_home();
	lcd_string(line1);
	lcd_setcursor(0, 2);
	lcd_string(line2);
}

// sd_formatRoutine() in main.c, without asking (the card is the browser's)
// and without stopping to ask for a reboot. The init project is saved later,
// by saveInitProject().
static FRESULT formatCard(void)
{
	static BYTE work[_MAX_SS];

	bootMessage("Formatting card", "...");
	FRESULT res = f_mkfs("", FM_ANY, 0, work, sizeof(work));
	if (res != FR_OK)
		return res;
	res = f_mount((FATFS *)&SDFatFS, (TCHAR const *)SDPath, 1);
	if (res != FR_OK)
		return res;

	preset_createGlobals();
	return FR_OK;
}

// The rest of sd_formatRoutine(): store the init kit as project 0. This has to
// wait for soundengine_init(), which sets up the empty patterns (length 16);
// saved before that, as on the MCU, every track of the project has length 0.
static void saveInitProject(void)
{
	preset_init(0);
	sprintf(savefileNames[0][PROJECT_NAMES], "Init");
	preset_saveProject(0);
}

// The start-up part of main() in main.c, for a machine without clocks,
// DMA and peripherals to configure.
WEB_EXPORT void web_boot(float rate, uint32_t seed)
{
	sampleRate = rate;
	web_rng_seed(seed);

	hspi1.Instance = SPI1;
	htim2.Instance = TIM2;
	htim14.Instance = TIM14;
	huart3.Instance = USART3;
	hrng.Instance = RNG;

	for (uint8_t i = 0; i < NUM_POTS; i++)
		writePotWipers(i);
	for (uint8_t i = 0; i < NUM_FADERS; i++)
		adc_buf[8 + i] = 0xCCC0;  // faders at 80 %

	// cables in all four outputs (the jack switches steer the mixer routing)
	switch_l1_GPIO_Port->IDR |= switch_l1_Pin;
	switch_r1_GPIO_Port->IDR |= switch_r1_Pin;
	switch_l2_GPIO_Port->IDR |= switch_l2_Pin;
	switch_r2_GPIO_Port->IDR |= switch_r2_Pin;

	MX_FATFS_Init();
	dio_init(&hspi1);
	lcd_init();
	lcd_clear();
	lcd_home();
	menu_init();
	adc_checkPots();
	adc_checkPots();
	MX_FATFS_ReInit();

	bootMessage("Sonic Potions", "LXR2 Drums V" FIRMWARE_VERSION);

	uint8_t sd_card_ok = 0;
	uint8_t formatted = 0;
	FRESULT res = f_mount((FATFS *)&SDFatFS, (TCHAR const *)SDPath, 1);
	if (res == FR_NO_FILESYSTEM)
	{
		res = formatCard();
		formatted = res == FR_OK;
	}
	if (res == FR_OK)
		sd_card_ok = 1;
	else
		web_log("SD card could not be mounted, running without it");

	lcd_clear();
	adc_checkPots();
	adc_checkPots();

	sprintf(currentFolder, "/proj00");
	if (sd_card_ok)
	{
		FILINFO fno;
		res = f_stat(currentFolder, &fno);
		if (res == FR_NO_FILE)
		{
			if (f_mkdir(currentFolder) == FR_OK)
				f_chdir(currentFolder);
		}
		else
		{
			f_chdir(currentFolder);
		}
	}

	soundengine_init();

	if (sd_card_ok)
	{
		if (!preset_loadGlobals())
		{
			bootMessage("glo.cfg error", "Using defaults.");
			preset_createGlobals();
		}

		if (formatted)
			saveInitProject();

		f_chdir(currentFolder);
		// init morph target to preset 0
		morphParameters = mainParameters[0];

		// load project + song names
		for (uint8_t i = 0; i < NUM_KIT_SLOTS; i++)
			preset_loadProjectNames(i);
		f_chdir("/proj00");

		uint8_t lastUsedProject;
		preset_loadLastUsedProjectNrFromSD(&lastUsedProject);
		preset_loadProject(lastUsedProject, 0);
		menu_loadedPresetNr[CURRENT_PROJECT] = menu_selectedPresetNr[SAVE_TYPE_PROJECT] = lastUsedProject;
	}
	else
	{
		copyClear_clearCurrentPattern();
		preset_init(0);
		preset_loadDefaultGlobals();
	}

	initMidiUart();
	__HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE);

	buttonHandler_init();

	if (sd_card_ok)
	{
		preset_initTmpFolder();
		loadTmpFolderFiles();
		preset_LoadTmpKit();
		preset_LoadTmpPat();
	}
	led_updateSeqLeds();
	preset_resetAllSaveTimers();

	lcd_process_display_interrupt = 1;
	booted = 1;
}

// ---- the main loop ------------------------------------------------------------
// One pass of the while(1) loop in main.c.
static void mainLoopPass(void)
{
	stepPots();
	adc_checkPots();
	soundengine_process();
	buttonHandler_tick();
	stepPots();
	adc_checkPots();
	menu_parseEncoder(encode_read4(), encode_readButton());
	led_tickHandler();
	stepPots();
	adc_checkPots();
	soundengine_process();
	screensaver_check();
	dio_process();
	stepPots();
	adc_checkPots();
	soundengine_process();
	adc_checkFaders();

	// USB-MIDI is not available; browser MIDI arrives through web_midi_in()
	uart_processMidi();
	preset_processTimer();
}

// Renders one block of OUTPUT_DMA_SIZE stereo frames per output.
// Returns the address of the planar float block (see audioOut).
WEB_EXPORT float *web_render(void)
{
	if (!booted)
		return &audioOut[0][0];

	runTimers(OUTPUT_DMA_SIZE / sampleRate);

	// the I2S DMA has played half 0: it needs refilling
	bCurrentSampleValid = 0;
	mainLoopPass();
	if (bCurrentSampleValid != SAMPLE_VALID)
	{
		// the TIM2 interrupt in stm32f7xx_it.c
		seq_tick();
		soundengine_calcNextSampleBlock();
	}

	midiTransmit();

	const float scale = 1.f / 32768.f;
	for (int i = 0; i < OUTPUT_DMA_SIZE; i++)
	{
		audioOut[0][i] = dma_buffer[i * 2] * scale;
		audioOut[1][i] = dma_buffer[i * 2 + 1] * scale;
		audioOut[2][i] = dma_buffer2[i * 2] * scale;
		audioOut[3][i] = dma_buffer2[i * 2 + 1] * scale;
	}
	return &audioOut[0][0];
}

WEB_EXPORT int web_block_size(void)
{
	return OUTPUT_DMA_SIZE;
}

// ---- front panel input -------------------------------------------------------
// button: number from enum ButtonNumbers (buttonHandler.h), 0..44
WEB_EXPORT void web_set_button(uint8_t button, uint8_t pressed)
{
	if (button == BUT_PAGE_DOWN)
	{
		// the one button that is not on the shift registers (see DIO.c)
		if (pressed)
			SW_43_GPIO_Port->IDR |= SW_43_Pin;
		else
			SW_43_GPIO_Port->IDR &= ~SW_43_Pin;
		return;
	}
	if (button >= NUM_INPUTS_BYTES * 8)
		return;
	if (pressed)
		web_buttonState[button / 8] |= (uint8_t)(1 << (button % 8));
	else
		web_buttonState[button / 8] &= (uint8_t)~(1 << (button % 8));
}

WEB_EXPORT void web_encoder(int32_t detents)
{
	web_encoder_turn(detents);
}

WEB_EXPORT void web_encoder_button(uint8_t pressed)
{
	web_encoder_setButton(pressed);
}

// Turn pot 0..3 by the given angle (positive = clockwise).
WEB_EXPORT void web_pot_turn(uint8_t pot, float degrees)
{
	if (pot < NUM_POTS)
		potPending[pot] += degrees;
}

// Fader 0..5 (drum 1-3, snare, cymbal, hihat), position 0..1.
WEB_EXPORT void web_set_fader(uint8_t fader, float position)
{
	if (fader >= NUM_FADERS)
		return;
	if (position < 0.f)
		position = 0.f;
	if (position > 1.f)
		position = 1.f;
	adc_buf[8 + fader] = (uint16_t)(position * 4095.f) << 4;
}

// ---- display and LED output -------------------------------------------------------
WEB_EXPORT uint8_t *web_lcd_text(void)
{
	return &web_lcdText[0][0];
}

WEB_EXPORT uint8_t *web_lcd_cgram(void)
{
	return web_lcdCgram;
}

WEB_EXPORT uint32_t web_lcd_version(void)
{
	return web_lcdOn ? web_lcdVersion : 0;
}

// LED states as bits, numbered as enum LedNumbers (ledHandler.h); bit 40 is
// the page-down LED, which is driven from a GPIO pin; 41..44 the VA voices.
WEB_EXPORT uint32_t web_leds_low(void)
{
	return (uint32_t)web_ledState[0] | ((uint32_t)web_ledState[1] << 8) |
	       ((uint32_t)web_ledState[2] << 16) | ((uint32_t)web_ledState[3] << 24);
}

WEB_EXPORT uint32_t web_leds_high(void)
{
	return (uint32_t)web_ledState[4] | ((uint32_t)(web_ledState[5] & 0xFE) << 8) |
	       ((LED_43_GPIO_Port->ODR & LED_43_Pin) ? 0x100u : 0u);
}

// ---- SD card persistence -----------------------------------------------------------
WEB_EXPORT uint32_t web_sd_sector_count(void)
{
	return web_sdSectors;
}

WEB_EXPORT uint8_t *web_sd_image(void)
{
	return web_sdImage;
}
