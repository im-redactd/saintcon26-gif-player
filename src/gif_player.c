// SPDX-License-Identifier: MIT AND Apache-2.0
// Copyright (c) 2026 redactd
// draw_turbo_frame() adapts the row/interlace loop of DecodeLZWTurbo() from AnimatedGIF's gif.inl
// (Copyright 2020 BitBank Software, Inc., Apache-2.0, see lib/AnimatedGIF/LICENSE).
#include "gif_player.h"

#include <math.h>
#include <string.h>

#include "ff.h"
#include "AnimatedGIF.h"
#include "gif.inl"  // AnimatedGIF's C implementation (static helpers live in this unit)

static GIFIMAGE gif;
static FIL gif_file;
static bool is_open;
static uint16_t *fb;
static int fb_w, fb_h;
// Canvas -> screen mapping: canvas coordinate c lands on screen origin + c * scale, with the scale
// in 16.16 fixed point (nearest neighbour both ways: canvas pixels can repeat or be dropped).
static int32_t scale_q16 = 1 << 16;
static int origin_x, origin_y;

static inline int to_screen(int c, int origin) {
    return origin + (int)(((int64_t)c * scale_q16) >> 16);
}

// Screen column where canvas column c starts, clipped to the screen (filled per GIF, so the
// line drawer does no per-pixel arithmetic when scaling).
static int16_t col_start[MAX_WIDTH + 1];

// Frame rectangles (canvas coordinates) for GIF disposal handling.
struct frame_rect {
    int x, y, w, h;
    uint8_t disposal;
};
static struct frame_rect cur_frame, prev_frame;
static bool read_failed;  // a file read failed during the current frame (e.g. the host rewrote it)

// AnimatedGIF's turbo decoder (several times faster) needs ~24 KB plus one byte per frame pixel.
// Canvases up to the screen size use it; bigger ones fall back to the line-by-line decoder.
#define TURBO_MAX_AREA 150000  // e.g. 360x360, 498x278, 480x300
static uint8_t turbo_buf[TURBO_BUFFER_SIZE + TURBO_MAX_AREA] __attribute__((aligned(4)));

static int32_t fatfs_read(GIFFILE *f, uint8_t *buf, int32_t len) {
    int32_t remain = f->iSize - f->iPos;
    if (len > remain) len = remain;
    if (len <= 0) return 0;
    UINT got = 0;
    if (f_read((FIL *)f->fHandle, buf, (UINT)len, &got) != FR_OK) {
        read_failed = true;  // the library would read 0 bytes as a plain EOF
        return 0;
    }
    f->iPos += (int32_t)got;
    return (int32_t)got;
}

static int32_t fatfs_seek(GIFFILE *f, int32_t pos) {
    if (pos < 0) pos = 0;
    if (pos > f->iSize) pos = f->iSize;
    if (f_lseek((FIL *)f->fHandle, (FSIZE_t)pos) != FR_OK) return f->iPos;
    f->iPos = pos;
    return pos;
}

static void fatfs_close(void *handle) {
    f_close((FIL *)handle);
}

// Called by AnimatedGIF for every decoded line of the current frame.
static void draw_line(GIFDRAW *d) {
    uint8_t *src = d->pPixels;
    const uint16_t *pal = d->pPalette;
    int cy = d->iY + d->y;  // canvas row

    if (d->y == 0)  // first line: remember where this frame is, for its disposal later
        cur_frame = (struct frame_rect){d->iX, d->iY, d->iWidth, d->iHeight, d->ucDisposalMethod};

    if (scale_q16 == 1 << 16) {  // 1:1, the common case for GIFs made for this screen
        int py = origin_y + cy;
        if (py < 0 || py >= fb_h) return;
        int x0 = origin_x + d->iX;
        int a = x0 < 0 ? -x0 : 0, b = x0 + d->iWidth > fb_w ? fb_w - x0 : d->iWidth;
        uint16_t *row = fb + py * fb_w + x0;
        if (d->ucHasTransparency) {
            uint8_t t = d->ucTransparent;
            for (int x = a; x < b; x++)
                if (src[x] != t) row[x] = pal[src[x]];
        } else {
            for (int x = a; x < b; x++) row[x] = pal[src[x]];
        }
        return;
    }

    // screen rows covered by this canvas row (none when shrinking and the row is dropped)
    int y0 = to_screen(cy, origin_y), y1 = to_screen(cy + 1, origin_y);
    if (y0 < 0) y0 = 0;
    if (y1 > fb_h) y1 = fb_h;
    if (y0 >= y1) return;

    const int16_t *cols = col_start + d->iX;
    uint16_t *top = fb + y0 * fb_w;
    for (int x = 0; x < d->iWidth; x++) {
        int a = cols[x], b = cols[x + 1];
        uint8_t idx = src[x];
        if (a >= b || (d->ucHasTransparency && idx == d->ucTransparent)) continue;
        uint16_t c = pal[idx];
        for (uint16_t *row = top; row < fb + y1 * fb_w; row += fb_w)
            for (int px = a; px < b; px++) row[px] = c;
    }
}

