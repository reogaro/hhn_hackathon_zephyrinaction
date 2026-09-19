# Flappy GY-521

Flappy Microchip, played by moving the board. An **AZ-Delivery GY-521**
(InvenSense MPU-6050 accelerometer + gyroscope) feeds motion into the game: a
sharp flick or a quick twist of the board makes the player flap, exactly like
pressing SW2. SW2 still works, so the game is playable with or without the
GY-521 attached.

This project is a copy of the `pic64_smp_hello` demo from
[flappy-microchip](../flappy-microchip/) plus one new module, and it reuses that
example's VDMA/HDMI drivers and devicetree bindings in place. The sensor code
comes from [i2c-gy521](../i2c-gy521/); read its README for the wiring and the
sensor configuration. It is the same idea as [flappy-accel11](../flappy-accel11/),
with a different sensor and a second motion signal (the gyroscope).

## What changed against the Flappy source

| File | Change |
| ---- | ------ |
| `src/gy521.c`, `src/gy521.h` | New. Reader thread: finds the MPU-6050 on i2c0/i2c1 at 0x68/0x69, samples at 100 Hz, raises a flap event on a flick or a twist. |
| `src/screen_game.c` | `flap = SW2 edge OR gy521_take_flap()`. Motion is ignored for 1 s after a crash so the player does not restart by accident. On screen texts mention the flick. |
| `src/main.c` | Calls `gy521_start()` before `ui_start()`. |
| `prj.conf` | `CONFIG_I2C=y`. |
| `CMakeLists.txt`, `app.overlay` | Point at `../flappy-microchip/source` for drivers and DTS. |

## Wiring

Same as [i2c-gy521](../i2c-gy521/README.md#hardware--wiring): VCC, GND, SCL and
SDA to the Curiosity Kit header. Leave AD0 low or floating for address 0x68 (0x69
also works). Both I2C controllers are tried, since the FPGA design decides which
one reaches the header.

## How a flap is detected

Two detectors run on every sample, and either one raises a flap. Both look at the
size of a vector, not at one axis, so it does not matter how the board is held.

| Detector | Signal | At rest | Fires when | Constant |
| -------- | ------ | ------- | ---------- | -------- |
| Flick | acceleration magnitude | 1 g (gravity) | more than 600 mg above or below 1 g | `FLICK_THRESHOLD_MG` |
| Twist | angular rate magnitude | about 0 dps | faster than 150 dps | `TWIST_THRESHOLD_DPS` |

After one flap, further motion is ignored for `FLAP_HOLDOFF_MS` (250 ms), because
a flick has a push phase and a braking phase that would both count. A motion that
is held (shaking the board continuously) therefore flaps about four times a
second. All constants are at the top of `src/gy521.c`.

Noise handling, in the order a sample meets it:

1. **Bad reads are dropped.** A burst that returns all zeros or all ones is a bus
   glitch, not a sample, and is discarded. Twenty failures in a row stop the
   reader.
2. **Median of three.** Each detector judges the median of the last three
   magnitudes, so a motion must show in two consecutive samples (20 ms). A single
   spike from a bus glitch or a knock is ignored, and there is no added lag.
3. **Hold-off.** One flap per motion, as above.

The game tick runs every 33 ms and the sensor thread every 10 ms, so a flap is
seen within one game frame. The sensor is configured as in `i2c-gy521` (+/-2 g,
+/-250 dps, 44 Hz low-pass filter), and the temperature is not used.

## Build

```bash
export ZEPHYR_BASE=$HOME/pic64gx-zephyr/zephyr
export ZEPHYR_SDK_INSTALL_DIR=$HOME/zephyr-sdk-1.0.1
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr

$HOME/pic64gx-zephyr/.venv/bin/west build \
  -b pic64gx_curiosity_kit/pic64gx1000/u54/smp \
  -d $HOME/pic64gx-zephyr/build/flappy-gy521 -p always \
  examples/flappy-gy521

./setup/linux/payload.sh \
  --build-dir $HOME/pic64gx-zephyr/build/flappy-gy521 \
  --output examples/flappy-gy521/payload.bin
./setup/linux/flash.sh --list
```

`payload.sh` reads the HSS configuration from the `flappy-microchip` source tree,
not from this directory. The two are identical apart from the set name, so the
image is the same; `hss-payload.yaml` here is kept for reference and for tools
that take the config from the project directory.

## Console

mmuart1 at 115200 8N1. On boot you should see one of:

```text
[gy521] MPU-6050 on i2c0 at 0x68 (WHO_AM_I 0x68)
[gy521] streaming, flick threshold 600 mg, twist threshold 150 dps
```

```text
[gy521] no MPU-6050 found, SW2 is the only input
```

## Tuning

Set `GY521_DEBUG_PRINT` to 1 in `src/gy521.c` and watch the console. Note the
acceleration and rotation at rest, while handling the board, and during a flap,
then put each threshold between "handling" and "flap". Turn it off again
afterwards, printing at 115200 baud is slow.

- Flaps fire when you only handle the board: raise `FLICK_THRESHOLD_MG` or
  `TWIST_THRESHOLD_DPS`.
- You have to shake it hard: lower them.
- One flick gives two flaps: raise `FLAP_HOLDOFF_MS`.
- Only want one kind of motion: drop the `is_flick()` or `is_twist()` term from
  the flap condition in `gy521_fn()`.

## Verification status

**Not built for the target and not run on hardware.** There is no west workspace
on the machine this was written on, so the full Zephyr/LVGL build was not tried.
What was checked: `src/gy521.c` compiles warning-free with `gcc -Wall -Wextra`
against stub Zephyr headers, and running it there against a simulated MPU-6050
gave the expected behaviour: the init register sequence matches `i2c-gy521`,
rest, slight tilt and a 20 dps turn do not flap, a 1.95 g flick, a free fall and
a 200 dps twist flap once each, and a sustained twist flaps about every 250 ms.
The game-side edits (`main.c`, `screen_game.c`) are a rename of the
`flappy-accel11` ones and were not compiled. The thresholds are starting points
and will probably need adjusting on the real board.
