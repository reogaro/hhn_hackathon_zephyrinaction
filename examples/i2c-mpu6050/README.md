# GY-521 (MPU-6050) over I2C

Standalone I2C example for the PIC64GX1000 Curiosity Kit. It reads acceleration,
angular rate and die temperature from a **GY-521** breakout (InvenSense
MPU-6050) and prints them on the console. Same structure as `i2c-accel11`, with
the BMA456 register code replaced by MPU-6050 registers.

## Wiring

| GY-521 pin | Kit pin | Note |
| ---------- | ------- | ---- |
| VCC        | 3V3     | The board has its own 3.3 V regulator, so 5 V also powers it, but 3V3 keeps the I2C pull-ups at the kit's logic level |
| GND        | GND     | |
| SCL        | I2C SCL | |
| SDA        | I2C SDA | |
| AD0        | not connected | Pulled low on the board: address `0x68`. Tie to VCC for `0x69` |
| XDA, XCL, INT | not connected | Not used |

The GY-521 already has pull-up resistors on SCL and SDA, so no external ones are
needed.

## Address and identification

The code probes `i2c0` and `i2c1` (which one reaches the header is decided by the
FPGA design) at `0x68` and `0x69`, and reads `WHO_AM_I` (register `0x75`). A
genuine MPU-6050 returns `0x68`. Clone chips return other values; those are
accepted and the value is printed, since the accel/gyro/temperature registers
are the same.

## Build

```bash
export ZEPHYR_BASE=$HOME/pic64gx-zephyr/zephyr
export ZEPHYR_SDK_INSTALL_DIR=$HOME/zephyr-sdk-1.0.1
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr

west build -b pic64gx_curiosity_kit/pic64gx1000/u54/smp \
  -d build/i2c-mpu6050 -p always examples/i2c-mpu6050
```

Then create the HSS payload and flash as for the other examples. Console is
**mmuart1 at 115200 8N1**.

## Expected output

```
=== GY-521 (MPU-6050) on PIC64GX1000 Curiosity Kit ===

Probing I2C controllers and addresses:
  i2c0  0x68 : MPU-6050 found, WHO_AM_I 0x68

A[mg] X     12 Y    -20 Z   1002 | G[dps] X    0 Y   -1 Z    0 | T 27.40 C
```

Flat and still: one accel axis near 1000 mg (gravity), the others near zero,
gyro near zero.

## Verification status

Builds for `pic64gx_curiosity_kit/pic64gx1000/u54/smp`. Not run on hardware: the
detection path, initialisation and the sample output above are untested against
a real GY-521.

If nothing answers, check with the shell:

```
i2c read_byte i2c@2010a000 0x68 0x75
i2c read_byte i2c@2010b000 0x68 0x75
```

Do not use `i2c scan`; the PIC64GX driver rejects the zero length writes it
relies on.