// Square-ish GIFs fill the round screen; wide or tall ones are fitted inside the circle (with a
// little corner clipping allowed) so the whole scene stays visible. Big upscales snap to whole
// multiples so pixel art stays crisp.
static void layout_canvas(void) {
    int cw = gif.iCanvasWidth, ch = gif.iCanvasHeight;
    double s = 1.0;
    if (cw > 0 && ch > 0) {
        double box = (double)fb_w / cw < (double)fb_h / ch ? (double)fb_w / cw : (double)fb_h / ch;
        double aspect = (double)cw / ch;
        s = box;
        if (aspect < 0.85 || aspect > 1.18) {
            double circle = 1.1 * fb_w / sqrt((double)cw * cw + (double)ch * ch);
            if (circle < s) s = circle;
        }
        if (s >= 2.0) s = floor(s);
        if (s > 6.0) s = 6.0;
    }
    scale_q16 = (int32_t)(s * 65536.0);
    origin_x = (fb_w - (int)(cw * s)) / 2;
    origin_y = (fb_h - (int)(ch * s)) / 2;
    for (int c = 0; c <= cw && c <= MAX_WIDTH; c++) {
        int x = to_screen(c, origin_x);
        col_start[c] = (int16_t)(x < 0 ? 0 : x > fb_w ? fb_w : x);
    }
    memset(fb, 0, (size_t)fb_w * fb_h * sizeof(uint16_t));
    prev_frame.disposal = 0;
    gif.pTurboBuffer = (long)cw * ch <= TURBO_MAX_AREA ? turbo_buf : NULL;
}

// In turbo mode the library decodes the whole frame (8-bit indices, rows in file order) into the
// turbo buffer and only calls back for its own "cooked" output; feed our line drawer from it.
// (Adapted from DecodeLZWTurbo() in AnimatedGIF's gif.inl, Apache-2.0.)
static void draw_turbo_frame(void) {
    GIFDRAW d = {0};
    d.iX = gif.iX;
    d.iY = gif.iY;
    d.iWidth = gif.iWidth;
    d.iHeight = gif.iHeight;
    d.pPalette = gif.bUseLocalPalette ? gif.pLocalPalette : gif.pPalette;
    d.ucTransparent = gif.ucTransparent;
    d.ucHasTransparency = gif.ucGIFBits & 1;
    d.ucDisposalMethod = (gif.ucGIFBits & 0x1c) >> 2;
    d.ucBackground = gif.ucBackground;
    int last = gif.iHeight - 1;
    for (int y = 0; y < gif.iHeight; y++) {
        int row = y;
        if (gif.ucMap & 0x40) {  // interlaced: rows arrive in 4 passes (same mapping as the library)
            if (row > last / 2) row = row * 2 - (last | 1);
            else if (row > last / 4) row = row * 4 - ((last & ~1) | 2);
            else if (row > last / 8) row = row * 8 - ((last & ~3) | 4);
            else row = row * 8;
        }
        d.y = row;
        d.pPixels = &turbo_buf[y * gif.iWidth];
        draw_line(&d);
    }
}

void gif_player_init(uint16_t *framebuffer, int width, int height) {
    fb = framebuffer;
    fb_w = width;
    fb_h = height;
}

