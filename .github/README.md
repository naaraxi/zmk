# Keychron Ultra ZMK firmware (OpenRGB fork)

Fork of [Keychron/zmk](https://github.com/Keychron/zmk) that adds host-driven
per-key RGB control (OpenRGB "direct" mode, ZMK issue #893) to the Keychron
Ultra keyboards, plus the host tooling to build and flash it.

Matching OpenRGB plugin: https://github.com/naaraxi/keychron_ultra_openrgb

> Personal project, shared as-is. No support or warranty is promised, and
> flashing custom firmware is inherently do-it-yourself. Use at your own risk.

## Supported keyboards

Each keyboard is a ZMK "shield" built on the `keychron` board. The Ultra boards
carry the per-key RGB matrix that OpenRGB drives:

| Shield                    | Keyboard          | LEDs |
|---------------------------|-------------------|------|
| keychron_v6_ultra_ansi    | V6 Ultra          | 108  |
| keychron_q6_ultra_ansi    | Q6 Ultra          | 108  |
| keychron_v5_ultra_ansi    | V5 Ultra          | 97   |
| keychron_v10_ultra_ansi   | V10 Ultra         | 88   |
| keychron_v3_ultra_ansi    | V3 Ultra          | 87   |
| keychron_q3_ultra_ansi    | Q3 Ultra          | 87   |
| keychron_v1_ultra_jis     | V1 Ultra (JIS)    | 86   |
| keychron_v1_ultra_iso     | V1 Ultra (ISO)    | 83   |
| keychron_v1_ultra_ansi    | V1 Ultra          | 82   |
| keychron_q1_ultra_ansi    | Q1 Ultra          | 82   |
| keychron_z270_ultra_ansi  | Z2-70 Ultra       | 75   |
| keychron_v2_ultra_ansi    | V2 Ultra          | 67   |
| keychron_v0_ultra_ansi    | V0 Ultra          | 26   |

Two non-Ultra shields also build but have no per-key RGB matrix, so OpenRGB does
not apply to them: `keychron_k3se2_ansi`, `keychron_k5se2_ansi`.

## Build

Requires Docker (uses ZMK's CI toolchain image; nothing is installed on the
host). From the repo root:

    ./openrgb/build.sh                          # default: keychron_v6_ultra_ansi
    ./openrgb/build.sh keychron_q6_ultra_ansi   # any shield from the table above

The first run creates the west workspace inside the repo (clones zephyr/ and
modules/). Output is an OTA-flashable image at `openrgb/output/<shield>.bin`,
with the same header format as Keychron's official firmware.

If you drive west yourself instead of the script, the equivalent command is:

    west build -s app -p -b keychron -d app/build -- -DSHIELD=<shield>

That produces `app/build/zephyr/zmk.bin`. The board is `keychron` (it defines
the RTK_DFU option the Ultra shields need), and the build dir must be
`app/build` so the OTA header step finds the image.

## Flash

The Keychron Launcher only flashes official images, so use the included host
flasher (Realtek SC-FWU over /dev/hidraw, needs root). Plug the keyboard in via
USB, then run the flasher on the built image. See `openrgb/README.md` for the
exact flash command, recovery steps, and the DFU bootloader details.

## More

`openrgb/README.md` has the full detail: what changed versus upstream, the 0x16
protocol, the auto hand-back behavior, the lock-indicator overlay, and recovery.
