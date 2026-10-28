# SC26 badge hardware

Custom board around a Raspberry Pi **RP2350A** (QFN-60, dual Cortex-M33, 520 KB SRAM) with
**16 MB** QSPI flash, a 12 MHz crystal and USB. There are no published schematics. This map was
recovered from the badge's original firmware. With this firmware on real hardware, the display,
the WS2812 LEDs, and the up/left/right/centre buttons have been confirmed; down, A and B are
mapped but have not been seen pressed yet.

## GPIO map

QFN-60 pin numbers count counter-clockwise from the corner dot: left edge 1-15, bottom 16-30,
right 31-45, top 46-60.

| GPIO | QFN pin | Function | Direction / pull |
|---:|---:|---|---|
| 0 | 2 | UART0 TX, to the dock connector (not used by this firmware) | out |
| 1 | 3 | UART0 RX, to the dock connector (not used by this firmware) | in |
| 2, 3 | 4, 5 | not used by any known firmware | - |
| 4 | 7 | I2C0 SDA, 1 KB 24C08-class EEPROM at 0x50-0x53 | open drain, internal pull-up |
| 5 | 8 | I2C0 SCL | open drain, internal pull-up |
| 6 | 9 | not used | - |
| 7 | 10 | LCD backlight, PWM slice 3 channel B (higher duty = brighter) | out |
| 8 | 12 | from the LCD connector, probably panel TE (tearing effect); never read | in, pull-up |
| 9 | 13 | LCD reset, active low | out |
| 10 | 14 | LCD QSPI SCLK | out |
| 11-14 | 15-18 | LCD QSPI D0-D3 (must stay consecutive for the PIO) | out |
| 15 | 19 | LCD chip select, active low | out |
| 16 | 27 | button **B** | in, pull-up, active low |
| 17 | 28 | button **A** | in, pull-up, active low |
| 18 | 29 | single LED (the original firmware blinks it at ~1 Hz) | out |
| 19 | 31 | D-pad **right** | in, pull-up, active low |
| 20 | 32 | D-pad **centre** press | in, pull-up, active low |
| 21 | 33 | D-pad **down** | in, pull-up, active low |
| 22 | 34 | D-pad **left** | in, pull-up, active low |
| 23 | 35 | D-pad **up** | in, pull-up, active low |
| 24 | 36 | WS2812 data, 9 LEDs | out |
| 25 | 37 | not used | - |
| 26-28 | 40-42 | not used (ADC-capable) | - |
| 29 | 43 | not used by this firmware | - |

The display pins wrap around the chip's bottom-left corner (QFN pins 10-19); that is where the
display's 16-pin FPC traces land.

## Display

* **Panel:** 360×360 round colour LCD, **Sitronix ST77916** controller, RGB565, no row/column
  offset, display inversion on (`0x21`), MADCTL `0x00`. Brightness comes from the GPIO7 PWM, not
  from a controller command.
* **Bus:** 4-bit QSPI, SPI mode 0, write-only. The original firmware and this one both run SCLK at
  **75 MHz** (clk_sys 150 MHz, PIO divider 1).
* **Reset:** RESET low 100 ms, high, wait 100 ms, then send the init sequence
  ([`src/st77916_init.h`](../src/st77916_init.h), 195 commands; the only delays are 10 ms after
  each `0x4C` and 120 ms after `0x21`, `0x11` and `0x29`).

### Wire format

Every transaction is framed by CS low/high:

| Transaction | Lines | Bytes |
|---|---|---|
| Register write | all on D0 (1-bit) | `02` `00` *cmd* `00` *params…* |
| Pixel write | header on D0, then pixels on D0-D3 (4-bit) | `32` `00` `2C` `00`, then RGB565 pixels, high byte first |

Before each pixel write, set the window with CASET (`0x2A`) and RASET (`0x2B`), both as plain
register writes.

### PIO implementation

A two-instruction PIO program clocks one nibble every two PIO cycles:

```
.side_set 1                ; SCLK
out pins, 4    side 0      ; D0..D3 change while SCLK is low
nop            side 1      ; panel samples on the rising edge
```

Settings: autopull at 8 bits, shift left, TX FIFO joined. A byte written to the TX FIFO goes out as
two nibbles, high nibble first. Single-line (1-bit) bytes are sent by spreading each bit into its
own nibble with only D0 set, so one byte becomes four FIFO bytes. Pixel data is DMA'd byte by byte
from the framebuffer. CS is released only after the state machine has stalled on an empty FIFO, so
the last SCLK edge has already happened.

## LEDs

* **9 × WS2812** on GPIO24, GRB order, 800 kHz, 24 bits per LED (no white channel).
* **1 single-colour LED** on GPIO18.

## Buttons

A 5-way D-pad (up/down/left/right/centre) plus A and B. Each is a switch to GND and needs the
RP2350's internal pull-up. In the original firmware's input mask the order is
up, down, left, right, centre, B, A (bits 0-6); `src/buttons.h` keeps that order.

## Memory

* **Flash:** 16 MB, W25Q-compatible (JEDEC `0x9F` reports the size; this firmware checks it at boot).
* **EEPROM:** 1 KB on I2C0. It belongs to the badge's game firmware and this firmware never accesses it.

## Not yet known

* The panel's exact part number and the 16-pin FPC pinout.
* Whether GPIO8 really is TE: send `0x35` and put a scope on it.
* What is physically on GPIO18 (an LED is the best guess).
* Whether the board has external I2C pull-ups.
