/*
 * web_lcd.c
 *
 * Replaces lcd.c, which bit-bangs a 16x2 HD44780-compatible display over
 * GPIO, for the WebAssembly build. The same API drives a model of the display
 * controller (DDRAM, CGRAM, address counter) that the browser draws.
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */
#include "lcd.h"
#include "web_port.h"
#include <string.h>

uint8_t lcd_process_display_interrupt = 0;

uint8_t web_lcdText[WEB_LCD_ROWS][WEB_LCD_COLS];
uint8_t web_lcdCgram[64];
uint8_t web_lcdOn = 1;
uint32_t web_lcdVersion;

static uint8_t ddram[0x80];
static uint8_t addressCounter;
static uint8_t addressIsCgram;
static uint8_t entryIncrement = 1;

static void refreshText(void)
{
	for (int col = 0; col < WEB_LCD_COLS; col++)
	{
		web_lcdText[0][col] = ddram[LCD_DDADR_LINE1 + col];
		web_lcdText[1][col] = ddram[LCD_DDADR_LINE2 + col];
	}
	web_lcdVersion++;
}

static void advance(void)
{
	if (entryIncrement)
		addressCounter++;
	else
		addressCounter--;
	addressCounter &= addressIsCgram ? 0x3F : 0x7F;
}

void lcd_command(uint8_t data)
{
	if (data & LCD_SET_DDADR)
	{
		addressIsCgram = 0;
		addressCounter = data & 0x7F;
	}
	else if (data & LCD_SET_CGADR)
	{
		addressIsCgram = 1;
		addressCounter = data & 0x3F;
	}
	else if (data & LCD_SET_FUNCTION)
	{
		// bus width, lines, font: nothing to model
	}
	else if (data & LCD_SET_SHIFT)
	{
		// cursor/display shift and the Winstar OLED power bits: not used for output
	}
	else if (data & LCD_SET_DISPLAY)
	{
		web_lcdOn = (data & LCD_DISPLAY_ON) != 0;
		web_lcdVersion++;
	}
	else if (data & LCD_SET_ENTRY)
	{
		entryIncrement = (data & LCD_ENTRY_INCREASE) != 0;
	}
	else if (data & LCD_CURSOR_HOME)
	{
		addressIsCgram = 0;
		addressCounter = 0;
	}
	else if (data & LCD_CLEAR_DISPLAY)
	{
		memset(ddram, ' ', sizeof(ddram));
		addressIsCgram = 0;
		addressCounter = 0;
		entryIncrement = 1;
		refreshText();
	}
}

void lcd_data(uint8_t data)
{
	if (addressIsCgram)
	{
		web_lcdCgram[addressCounter] = data & 0x1F;
		web_lcdVersion++;
	}
	else
	{
		ddram[addressCounter] = data;
		refreshText();
	}
	advance();
}

void lcd_init(void)
{
	memset(ddram, ' ', sizeof(ddram));
	memset(web_lcdCgram, 0, sizeof(web_lcdCgram));
	addressCounter = 0;
	addressIsCgram = 0;
	entryIncrement = 1;
	web_lcdOn = 1;
	refreshText();
}

void lcd_showSplash(void)
{
}

void lcd_clear(void)
{
	lcd_command(LCD_CLEAR_DISPLAY);
}

void lcd_home(void)
{
	lcd_command(LCD_CURSOR_HOME);
}

static uint8_t cursorAddress(uint8_t x, uint8_t y)
{
	switch (y)
	{
	default:
	case 1: return (uint8_t)(LCD_SET_DDADR + LCD_DDADR_LINE1 + x);
	case 2: return (uint8_t)(LCD_SET_DDADR + LCD_DDADR_LINE2 + x);
	case 3: return (uint8_t)(LCD_SET_DDADR + LCD_DDADR_LINE3 + x);
	case 4: return (uint8_t)(LCD_SET_DDADR + LCD_DDADR_LINE4 + x);
	}
}

void lcd_setcursor(uint8_t x, uint8_t y)
{
	if (y < 1 || y > 4)
		return;
	lcd_command(cursorAddress(x, y));
}

void lcd_string(const char *data)
{
	while (*data != '\0')
		lcd_data((uint8_t)*data++);
}

void lcd_generatechar(uint8_t code, const uint8_t *data)
{
	lcd_command(LCD_SET_CGADR | (uint8_t)(code << 3));
	for (uint8_t i = 0; i < 8; i++)
		lcd_data(data[i]);
}

void lcd_turnOn(uint8_t isOn, uint8_t cursorOn)
{
	lcd_command((uint8_t)(LCD_SET_DISPLAY | (LCD_DISPLAY_ON * isOn) | LCD_CURSOR_ON * cursorOn | LCD_BLINKING_OFF));
}

// The non-blocking variants complete in one call (return 1 = done).
uint8_t lcd_command_nonblock(uint8_t data)
{
	lcd_command(data);
	return 1;
}

uint8_t lcd_clear_nonblock(void)
{
	lcd_clear();
	return 1;
}

uint8_t lcd_turnOn_nonblock(uint8_t isOn, uint8_t cursorOn)
{
	lcd_turnOn(isOn, cursorOn);
	return 1;
}

uint8_t lcd_setcursor_nonBlock(uint8_t x, uint8_t y)
{
	lcd_command(cursorAddress(x, y));
	return 1;
}

uint8_t lcd_data_nonBlock(uint8_t data)
{
	lcd_data(data);
	return 1;
}

uint8_t lcd_generatechar_nonBlock(uint8_t code, const uint8_t *data)
{
	lcd_generatechar(code, data);
	return 1;
}
