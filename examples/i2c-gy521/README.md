# AZ-Delivery GY-521 (InvenSense MPU-6050) over I2C

Standalone 6-DOF IMU example for the PIC64GX1000 Curiosity Kit. It reads 3-axis acceleration, 3-axis angular rate (gyroscope), and internal die temperature from an **AZ-Delivery GY-521** breakout board (MPU-6050) connected to the board via I2C.

## Hardware & Wiring

Connect the GY-521 board to the PIC64GX1000 Curiosity Kit header / mikroBUS:

| GY-521 Pin | Curiosity Kit Signal | Description |
| ---------- | -------------------- | ----------- |
| VCC        | 5V (or 3.3V)         | Supply power |
| GND        | GND                  | Ground |
| SCL        | SCL (I2C Clock)      | I2C Clock |
| SDA        | SDA (I2C Data)       | I2C Data |
| AD0        | GND (or NC)          | Selects address 0x68 (default) |

## Sensor Features

- **Accelerometer:** +/-2g range (16384 LSB/g)
- **Gyroscope:** +/-250 deg/s range (131 LSB/(deg/s))
- **DLPF:** Configured to ~44 Hz bandwidth
- **Burst Read:** Contiguous 14-byte DMA/FIFO read across all 6 degrees of freedom + temperature

## Building and Flashing

```bash
# Build with west
west build -b pic64gx_curiosity_kit/pic64gx1000/u54/smp \
  -d build/i2c-gy521 -p always \
  examples/i2c-gy521
```

Deploy via OpenOCD / GDB or package into HSS payload with `payload.sh`.

## Output

Streaming live samples to `mmuart1` console (115200 8N1):

```text
=== AZ-Delivery GY-521 (MPU-6050) on PIC64GX Curiosity Kit ===

Probing I2C controllers (i2c0, i2c1) and addresses (0x68, 0x69):
  i2c0  0x68 : GY-521 (MPU-6050) found! WHO_AM_I = 0x68

Using i2c0 at address 0x68
Configured: Accel +/-2g (16384 LSB/g), Gyro +/-250 dps (131 LSB/dps), DLPF 44Hz

Streaming 6-DOF IMU samples:

ACC [mg]: X=  +14 Y=  -21 Z=+1016 | GYR [dps]: X=  +0.1 Y=  -0.2 Z=  +0.0 | T=25.8 C
```
