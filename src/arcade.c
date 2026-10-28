// klippy-cade: Klippy on the glass at boot, then a picker. Doom is the first entry.
#include "arcade.h"

#include <stdio.h>
#include <string.h>

#include "buttons.h"
#include "display.h"
#include "font8.h"
#include "gif_store.h"
#include "hardware/resets.h"
#include "hardware/structs/nvic.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/systick.h"
#include "hardware/structs/watchdog.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "klippy_face.h"
#include "leds.h"
#include "pico/stdlib.h"
#include "tusb.h"

#define DOOM_VTOR 0x10100000u

// Doom leaves a crash record in watchdog scratch 0..3 (see Doom's badge_diag.h).
#define DIAG_MAGIC 0x4b4c0000u
#define DIAG_HANG 1u

static uint16_t ink(uint8_t r, uint8_t g, uint8_t b) {
    return display_rgb(r, g, b);
}

static void clear(void) {
    memset(display_framebuffer(), 0, DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));
}

static void blit(const uint16_t *src, int sw, int sh, int dx, int dy) {
    uint16_t *fb = display_framebuffer();
    for (int y = 0; y < sh; y++) {
        if ((unsigned)(dy + y) >= DISPLAY_HEIGHT) continue;
        int x0 = dx < 0 ? 0 : dx;
        int x1 = dx + sw;
        if (x1 > DISPLAY_WIDTH) x1 = DISPLAY_WIDTH;
        if (x0 >= x1) continue;
        memcpy(&fb[(dy + y) * DISPLAY_WIDTH + x0], &src[y * sw + (x0 - dx)], (size_t)(x1 - x0) * 2);
    }
}

static void glyph(int x, int y, char ch, uint16_t color, int scale) {
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t *g = font8 + (size_t)(ch - 32) * FONT_H * FONT_ROWBYTES;
    uint16_t *fb = display_framebuffer();
    for (int row = 0; row < FONT_H; row++) {
        for (int col = 0; col < FONT_W; col++) {
            uint8_t bits = g[row * FONT_ROWBYTES + col / 8];
            if (!(bits & (uint8_t)(0x80 >> (col % 8)))) continue;
            for (int sy = 0; sy < scale; sy++) {
                int py = y + row * scale + sy;
                if ((unsigned)py >= DISPLAY_HEIGHT) continue;
                for (int sx = 0; sx < scale; sx++) {
                    int px = x + col * scale + sx;
                    if ((unsigned)px >= DISPLAY_WIDTH) continue;
                    fb[py * DISPLAY_WIDTH + px] = color;
                }
            }
        }
    }
}

static int text_width(const char *s, int scale) {
    return (int)strlen(s) * FONT_W * scale;
}

static void text(int x, int y, const char *s, uint16_t color, int scale) {
    for (; *s; s++) {
        glyph(x, y, *s, color, scale);
        x += FONT_W * scale;
    }
}

static void text_centered(int y, const char *s, uint16_t color, int scale) {
    int w = text_width(s, scale);
    text((DISPLAY_WIDTH - w) / 2, y, s, color, scale);
}

static void present(void) {
    display_wait();
    display_present();
}

bool doom_image_present(void) {
    const uint32_t *vt = (const uint32_t *)DOOM_VTOR;
    uint32_t sp = vt[0];
    uint32_t pc = vt[1];
    if (sp < 0x20000000u || sp > 0x20082000u) return false;
    if ((pc & 1u) == 0) return false;
    pc &= ~1u;
    return pc >= DOOM_VTOR && pc < 0x10200000u;
}

void launch_doom(void) {
    const uint32_t *vt = (const uint32_t *)DOOM_VTOR;
    uint32_t sp = vt[0];
    uint32_t pc = vt[1];
    tud_disconnect();
    sleep_ms(30);
    display_wait();
    // Stage 0 means Doom never reached main(). The watchdog brings us back
    // here if Doom hangs before it takes over feeding it.
    watchdog_hw->scratch[0] = DIAG_MAGIC | (DIAG_HANG << 12);
    watchdog_hw->scratch[1] = 0;
    watchdog_hw->scratch[2] = 0;
    watchdog_hw->scratch[3] = 0;
    watchdog_enable(8000, true);
    save_and_disable_interrupts();
    systick_hw->csr = 0;
    nvic_hw->icer[0] = ~0u;
    nvic_hw->icer[1] = ~0u;
    nvic_hw->icpr[0] = ~0u;
    nvic_hw->icpr[1] = ~0u;
    // The launcher's QSPI state machine is still clocking the panel. Doom's
    // claim of those pins panics if the hardware is left running, and the
    // launcher's stack limit would fault the first push on Doom's stack.
    reset_block(RESETS_RESET_PIO0_BITS | RESETS_RESET_PIO1_BITS |
                RESETS_RESET_DMA_BITS | RESETS_RESET_PWM_BITS |
                RESETS_RESET_USBCTRL_BITS);
    unreset_block_wait(RESETS_RESET_PIO0_BITS | RESETS_RESET_PIO1_BITS |
                       RESETS_RESET_DMA_BITS | RESETS_RESET_PWM_BITS);
    scb_hw->vtor = DOOM_VTOR;
    __dsb();
    __isb();
    __asm volatile(
        "msr msplim, %0\n"
        "msr msp, %1\n"
        "cpsie i\n"
        "bx %2"
        :
        : "r"(0), "r"(sp), "r"(pc)
        : "memory");
    while (true) tight_loop_contents();
}