bool gif_player_open_file(const char *path) {
    gif_player_close();
    if (f_open(&gif_file, path, FA_READ) != FR_OK) return false;
    GIF_begin(&gif, GIF_PALETTE_RGB565_BE);
    gif.pfnRead = fatfs_read;
    gif.pfnSeek = fatfs_seek;
    gif.pfnDraw = draw_line;
    gif.pfnClose = fatfs_close;
    gif.GIFFile.fHandle = &gif_file;
    gif.GIFFile.iSize = (int32_t)f_size(&gif_file);
    if (!GIFInit(&gif)) {
        f_close(&gif_file);
        return false;
    }
    is_open = true;
    layout_canvas();
    return true;
}

bool gif_player_open_memory(const uint8_t *data, size_t len) {
    gif_player_close();
    GIF_begin(&gif, GIF_PALETTE_RGB565_BE);
    if (!GIF_openRAM(&gif, (uint8_t *)data, (int)len, draw_line)) return false;
    is_open = true;
    layout_canvas();
    return true;
}

void gif_player_close(void) {
    if (is_open) GIF_close(&gif);
    is_open = false;
}

// Fills a canvas rectangle (in canvas coordinates) on screen with black.
static void clear_canvas_rect(int x, int y, int w, int h) {
    int x0 = to_screen(x, origin_x), x1 = to_screen(x + w, origin_x);
    int y0 = to_screen(y, origin_y), y1 = to_screen(y + h, origin_y);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > fb_w) x1 = fb_w;
    if (y1 > fb_h) y1 = fb_h;
    for (int py = y0; py < y1; py++)
        if (x1 > x0) memset(fb + py * fb_w + x0, 0, (size_t)(x1 - x0) * sizeof(uint16_t));
}

int gif_player_next_frame(int *delay_ms) {
    *delay_ms = 0;
    if (!is_open) return -1;

    // Disposal of the previous frame happens before this one is drawn. "Restore to background"
    // clears its rectangle (black is the background here, like a page behind a browser GIF);
    // "restore to previous" (3) is treated as "leave in place", which most GIFs tolerate.
    if (prev_frame.disposal == 2)
        clear_canvas_rect(prev_frame.x, prev_frame.y, prev_frame.w, prev_frame.h);
    prev_frame.disposal = 0;
    if (gif.GIFFile.iPos >= gif.GIFFile.iSize - 1)  // about to wrap: the canvas starts out blank
        clear_canvas_rect(0, 0, gif.iCanvasWidth, gif.iCanvasHeight);

    gif.iError = GIF_SUCCESS;  // the library never clears it between frames
    read_failed = false;
    int rc = GIF_playFrame(&gif, delay_ms, NULL);
    if (rc == 0 && gif.iError == GIF_EMPTY_FRAME) {
        // trailing extension blocks or junk after the last image: loop back to the first frame
        gif.iError = GIF_SUCCESS;
        GIF_reset(&gif);
        clear_canvas_rect(0, 0, gif.iCanvasWidth, gif.iCanvasHeight);
        rc = GIF_playFrame(&gif, delay_ms, NULL);
    }
    if (gif.iError != GIF_SUCCESS || read_failed) return -1;
    // a frame that ran out of file before its end-of-data block is truncated: don't show it
    if (!gif.bEndOfFrame && gif.GIFFile.iPos >= gif.GIFFile.iSize) {
        gif.iError = GIF_EARLY_EOF;
        return -1;
    }
    if (gif.pTurboBuffer) draw_turbo_frame();
    prev_frame = cur_frame;
    if (*delay_ms <= 10) *delay_ms = 100;  // what browsers do with 0/10 ms delays
    return rc;
}

void gif_player_canvas_size(int *width, int *height) {
    *width = is_open ? gif.iCanvasWidth : 0;
    *height = is_open ? gif.iCanvasHeight : 0;
}

const char *gif_player_error(void) {
    static const char *const names[] = {"ok", "decode error", "too wide", "invalid parameter",
                                        "unsupported feature", "file not open", "early EOF",
                                        "empty frame", "bad file", "out of memory"};
    int e = gif.iError;
    return (e >= 0 && e < (int)(sizeof names / sizeof names[0])) ? names[e] : "unknown";
}
