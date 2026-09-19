# Flappy Accel

Flappy Microchip, played by moving the board. An **Accel 11 Click** (Bosch
BMA456) on the mikroBUS header feeds motion into the game: a sharp flick of the
board makes the player flap, exactly like pressing SW2. SW2 still works, so the
game is playable with or without the Click attached.

This project is a copy of the `pic64_smp_hello` demo from
[flappy-microchip](../flappy-microchip/) plus one new module, and it reuses that
example's VDMA/HDMI drivers and devicetree bindings in place. The sensor code
comes from [i2c-accel11](../i2c-accel11/); read its README for the wiring, the
BMA456 chip ID issue and the two PIC64GX I2C driver quirks.

## What changed against the Flappy source

| File | Change |
| ---- | ------ |
| `src/accel11.c`, `src/accel11.h` | New. Reader thread: finds the BMA456 on i2c0/i2c1 at 0x18/0x19, samples at 100 Hz, raises a flap event on a flick. |
| `src/screen_game.c` | `flap = SW2 edge OR accel11_take_flap()`. Flicks are ignored for 1 s after a crash so the player does not restart by accident. On screen texts mention the flick. |
| `src/main.c` | Calls `accel11_start()` before `ui_start()`. |
| `prj.conf` | `CONFIG_I2C=y`. |
| `CMakeLists.txt`, `app.overlay` | Point at `../flappy-microchip/source` for drivers and DTS. |

## How a flick is detected

The reader looks at the size of the acceleration vector, not at one axis, so it
does not matter how the Click is oriented. At rest it is 1 g. A flick pushes it
more than `FLICK_THRESHOLD_MG` (600 mg) above or below 1 g. After one flap,
further flicks are ignored for `FLICK_HOLDOFF_MS` (250 ms), because a flick has a
push phase and a braking phase that would both count. Both constants are at the
top of `src/accel11.c`.

Noise handling, in the order a sample meets it:

1. **Bad reads are dropped.** A read that returns all zeros or all ones is a bus
   glitch, not a sample, and is discarded. Twenty failures in a row stop the
   reader.
2. **Median of three.** The threshold is judged on the median of the last three
   magnitudes, so a flick must show in two consecutive samples (20 ms). A single
   spike from a bus glitch or a knock is ignored, and there is no added lag.
3. **Hold-off.** One flap per flick, as above.

The game tick runs every 33 ms and the sensor thread every 10 ms, so a flick is
seen within one game frame.

## Build

```bash
export ZEPHYR_BASE=$HOME/pic64gx-zephyr/zephyr
export ZEPHYR_SDK_INSTALL_DIR=$HOME/zephyr-sdk-1.0.1
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr

$HOME/pic64gx-zephyr/.venv/bin/west build \
  -b pic64gx_curiosity_kit/pic64gx1000/u54/smp \
  -d $HOME/pic64gx-zephyr/build/flappy-accel11 -p always \
  examples/flappy-accel11

./setup/linux/payload.sh \
  --build-dir $HOME/pic64gx-zephyr/build/flappy-accel11 \
  --output examples/flappy-accel11/payload.bin
./setup/linux/flash.sh --list
```

## Console

mmuart1 at 115200 8N1. On boot you should see one of:

```
[accel11] BMA456 on i2c0 at 0x18
[accel11] streaming, flick threshold 600 mg
```
```
[accel11] no BMA456 found, SW2 is the only input
```

## Tuning

Set `ACCEL11_DEBUG_PRINT` to 1 in `src/accel11.c` and watch the console. Note the
magnitude at rest (about 1000 mg), while handling the board, and during a flick,
then put `FLICK_THRESHOLD_MG` between "handling" and "flick". Turn it off again
afterwards, printing at 115200 baud is slow.

- Flaps fire when you only handle the board: raise `FLICK_THRESHOLD_MG`.
- You have to shake it hard: lower `FLICK_THRESHOLD_MG`.
- One flick gives two flaps: raise `FLICK_HOLDOFF_MS`.

## Verification status

**Not built and not run.** Devicetree configuration of this project succeeds
against a local Zephyr tree, but the full build was not possible here (no west
workspace, so the LVGL module was not available), and no Accel 11 Click was
available. The sensor path inherits the caveats of `i2c-accel11`: I2C traffic and
real readings have never been exercised on hardware. The flick threshold is a
starting point and will probably need adjusting on the real board.