static void crash_report(void) {
    uint32_t s0 = watchdog_hw->scratch[0];
    if ((s0 & 0xffff0000u) != DIAG_MAGIC) return;
    uint32_t pc = watchdog_hw->scratch[1];
    uint32_t lr = watchdog_hw->scratch[2];
    uint32_t extra = watchdog_hw->scratch[3];
    watchdog_hw->scratch[0] = 0;

    static const char *const kinds[] = {"?", "hang", "fault", "panic"};
    uint32_t kind = (s0 >> 12) & 0xfu;
    char line[40];
    clear();
    text_centered(70, "doom stopped", ink(255, 80, 80), 2);
    snprintf(line, sizeof line, "%s at stage %lu", kind < 4 ? kinds[kind] : "?", (unsigned long)(s0 & 0xfffu));
    text_centered(120, line, ink(255, 214, 64), 2);
    snprintf(line, sizeof line, "pc %08lx", (unsigned long)pc);
    text_centered(170, line, ink(220, 220, 220), 2);
    snprintf(line, sizeof line, "lr %08lx", (unsigned long)lr);
    text_centered(205, line, ink(220, 220, 220), 2);
    snprintf(line, sizeof line, "x  %08lx", (unsigned long)extra);
    text_centered(240, line, ink(220, 220, 220), 2);
    text_centered(300, "any button", ink(140, 140, 140), 1);
    present();
    while (!buttons_poll()) {
        tud_task();
        sleep_ms(10);
    }
}

void arcade_splash(void) {
    crash_report();
    clear();
    blit(klippy_face, KLIPPY_W, KLIPPY_H, (DISPLAY_WIDTH - KLIPPY_W) / 2, 16);
    text_centered(268, "klippy-cade", ink(255, 214, 64), 2);
    present();
    leds_fill(80, 40, 0);
    leds_show();
    absolute_time_t until = make_timeout_time_ms(2200);
    while (!time_reached(until)) {
        tud_task();
        if (buttons_poll()) break;
        sleep_ms(10);
    }
}

int arcade_menu(void) {
    int sel = 0;
    bool drawn = false;
    for (;;) {
        tud_task();
        int n = 1 + gif_store_count();
        if (sel >= n) sel = n - 1;
        uint32_t pressed = buttons_poll();
        if (pressed & (1u << BTN_UP)) {
            sel = sel == 0 ? n - 1 : sel - 1;
            drawn = false;
        }
        if (pressed & (1u << BTN_DOWN)) {
            sel = (sel + 1) % n;
            drawn = false;
        }
        if (pressed & ((1u << BTN_A) | (1u << BTN_CENTER))) {
            if (sel == 0) {
                if (!doom_image_present()) {
                    text_centered(300, "doom is missing", ink(255, 80, 80), 1);
                    present();
                    sleep_ms(900);
                    drawn = false;
                    continue;
                }
                return -1;
            }
            return sel - 1;
        }

        uint32_t t = to_ms_since_boot(get_absolute_time());
        uint8_t glow = (uint8_t)(24 + ((t / 12) & 31));
        leds_fill(glow, glow / 4, 0);
        leds_show();

        if (drawn) {
            sleep_ms(10);
            continue;
        }
        clear();
        text_centered(28, "klippy-cade", ink(255, 214, 64), 2);
        int window = 5;
        int first = sel - 2;
        if (first < 0) first = 0;
        if (first > n - window) first = n - window;
        if (first < 0) first = 0;
        for (int row = 0; row < window && first + row < n; row++) {
            int idx = first + row;
            char label[20];
            if (idx == 0) {
                strcpy(label, "DOOM");
            } else {
                const char *name = gif_store_name(idx - 1);
                size_t len = strlen(name);
                if (len > 4 && strcmp(name + len - 4, ".gif") == 0) len -= 4;
                if (len > 14) len = 14;
                memcpy(label, name, len);
                label[len] = 0;
            }
            int y = 78 + row * 36;
            uint16_t color = idx == sel ? ink(255, 236, 180) : ink(170, 170, 170);
            if (idx == sel) {
                uint16_t *fb = display_framebuffer();
                for (int yy = y - 4; yy < y + FONT_H * 2 + 4; yy++) {
                    if ((unsigned)yy >= DISPLAY_HEIGHT) continue;
                    for (int xx = 36; xx < DISPLAY_WIDTH - 36; xx++) fb[yy * DISPLAY_WIDTH + xx] = ink(48, 16, 8);
                }
                color = ink(255, 220, 80);
            }
            int scale = 2;
            text((DISPLAY_WIDTH - text_width(label, scale)) / 2, y, label, color, scale);
        }
        text_centered(318, "A launch", ink(140, 140, 140), 1);
        present();
        drawn = true;
    }
}
