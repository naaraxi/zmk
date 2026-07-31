# OpenRGB direct control for the Keychron V6 Ultra 8K

This branch (`openrgb`) adds host-driven per-key RGB control to the Keychron V6
Ultra 8K firmware - implementing what [ZMK issue #893](https://github.com/zmkfirmware/zmk/issues/893)
asks for - plus the host tooling to build and flash it.

> **Personal project, shared as-is.** I built this for my own keyboard and put
> it on GitHub in case it's useful to someone else. No support, warranty, or
> maintenance is promised - use it at your own risk, and understand that
> flashing custom firmware is inherently do-it-yourself.

It rides the keyboard's existing raw-HID (VIA) channel with a new `id_openrgb`
(0x16) command: enter/exit a **direct mode** where the host owns the 108-LED
matrix, stream colors, and query the LED layout. When the host stops or the
keyboard goes wireless, control auto-hands-back to your onboard/Launcher
lighting after ~3 s. Caps/Num-Lock indicators stay visible on top.

The matching OpenRGB plugin lives in a separate repo:
**https://github.com/naaraxi/keychron_ultra_openrgb**

## What changed (vs upstream `rtl8762g`)
- `app/src/launcher/openrgb.c` - the 0x16 command handler.
- `app/src/rgb/rgb_matrix.c` - direct mode (effect bypass + auto-hand-back
  watchdog) and the lock-indicator overlay.
- `app/src/rgb/keychron/keychron_rgb.c` - `os_indicator_owns_led()` so host
  writes don't fight the caps/num overlay (no flicker).
- `Kconfig` / headers - `ZMK_OPENRGB`.

## Build
Needs Docker. From the repo root:

```bash
./openrgb/build.sh                       # keychron_v6_ultra_ansi
./openrgb/build.sh keychron_q6_ultra_ansi   # another Ultra shield
```

First run initializes the west workspace (clones zephyr/ + modules/ into the
repo) inside the toolchain container. Output: `openrgb/output/<shield>.bin` - an
OTA-flashable image with the same header format as Keychron's CDN firmware
(SHA-256 checksum, plaintext/unsigned - self-built images are accepted).

## Flash
The Keychron Launcher only flashes *official* images, so use the included host
flasher (Realtek SC-FWU over `/dev/hidraw`). It finds your keyboard on its own
and works on any Ultra board. Plug the keyboard in via USB:

```bash
sudo ./openrgb/flash.py handshake                       # read-only: identify only
sudo ./openrgb/flash.py flash openrgb/output/keychron_v6_ultra_ansi.bin
```

If you have two Keychron boards plugged in, it will stop and ask which one, so
it cannot flash the wrong keyboard by accident. Pass `--device=/dev/hidrawN` to
choose.

Flashing is **brick-safe**: the image is staged to a separate "OTA Tmp" bank and
only activated after the device verifies its CRC - an interrupted or bad flash
leaves the running firmware untouched. Recovery: the official 1.0.2 image is
still on Keychron's CDN and can be reflashed via the Launcher.

## Test
`openrgb_test.py` talks to the keyboard directly, so you can check the firmware
without installing OpenRGB. It works on any Ultra board, because it asks the
keyboard how many LEDs it has instead of assuming.

```bash
sudo ./openrgb/openrgb_test.py count    # just asks the LED count, nothing lights up
sudo ./openrgb/openrgb_test.py demo     # cycles red/green/blue/white across all keys
sudo ./openrgb/openrgb_test.py hold     # holds one color for a while
```

If `count` gets an answer, the OpenRGB firmware is running. Stock firmware does
not know the command and replies `0xFF`.

## Running without sudo (optional)
Both scripts talk to `/dev/hidraw`, which normally only root can open. If you
would rather not use `sudo` every time, install the udev rule included here:

```bash
sudo cp openrgb/61-keychron-ultra-openrgb.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Then unplug and replug the keyboard. This is optional - `sudo` works fine
without it. The same rule also lets OpenRGB reach the keyboard while running as
your normal user.

## Keeping up with upstream
```bash
git fetch origin && git merge origin/rtl8762g
```
