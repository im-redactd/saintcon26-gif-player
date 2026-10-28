# SC26 badge GIF player

Firmware for the **SC26 badge**: an RP2350 board with a 360×360 round LCD, 9 RGB LEDs and a
D-pad. It plays animated GIFs and matches the LEDs to the picture. Plug it into a computer and
it shows up as a USB drive: drop GIFs on it and they start playing.

* Plays GIFs up to 1024 px wide, decoding faster than real time. Square GIFs fill the round
  screen; wide or tall ones are fitted inside the circle.
* 15 MB GIF drive in the on-board flash, exposed over USB mass storage. The boot sector, FAT and
  directory blocks are journaled, so pulling the cable mid-write can't leave the drive
  unformatted (a half-copied file may still need deleting).
* WS2812 LEDs follow the average colour of the frame ("ambient"), run a green rain animation,
  or turn off.
* USB serial console for control and diagnostics.
* `picotool` can reboot and reflash the badge from the computer, with no button pressing.
* Comes with original hacker-con animations (digital rain, a HACK THE PLANET globe, the DNS
  haiku, an ACCESS GRANTED terminal, radar) and tools to convert your own GIFs.

Hardware details and the full pin map are in [docs/HARDWARE.md](docs/HARDWARE.md).

## Try it without compiling

Every [release](../../releases) has a prebuilt **`sc26-gif-player-<version>.uf2`**:

1. Put the badge in BOOTSEL mode. If it already runs this firmware, or any Pico SDK USB firmware,
   open its serial port at **1200 baud** (or run `picotool reboot -u -f`). Otherwise hold BOOTSEL
   while plugging it in. A drive called `RP2350` appears.
2. Copy the `.uf2` onto it. The badge reboots and starts playing. On the very first boot it formats
   the GIF drive (about 5 s); after that a drive called `SC26GIFS` appears.

The optional `sc26-demo-gifs-<version>.uf2` loads all the animations from `gifs/` onto the drive the
same way. It **replaces** the drive's contents, and should only be used on this badge (see Security
notes). Checksums are in `SHA256SUMS`.

## Using it

| Control | Action |
|---|---|
| D-pad left / right | previous / next GIF |
| D-pad up / down | screen brightness |
| D-pad centre | pause / resume |
| A | LED mode: ambient → rain → off |
| B | LED brightness |

Down, A and B are mapped from the badge's original firmware but have not been confirmed on
hardware yet. The console's `t` command shows what each button reads; please report what you see.

**Adding GIFs:** copy `.gif` files onto the `SC26GIFS` drive. When the copy finishes, the badge
rescans the drive and jumps to the new file. Writes go to NOR flash at roughly 60-150 KB/s, so a
few MB takes a minute or two; the LEDs blink blue while the computer is writing.

**Limits:**
* Only `.gif` files in the drive's top-level folder are played, at most 64, sorted by name.
  Extras are ignored.
* File names can be up to 95 characters.
* A GIF loops until you press left or right; there is no auto-advance.
* After copying several files at once, the badge jumps to the first new one by name.
* Only `matrix_rain.gif` is on the drive after the first boot. The other animations in `gifs/` come
  with the demo pack, or you can copy them on.

**Console:** open the badge's serial port at any baud rate except 1200 (which reboots it into
BOOTSEL) and press `h`:

```
n / p   next / previous GIF        m   LED mode
l       list GIFs                  b   LED brightness
space   pause / resume             r   rescan the drive
+ / -   backlight                  i   info: version, decode time, flash stats
t       button test
```

## Building

You need:

* The [Pico SDK](https://github.com/raspberrypi/pico-sdk) **2.3.0 or newer** (tested with 2.3.1),
  with its `lib/tinyusb` submodule.
* An Arm GNU toolchain (`arm-none-eabi-gcc`). Developed with Arm GNU Toolchain 14.2; CI uses
  Ubuntu 24.04's packaged 13.2.
* CMake 3.17 or newer, and Ninja or Make.
* A host C/C++ compiler. If `picotool` 2.3+ and `pioasm` aren't installed, the SDK fetches and
  builds them for its own use (UF2 conversion), which needs network access. That copy of picotool
  has no USB support; see Flashing.

The [Raspberry Pi Pico VS Code extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico)
installs all of this. By hand on Linux or macOS:

```sh
git clone --branch 2.3.1 --depth 1 https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
git -C ~/pico-sdk submodule update --init --depth 1 lib/tinyusb
export PICO_SDK_PATH=~/pico-sdk
```

On Windows (PowerShell):

```powershell
git clone --branch 2.3.1 --depth 1 https://github.com/raspberrypi/pico-sdk.git $HOME\pico-sdk
git -C $HOME\pico-sdk submodule update --init --depth 1 lib/tinyusb
$env:PICO_SDK_PATH = "$HOME\pico-sdk"
```

Then, from this folder:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                       # -> build/sc26_gif.uf2
```

Alternatively, pass `-DPICO_SDK_FETCH_FROM_GIT=ON -DPICO_SDK_FETCH_FROM_GIT_TAG=2.3.1` instead of
setting `PICO_SDK_PATH`. This downloads every SDK submodule; on Windows run
`git config --global core.longpaths true` first. `-DSC26_VERSION=...` sets the version string that
`i` and `picotool info` report (default `dev`). In Windows PowerShell, quote any `-D` argument
whose value has dots, for example `"-DSC26_VERSION=v1.0.0"`; otherwise PowerShell 5.1 splits it.

## Flashing

* **Drag and drop:** put the badge in BOOTSEL mode (see "Try it without compiling") and copy
  `build/sc26_gif.uf2` onto the `RP2350` drive.
* **picotool:** `picotool load -x build/sc26_gif.uf2 -f` reboots and flashes in one go. Add
  `--ser <serial>` if more than one RP2xxx board is plugged in; `picotool info -a` shows the
  serials. This needs a picotool built with USB support, for example from the VS Code extension or
  a [picotool release](https://github.com/raspberrypi/picotool/releases).

Flashing new firmware leaves the GIF drive alone.

## Releases

Push a version tag and GitHub Actions does the rest:

```sh
git tag v1.0.0
git push origin v1.0.0
```

[`release.yml`](.github/workflows/release.yml) builds that commit with the tag as `SC26_VERSION`,
runs the tests, and publishes a GitHub Release. The release contains the firmware UF2, the demo GIF
pack, a zip of license notices and `SHA256SUMS`, with the notes taken from
[`.github/release-notes.md`](.github/release-notes.md). Tags with a hyphen (`v1.1.0-rc1`) become
pre-releases.

To make the same files locally:
`python tools/package_release.py --version v1.0.0 --uf2 build/sc26_gif.uf2 --out dist`.
Attach only those files to a release, never a locally built `.elf`: its debug info contains your
local paths.

## Tools

Python 3.9+ with Pillow: `pip install -r tools/requirements.txt`.

| Tool | What it does |
|---|---|
| `tools/badgeify.py in.gif out.gif` | Turns any GIF into a tidy 360×360 version for the round screen: centre crop (or `--fit` for captioned or wide clips), corners blacked out, one shared palette, and only changed pixels stored per frame (typically 40-60% smaller). |
| `tools/gif2uf2.py out.uf2 a.gif b.gif …` | Packs GIFs into a complete FAT16 drive image as a UF2. Dropping it on the `RP2350` boot drive replaces the GIF drive's contents and leaves the firmware alone. |
| `tools/package_release.py` | Builds the release files (see Releases). |
| `python tools/make_test_gifs.py gifs`, `python tools/make_cyber_gifs.py gifs [regular.ttf bold.ttf]` | Regenerate the animations in `gifs/`. The committed cyber GIFs were rendered with Consolas; other fonts give slightly different files. |

Tests: `python -m unittest discover -s tests -v`.

## How it works

Everything runs on core 0 in one loop: TinyUSB, the serial console, buttons, LEDs, and the GIF
frame scheduler.

* **Display:** a two-instruction PIO program drives the ST77916's 4-bit QSPI bus at 75 MHz, and
  DMA streams the 360×360 RGB565 framebuffer (259 KB) in about 7 ms. Frames are decoded by
  [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF), using its turbo decoder when the canvas
  fits a 150,000-pixel work area. Decoding writes straight into the framebuffer, with
  nearest-neighbour scaling, transparency and GIF disposal. "Restore to background" clears to
  black; "restore to previous" is treated as "leave in place".
* **Storage:** the drive is a FAT16 volume in flash from offset 1 MB. One 4 KB write-back cache
  serves both USB mass storage (the host's view) and [FatFs](http://elm-chan.org/fsw/ff/) (the
  firmware's view, read-only after formatting). Writes are committed at the end of every USB
  WRITE command. Blocks are only erased when a bit has to go 0→1, and only changed pages are
  programmed.
* **Power-loss safety:** each 4 KB block in the drive's first 64 KB (boot sector, FATs, root
  directory) is written through a small write-ahead journal, and an interrupted write is finished
  at the next boot.
* **New files:** when the host stops writing, the badge rescans the directory. Windows writes FAT
  entries lazily, so it waits until a new GIF ends with its trailer byte before playing it. It also
  notices files that are replaced or deleted, and a host-side reformat.

### Flash and RAM

| Flash range | Use |
|---|---|
| `0x10000000`-`0x100FDFFF` | firmware (≈420 KB, including the built-in GIF). The linker refuses anything bigger. |
| `0x100FE000`-`0x100FFFFF` | metadata journal (8 KB) |
| `0x10100000`-end of flash | FAT16 GIF drive (15 MB on the 16 MB part) |

RAM use is about 490 KB of 520 KB: 259 KB framebuffer, 175 KB GIF turbo buffer (24 KB of decoder
tables plus the 150,000-pixel frame area), and the rest for decoder state, USB and FatFs.

## Security notes

See [SECURITY.md](SECURITY.md) for how to report problems privately.

* **Anyone with USB access can reflash it.** The firmware keeps the standard Pico SDK reset paths
  (`picotool` reset interface, 1200-baud touch), and the RP2350 boot ROM always accepts UF2 files.
  Treat USB access as full control.
* **The badge parses untrusted files.** GIFs come from the host. The decoder rejects canvases wider
  than 1024 px and frames that extend outside the canvas, drawing is clipped to the screen, and
  malformed or truncated files are skipped.
* **Images from `gif2uf2.py` (including the demo pack) work on any RP2350.** They use the boot ROM's
  absolute-address family, so on another RP2350 board they would overwrite flash from 1 MB - 8 KB
  onward. Only drop them on this badge.
* **The USB IDs are Raspberry Pi's.** The firmware uses Raspberry Pi's VID and the Pico SDK's
  default PID (`2E8A:0009`) so stock `picotool` works. See
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) if you distribute hardware.

## License

The code in this repository is MIT ([LICENSE](LICENSE)), except the parts listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md): the vendored libraries in `lib/`, the WS2812 PIO
program, `pico_sdk_import.cmake`, code adapted from the Pico SDK and AnimatedGIF, and the panel's
register init values. Each keeps its own license, and the license texts are in `lib/` and
`LICENSES/`. The animations in `gifs/` are original and also MIT. Please keep film and TV clips out
of `gifs/`; `.gitignore` already ignores `private/` and `gifs/private/` for those.
