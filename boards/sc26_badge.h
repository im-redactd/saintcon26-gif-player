// SPDX-License-Identifier: MIT
// Copyright (c) 2026 redactd
// SC26 badge: custom RP2350A (QFN-60) board, 16 MB flash. Full pin map: docs/HARDWARE.md.
#ifndef _BOARDS_SC26_BADGE_H
#define _BOARDS_SC26_BADGE_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

#define PICO_RP2350A 1

// --- Display: Sitronix ST77916 round LCD on a 16-pin FPC, QSPI driven by PIO ---
#define SC26_LCD_SCLK_PIN 10
#define SC26_LCD_D0_PIN 11  // D0..D3 must be consecutive: 11, 12, 13, 14
#define SC26_LCD_CS_PIN 15
#define SC26_LCD_RST_PIN 9
#define SC26_LCD_TE_PIN 8
#define SC26_LCD_BL_PIN 7   // PWM backlight
#define SC26_LCD_WIDTH 360
#define SC26_LCD_HEIGHT 360

// --- WS2812 addressable RGB LEDs (9 in the chain, GRB order, 800 kHz) ---
#define SC26_WS2812_PIN 24
#ifndef SC26_WS2812_COUNT
#define SC26_WS2812_COUNT 9
#endif

// --- Single LED blinked by the original firmware's PIO "blink" program ---
#define SC26_STATUS_LED_PIN 18

// --- Buttons: 5-way D-pad + A/B, switches to GND, internal pull-ups.
// Order and masks match the original firmware's table (mask = 1 << index).
#define SC26_BTN_UP_PIN 23      // 0x01
#define SC26_BTN_DOWN_PIN 21    // 0x02
#define SC26_BTN_LEFT_PIN 22    // 0x04
#define SC26_BTN_RIGHT_PIN 19   // 0x08
#define SC26_BTN_CENTER_PIN 20  // 0x10  (D-pad press)
#define SC26_BTN_B_PIN 16       // 0x20
#define SC26_BTN_A_PIN 17       // 0x40

// --- Dock UART and EEPROM (unused by the GIF firmware, kept for reference) ---
#define PICO_DEFAULT_UART 0
#define PICO_DEFAULT_UART_TX_PIN 0
#define PICO_DEFAULT_UART_RX_PIN 1
#define PICO_DEFAULT_I2C 0
#define PICO_DEFAULT_I2C_SDA_PIN 4
#define PICO_DEFAULT_I2C_SCL_PIN 5

// --- Flash ---
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
