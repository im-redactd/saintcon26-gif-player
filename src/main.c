// SPDX-License-Identifier: MIT
// Copyright (c) 2026 redactd
// SC26 badge GIF player: plays animated GIFs from a USB-writable flash drive on the
// round ST77916 display and lights the badge LEDs to match.
#include <stdio.h>
#include <string.h>

#include "buttons.h"
#include "display.h"
#include "flash_disk.h"
#include "gif_player.h"
#include "gif_store.h"
#include "leds.h"
#include "pico/stdlib.h"
#include "tusb.h"

#define USB_QUIET_MS 1500     // host must stop writing this long before we rescan the drive
#define USB_STREAMING_MS 250  // a host write this recent means more are probably on the way
#define FLASH_IDLE_FLUSH_MS 300
#define PENDING_GIVE_UP_MS 120000  // stop waiting for a half-written new GIF after this long

enum led_mode { LED_AMBIENT, LED_RAIN, LED_OFF, LED_MODE_COUNT };
static const char *const led_mode_names[] = {"ambient", "rain", "off"};

static int current = -1;              // index into the store, -1 = built-in GIF
static char playing[GIF_STORE_NAME_MAX];  // name of the file being played ("" = built-in)
static unsigned playing_stamp;
static char pending[GIF_STORE_NAME_MAX];  // newly copied file that could not be opened yet
static bool paused;
static enum led_mode led_mode = LED_AMBIENT;
static int backlight = 80;
static uint32_t frames_drawn;
static uint32_t decode_us_avg, decode_us_max;  // per-frame GIF decode time (exponential average)
static bool usb_busy;
static uint32_t next_frame;  // when the next GIF frame is due (ms since boot)

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

// Opening a GIF clears the framebuffer: let the frame being sent finish first, and show the
// new GIF's first frame right away instead of after the old frame's delay.
static void before_open(void) {
    display_wait();
    next_frame = now_ms();
}

static int find(const char *name) {
    for (int i = 0; i < gif_store_count(); i++)
        if (strcmp(gif_store_name(i), name) == 0) return i;
    return -1;
}

static void open_builtin(void) {
    unsigned len;
    const unsigned char *gif = gif_store_builtin(&len);
    current = -1;
    playing[0] = 0;
    before_open();
    gif_player_open_memory(gif, len);
    printf("playing built-in matrix_rain.gif\n");
}

// Opening a file closes whatever was playing; on failure the previous GIF is put back.
static bool open_index(int index) {
    const char *name = gif_store_name(index);
    before_open();
    if (name && gif_player_open_file(name)) {
        current = index;
        strcpy(playing, name);
        playing_stamp = gif_store_stamp(name);
        int w, h;
        gif_player_canvas_size(&w, &h);
        printf("playing %s (%dx%d)\n", name, w, h);
        return true;
    }
    printf("could not open %s: %s\n", name ? name : "?", gif_player_error());
    int prev = playing[0] ? find(playing) : -1;
    if (prev >= 0 && prev != index && gif_player_open_file(playing)) current = prev;
    else open_builtin();
    return false;
}

// Opens the file `delta` steps from the current one, skipping files that will not open.
static void step(int delta) {
    int n = gif_store_count();
    int i = current;
    for (int tries = 0; tries < n; tries++) {
        i = i < 0 ? (delta > 0 ? 0 : n - 1) : ((i + delta) % n + n) % n;
        if (open_index(i)) return;
    }
    if (n) printf("no playable GIFs on the drive\n");
    open_builtin();
}

// After the host has finished writing: pick up new, removed and replaced files.
static void rescan_after_usb(void) {
    static char before[GIF_STORE_MAX][GIF_STORE_NAME_MAX];
    int old = gif_store_count();
    for (int i = 0; i < old; i++) strcpy(before[i], gif_store_name(i));
    int n = gif_store_rescan();
    bool remounted = gif_store_take_remounted();  // the open GIF's file handle is now invalid

    const char *fresh = NULL;  // first file that was not on the drive before
    for (int i = 0; i < n && !fresh; i++) {
        bool seen = false;
        for (int j = 0; j < old && !seen; j++) seen = strcmp(before[j], gif_store_name(i)) == 0;
        if (!seen) fresh = gif_store_name(i);
    }
    printf("drive scanned: %d GIF(s)%s%s\n", n, fresh ? ", new: " : "", fresh ? fresh : "");
    if (fresh) strcpy(pending, fresh);

    // Windows writes a copied file's data and FAT entries lazily, sometimes 30+ s after the copy
    // "finished"; every burst triggers another rescan, so wait (up to a limit) for the whole file.
    static uint32_t pending_since;
    if (fresh) pending_since = now_ms();
    if (pending[0]) {
        int i = find(pending);
        if (i >= 0 && !gif_store_complete(pending) && now_ms() - pending_since < PENDING_GIVE_UP_MS) {
            printf("waiting for the rest of %s\n", pending);
        } else {
            bool opened = i >= 0 && open_index(i);
            pending[0] = 0;
            if (opened) return;
        }
    }
    if (!playing[0]) {
        if (n && current < 0) step(+1);
        return;
    }
    int i = find(playing);
    if (i < 0) {
        printf("%s was removed\n", playing);
        current = -1;
        step(+1);
    } else if (remounted || gif_store_stamp(playing) != playing_stamp) {
        printf("%s was replaced\n", playing);
        open_index(i);
    } else {
        current = i;  // same file, possibly at a new position in the sorted list
    }
}

