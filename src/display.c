// SPDX-License-Identifier: MIT
// Copyright (c) 2026 redactd
#include "display.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include "qspi_lcd.pio.h"
#include "st77916_init.h"

// QSPI opcodes (sent on D0 only, followed by a 24-bit address carrying the command: 0x00 cmd 0x00)
#define QSPI_OP_WRITE_CMD 0x02    // command + parameters, all single-line
#define QSPI_OP_WRITE_PIXELS 0x32 // command single-line, pixel data on all four lines

// PIO clock divider: SCLK = clk_sys / (2 * div). 1 -> 75 MHz at 150 MHz, the same bus speed the
// original firmware ran this panel at.
#ifndef SC26_LCD_PIO_CLKDIV
#define SC26_LCD_PIO_CLKDIV 1.0f
#endif

static uint16_t framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static PIO pio;
static uint sm;
static int dma_chan;
static bool frame_in_flight;
static uint bl_slice;

uint16_t *display_framebuffer(void) {
    return framebuffer;
}

static inline void fifo_put_byte(uint8_t v) {
    while (pio_sm_is_tx_fifo_full(pio, sm)) tight_loop_contents();
    *(volatile uint8_t *)&pio->txf[sm] = v;  // byte write lands in bits 31:24 of the FIFO word
}

// One byte on D0 only: 8 bits -> 8 nibbles (bit in nibble bit 0), packed two per FIFO byte.
static void send_single_line(uint8_t b) {
    for (int bit = 7; bit > 0; bit -= 2)
        fifo_put_byte((uint8_t)(((b >> bit) & 1) << 4 | ((b >> (bit - 1)) & 1)));
}

// Waits until the state machine has shifted out everything queued so far.
static void wait_bus_idle(void) {
    uint32_t stall = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
    pio->fdebug = stall;  // clear, then wait for the SM to stall on an empty FIFO again
    while (!pio_sm_is_tx_fifo_empty(pio, sm) || !(pio->fdebug & stall)) tight_loop_contents();
}

static inline void cs(bool active) {
    gpio_put(SC26_LCD_CS_PIN, !active);
    // back-to-back commands would otherwise deselect for only ~2 cycles (13 ns); give the
    // panel a proper CS-high time (>= 107 ns at 150 MHz)
    if (!active) busy_wait_at_least_cycles(16);
}

static void write_command(uint8_t cmd, const uint8_t *data, size_t len) {
    cs(true);
    send_single_line(QSPI_OP_WRITE_CMD);
    send_single_line(0x00);
    send_single_line(cmd);
    send_single_line(0x00);
    for (size_t i = 0; i < len; i++) send_single_line(data[i]);
    wait_bus_idle();
    cs(false);
}

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    x0 += ST77916_X_OFFSET; x1 += ST77916_X_OFFSET;
    y0 += ST77916_Y_OFFSET; y1 += ST77916_Y_OFFSET;
    const uint8_t caset[] = {x0 >> 8, x0 & 0xff, x1 >> 8, x1 & 0xff};
    const uint8_t raset[] = {y0 >> 8, y0 & 0xff, y1 >> 8, y1 & 0xff};
    write_command(0x2a, caset, sizeof caset);
    write_command(0x2b, raset, sizeof raset);
}

static void dma_done_wait(void) {
    if (!frame_in_flight) return;
    dma_channel_wait_for_finish_blocking(dma_chan);
    wait_bus_idle();
    cs(false);
    frame_in_flight = false;
}

bool display_busy(void) {
    return frame_in_flight && (dma_channel_is_busy(dma_chan) || !pio_sm_is_tx_fifo_empty(pio, sm));
}

void display_wait(void) {
    dma_done_wait();
}

void display_present(void) {
    dma_done_wait();
    set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
    cs(true);
    send_single_line(QSPI_OP_WRITE_PIXELS);
    send_single_line(0x00);
    send_single_line(0x2c);  // RAMWR
    send_single_line(0x00);
    frame_in_flight = true;
    dma_channel_set_read_addr(dma_chan, framebuffer, false);
    dma_channel_set_trans_count(dma_chan, sizeof framebuffer, true);
}

void display_set_backlight(uint8_t percent) {
    if (percent > 100) percent = 100;
    pwm_set_gpio_level(SC26_LCD_BL_PIN, (uint16_t)(percent * 10));  // wrap is 1000
}

void display_init(void) {
    // control lines
    gpio_init(SC26_LCD_CS_PIN);
    gpio_put(SC26_LCD_CS_PIN, 1);
    gpio_set_dir(SC26_LCD_CS_PIN, GPIO_OUT);
    gpio_set_slew_rate(SC26_LCD_CS_PIN, GPIO_SLEW_RATE_FAST);
    gpio_init(SC26_LCD_RST_PIN);
    gpio_set_dir(SC26_LCD_RST_PIN, GPIO_OUT);
    gpio_init(SC26_LCD_TE_PIN);
    gpio_set_dir(SC26_LCD_TE_PIN, GPIO_IN);
    gpio_pull_up(SC26_LCD_TE_PIN);

    // backlight PWM, off until the first frame is ready
    gpio_set_function(SC26_LCD_BL_PIN, GPIO_FUNC_PWM);
    bl_slice = pwm_gpio_to_slice_num(SC26_LCD_BL_PIN);
    pwm_config pc = pwm_get_default_config();
    pwm_config_set_wrap(&pc, 999);
    pwm_config_set_clkdiv(&pc, (float)clock_get_hz(clk_sys) / (1000.0f * 20000.0f));  // ~20 kHz
    pwm_init(bl_slice, &pc, true);
    pwm_set_gpio_level(SC26_LCD_BL_PIN, 0);

    // QSPI engine
    uint offset;
    hard_assert(pio_claim_free_sm_and_add_program_for_gpio_range(&qspi_lcd_program, &pio, &sm, &offset,
                                                                 SC26_LCD_SCLK_PIN, 5, true));
    qspi_lcd_program_init(pio, sm, offset, SC26_LCD_D0_PIN, SC26_LCD_SCLK_PIN, SC26_LCD_PIO_CLKDIV);
    for (int i = 0; i < 5; i++) gpio_set_slew_rate(SC26_LCD_SCLK_PIN + i, GPIO_SLEW_RATE_FAST);

    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_8);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, true));
    dma_channel_configure(dma_chan, &dc, &pio->txf[sm], framebuffer, 0, false);

    // hardware reset, same timing as the original firmware
    gpio_put(SC26_LCD_RST_PIN, 0);
    sleep_ms(100);
    gpio_put(SC26_LCD_RST_PIN, 1);
    sleep_ms(100);

    for (size_t i = 0; i < count_of(st77916_init_cmds); i++) {
        const struct st77916_cmd *c = &st77916_init_cmds[i];
        write_command(c->cmd, c->data, c->len);
        if (c->delay_ms) sleep_ms(c->delay_ms);
    }

    memset(framebuffer, 0, sizeof framebuffer);
    display_present();
    display_wait();
}
