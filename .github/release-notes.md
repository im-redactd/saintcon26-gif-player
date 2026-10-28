Prebuilt firmware for the SC26 badge, version **{{VERSION}}**. No toolchain is needed to try it.

## Flash it

1. **Put the badge in BOOTSEL mode.** Use any one of:
   - If it already runs this firmware (or any Pico SDK USB firmware): open its serial port at
     **1200 baud** and close it again, or run `picotool reboot -u -f`.
   - Hold BOOTSEL while plugging it in (if your board has the button).

   A drive called **`RP2350`** appears.
2. **Copy `sc26-gif-player-{{VERSION}}.uf2`** onto the `RP2350` drive. The badge reboots and starts
   playing. On the very first boot it formats its 15 MB GIF drive (about 5 seconds), and then a USB
   drive called **`SC26GIFS`** appears.
3. **Add GIFs:** copy `.gif` files onto `SC26GIFS`. 360×360 looks best; anything up to 1024 px wide
   plays.

Flashing a new version keeps the GIFs that are already on the badge.

## Optional: demo GIF pack

`sc26-demo-gifs-{{VERSION}}.uf2` holds the {{GIF_COUNT}} original animations from the repository's
`gifs/` folder (digital rain, HACK THE PLANET, the DNS haiku, ACCESS GRANTED, radar, …). Copy it
onto the `RP2350` drive **after** the firmware.

> ⚠️ This **replaces everything on the badge's GIF drive**. Only use it on an SC26 badge: the boot ROM
> accepts it on any RP2350, and on another board it would overwrite flash from 1 MB - 8 KB onward.

## Controls

D-pad left/right: previous/next GIF · up/down: brightness · centre: pause · A: LED mode · B: LED
brightness. Serial console (any baud except 1200): press `h`. Down, A and B are mapped from the
badge's original firmware but have not been confirmed on hardware yet; the console's `t` command
shows what each button reads.

## Verify the download

```
sha256sum -c SHA256SUMS                                     # Linux / macOS
Get-FileHash -Algorithm SHA256 sc26-gif-player-*.uf2         # Windows
```

These binaries include third-party code: the Pico SDK (BSD-3-Clause), TinyUSB (MIT) and
AnimatedGIF (Apache-2.0). The notices are in `sc26-gif-player-{{VERSION}}-licenses.zip`.