static void update_leds(uint32_t t) {
    // pushing the chain blocks for ~30 us per LED; ~30 updates a second is plenty
    static uint32_t last;
    if (t - last < 33) return;
    last = t;
    if (usb_busy) {
        leds_fill(0, 0, (t / 150) & 1 ? 90 : 10);  // blue blink while the host writes
        leds_show();
        return;
    }
    switch (led_mode) {
    case LED_AMBIENT: {
        // average a sparse grid of the frame (pixels are big-endian RGB565)
        const uint16_t *fb = display_framebuffer();
        uint32_t r = 0, g = 0, b = 0, n = 0;
        for (int y = 30; y < DISPLAY_HEIGHT - 30; y += 12)
            for (int x = 30; x < DISPLAY_WIDTH - 30; x += 12, n++) {
                uint16_t p = fb[y * DISPLAY_WIDTH + x];
                p = (uint16_t)(p >> 8 | p << 8);
                r += (p >> 11) << 3;
                g += ((p >> 5) & 0x3f) << 2;
                b += (p & 0x1f) << 3;
            }
        leds_fill((uint8_t)(r / n), (uint8_t)(g / n), (uint8_t)(b / n));
        break;
    }
    case LED_RAIN:
        for (int i = 0; i < leds_count(); i++) {
            int phase = (int)((t / 70 + (uint32_t)i * 3) % 12);
            uint8_t v = phase == 0 ? 255 : phase < 5 ? (uint8_t)(160 >> phase) : 0;
            leds_set(i, phase == 0 ? 120 : 0, v, phase == 0 ? 120 : 0);
        }
        break;
    default:
        leds_fill(0, 0, 0);
        break;
    }
    leds_show();
}

static void print_help(void) {
    printf("\nSC26 GIF player %s - commands:\n"
           "  n / p   next / previous GIF\n"
           "  l       list GIFs on the drive\n"
           "  space   pause / resume\n"
           "  + / -   backlight brighter / dimmer\n"
           "  m       LED mode (ambient / rain / off)\n"
           "  b       LED brightness\n"
           "  r       rescan the drive\n"
           "  i       info\n"
           "  t       button test (prints which buttons are held)\n"
           "Buttons: left/right = previous/next GIF, up/down = backlight,\n"
           "         centre = pause, A = LED mode, B = LED brightness\n",
           SC26_VERSION);
}

static void print_info(void) {
    int w, h;
    gif_player_canvas_size(&w, &h);
    printf("SC26 GIF player %s\n", SC26_VERSION);
    printf("now playing: %s (%dx%d)%s\n", playing[0] ? playing : "built-in matrix_rain.gif", w, h,
           paused ? " [paused]" : "");
    printf("GIFs on drive: %d, frames drawn: %lu, backlight %d%%, LEDs: %s (%d)\n", gif_store_count(),
           (unsigned long)frames_drawn, backlight, led_mode_names[led_mode], leds_count());
    printf("frame decode: avg %lu.%lu ms, max %lu.%lu ms (screen push ~7 ms, runs in the background)\n",
           (unsigned long)(decode_us_avg / 1000), (unsigned long)(decode_us_avg / 100 % 10),
           (unsigned long)(decode_us_max / 1000), (unsigned long)(decode_us_max / 100 % 10));
    printf("drive: %lu KB at flash offset 0x%x; host wrote %lu sectors, last LBA %lu\n",
           (unsigned long)(flash_disk_sector_count() / 2), FLASH_DISK_OFFSET,
           (unsigned long)flash_disk_host_sectors(), (unsigned long)flash_disk_host_last_lba());
    const struct flash_disk_stats *s = flash_disk_get_stats();
    printf("flash: %lu blocks written (%lu erased, %lu journaled), %lu pages programmed, %lu ms busy\n",
           (unsigned long)s->flushes, (unsigned long)s->erases, (unsigned long)s->journaled, (unsigned long)s->pages,
           (unsigned long)(s->busy_us / 1000));
}

static void handle_command(int c) {
    switch (c) {
    case 'h': case '?': print_help(); break;
    case 'n': step(+1); break;
    case 'p': step(-1); break;
    case 'l':
        for (int i = 0; i < gif_store_count(); i++) printf("%c %2d  %s\n", i == current ? '*' : ' ', i, gif_store_name(i));
        if (!gif_store_count()) printf("(no GIFs on the drive - playing the built-in one)\n");
        break;
    case ' ': paused = !paused; printf(paused ? "paused\n" : "playing\n"); break;
    case '+': backlight = backlight + 10 > 100 ? 100 : backlight + 10; display_set_backlight((uint8_t)backlight); printf("backlight %d%%\n", backlight); break;
    case '-': backlight = backlight - 10 < 5 ? 5 : backlight - 10; display_set_backlight((uint8_t)backlight); printf("backlight %d%%\n", backlight); break;
    case 'm': led_mode = (led_mode + 1) % LED_MODE_COUNT; printf("LEDs: %s\n", led_mode_names[led_mode]); break;
    case 'b': {
        static const uint8_t levels[] = {16, 48, 96, 160, 255};
        static int level = 2;
        level = (level + 1) % (int)count_of(levels);
        leds_set_brightness(levels[level]);
        printf("LED brightness %d/255\n", levels[level]);
        break;
    }
    case 'r': rescan_after_usb(); break;
    case 'i': print_info(); break;
    case 't':
        for (int i = 0; i < BUTTON_COUNT; i++)
            printf("  %-6s (GPIO%d): %s\n", button_name(i), button_gpio(i), buttons_held() & (1u << i) ? "PRESSED" : "up");
        break;
    default: break;
    }
}

static void handle_buttons(uint32_t pressed) {
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (!(pressed & (1u << i))) continue;
        printf("button %s (GPIO%d)\n", button_name(i), button_gpio(i));
        switch (i) {
        case BTN_RIGHT: handle_command('n'); break;
        case BTN_LEFT: handle_command('p'); break;
        case BTN_UP: handle_command('+'); break;
        case BTN_DOWN: handle_command('-'); break;
        case BTN_CENTER: handle_command(' '); break;
        case BTN_A: handle_command('m'); break;
        case BTN_B: handle_command('b'); break;
        }
    }
}

int main(void) {
    leds_init();
    status_led_put(true);
    buttons_init();
    display_init();
    display_set_backlight((uint8_t)backlight);
    gif_player_init(display_framebuffer(), DISPLAY_WIDTH, DISPLAY_HEIGHT);

    flash_disk_init();
    bool mounted = gif_store_mount();
    tusb_init();
    stdio_init_all();  // stdio_usb expects TinyUSB to be up already (it asserts so in Debug builds)
    if (!mounted) printf("gif_store: could not mount or format the drive\n");

    if (gif_store_count()) step(+1);
    else open_builtin();

    uint32_t last_usb_write = 0;
    for (;;) {
        tud_task();
        uint32_t t = now_ms();

        int c = getchar_timeout_us(0);
        if (c >= 0) handle_command(c);
        handle_buttons(buttons_poll());
        status_led_put((t / 500) & 1);

        // Host writes to the drive: keep playing, and rescan once the writes have stopped.
        uint32_t w = flash_disk_host_write_ms();
        if (w && t - w > FLASH_IDLE_FLUSH_MS) flash_disk_flush();
        if (w != last_usb_write) {
            last_usb_write = w;
            if (!usb_busy) printf("receiving (LBA %lu)...\n", (unsigned long)flash_disk_host_last_lba());
            usb_busy = true;
        }
        if (usb_busy && t - w >= USB_QUIET_MS) {
            usb_busy = false;
            flash_disk_flush();
            rescan_after_usb();
        }

        // While a write burst is streaming in, give USB the whole loop: decoding a frame takes
        // tens of ms, during which tud_task() would not run and the transfer would stall.
        bool streaming = usb_busy && t - w < USB_STREAMING_MS;
        update_leds(t);  // rate-limited inside; keeps LED modes live while paused too
        if (paused || streaming || (int32_t)(t - next_frame) < 0) continue;
        display_wait();
        int delay;
        uint64_t t0 = time_us_64();
        int rc = gif_player_next_frame(&delay);
        uint32_t took = (uint32_t)(time_us_64() - t0);
        decode_us_avg = decode_us_avg ? (decode_us_avg * 7 + took) / 8 : took;
        if (took > decode_us_max) decode_us_max = took;
        if (rc < 0) {
            printf("GIF error in %s: %s - skipping\n", playing[0] ? playing : "built-in", gif_player_error());
            step(+1);
            next_frame = t + 200;
            continue;
        }
        display_present();
        frames_drawn++;
        next_frame = t + (uint32_t)delay;
    }
}
