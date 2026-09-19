/*
 * Copyright (c) 2026 Microchip Technology Inc
 * SPDX-License-Identifier: MIT
 *
 * AZ-Delivery GY-521 (InvenSense MPU-6050) 6-DOF IMU on PIC64GX1000 Curiosity Kit.
 *
 * The GY-521 breakout carries an InvenSense MPU-6050 containing:
 *   - 3-axis accelerometer (+/- 2g, 4g, 8g, 16g)
 *   - 3-axis gyroscope (+/- 250, 500, 1000, 2000 deg/s)
 *   - On-chip temperature sensor
 *
 * Wired to 5V/GND/SDA/SCL on the Curiosity Kit.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <stdlib.h>

/* ── MPU-6050 Register Map ────────────────────────────────────────────────── */
#define MPU6050_REG_SMPLRT_DIV       0x19
#define MPU6050_REG_CONFIG           0x1A
#define MPU6050_REG_GYRO_CONFIG      0x1B
#define MPU6050_REG_ACCEL_CONFIG     0x1C
#define MPU6050_REG_ACCEL_XOUT_H     0x3B
#define MPU6050_REG_TEMP_OUT_H       0x41
#define MPU6050_REG_GYRO_XOUT_H      0x43
#define MPU6050_REG_PWR_MGMT_1       0x6B
#define MPU6050_REG_PWR_MGMT_2       0x6C
#define MPU6050_REG_WHO_AM_I         0x75

#define MPU6050_WHO_AM_I_VAL         0x68

/* Reset / Power Management */
#define MPU6050_PWR1_DEVICE_RESET    0x80
#define MPU6050_PWR1_CLK_PLL_XGYRO   0x01

/* Full scale ranges & sensitivity */
/* Accel: +/- 2g -> 16384 LSB/g */
#define MPU6050_ACCEL_FS_2G          0x00
#define MPU6050_ACCEL_SENS_2G        16384

/* Gyro: +/- 250 deg/s -> 131.0 LSB/(deg/s) */
#define MPU6050_GYRO_FS_250DPS       0x00

/* ── Candidate buses and addresses ────────────────────────────────────────── */
static const struct device *const buses[] = {
	DEVICE_DT_GET(DT_ALIAS(i2c0)),
	DEVICE_DT_GET(DT_ALIAS(i2c1)),
};

static const char *const bus_names[] = { "i2c0", "i2c1" };

/* AD0 pin low = 0x68 (default GY-521 pull-down), AD0 pin high = 0x69 */
static const uint8_t addresses[] = { 0x68, 0x69 };

/* ── Device structure ─────────────────────────────────────────────────────── */
struct gy521_dev {
	const struct device *bus;
	const char *bus_name;
	uint8_t addr;
};

/* ── I2C Helpers ──────────────────────────────────────────────────────────── */

static int reg_read(const struct device *bus, uint8_t addr, uint8_t reg, uint8_t *buf, size_t len)
{
	return i2c_write_read(bus, addr, &reg, 1, buf, len);
}

static int reg_write(const struct device *bus, uint8_t addr, uint8_t reg, uint8_t val)
{
	const uint8_t out[2] = { reg, val };

	return i2c_write(bus, out, sizeof(out), addr);
}

static int reg_write_retry(const struct device *bus, uint8_t addr, uint8_t reg, uint8_t val, int retries)
{
	int ret = -EIO;

	for (int i = 0; i < retries; i++) {
		ret = reg_write(bus, addr, reg, val);
		if (ret == 0) {
			return 0;
		}
		k_msleep(20);
	}
	return ret;
}

/* ── Detection & Probe ────────────────────────────────────────────────────── */

static bool detect(struct gy521_dev *out)
{
	for (size_t b = 0; b < ARRAY_SIZE(buses); b++) {
		const struct device *bus = buses[b];

		if (!device_is_ready(bus)) {
			printk("  %-5s : driver not ready, skipping\n", bus_names[b]);
			continue;
		}

		int ret = i2c_configure(bus, I2C_SPEED_SET(I2C_SPEED_STANDARD) | I2C_MODE_CONTROLLER);
		if (ret != 0) {
			printk("  %-5s : i2c_configure() failed (%d), skipping\n", bus_names[b], ret);
			continue;
		}

		for (size_t a = 0; a < ARRAY_SIZE(addresses); a++) {
			uint8_t id = 0;

			ret = reg_read(bus, addresses[a], MPU6050_REG_WHO_AM_I, &id, 1);
			if (ret != 0) {
				printk("  %-5s 0x%02x : no response (%d)\n",
				       bus_names[b], addresses[a], ret);
				continue;
			}

			uint8_t masked_id = (id & 0x7E);
			if (masked_id != MPU6050_WHO_AM_I_VAL && id != MPU6050_WHO_AM_I_VAL) {
				printk("  %-5s 0x%02x : device answered, WHO_AM_I = 0x%02x (expected 0x%02x)\n",
				       bus_names[b], addresses[a], id, MPU6050_WHO_AM_I_VAL);
				continue;
			}

			printk("  %-5s 0x%02x : GY-521 (MPU-6050) found! WHO_AM_I = 0x%02x\n",
			       bus_names[b], addresses[a], id);

			out->bus = bus;
			out->bus_name = bus_names[b];
			out->addr = addresses[a];
			return true;
		}
	}

	return false;
}

/* ── Sensor Initialisation ────────────────────────────────────────────────── */

static int gy521_init(const struct gy521_dev *dev)
{
	int ret;

	k_msleep(50);

	/* 1. Reset device */
	ret = reg_write_retry(dev->bus, dev->addr, MPU6050_REG_PWR_MGMT_1, MPU6050_PWR1_DEVICE_RESET, 5);
	if (ret != 0) {
		printk("Reset failed (%d)\n", ret);
		return ret;
	}
	k_msleep(100);

	/* 2. Wake up device and select PLL with X axis gyroscope reference */
	ret = reg_write_retry(dev->bus, dev->addr, MPU6050_REG_PWR_MGMT_1, MPU6050_PWR1_CLK_PLL_XGYRO, 5);
	if (ret != 0) {
		printk("Wakeup / clock select failed (%d)\n", ret);
		return ret;
	}
	k_msleep(20);

	/* 3. Configure Digital Low Pass Filter (DLPF) ~ 44 Hz accel, 42 Hz gyro */
	ret = reg_write_retry(dev->bus, dev->addr, MPU6050_REG_CONFIG, 0x03, 5);
	if (ret != 0) {
		printk("DLPF config failed (%d)\n", ret);
		return ret;
	}

	/* 4. Configure sample rate divider: 1kHz / (1 + 4) = 200 Hz */
	ret = reg_write_retry(dev->bus, dev->addr, MPU6050_REG_SMPLRT_DIV, 0x04, 5);
	if (ret != 0) {
		printk("Sample rate divider failed (%d)\n", ret);
		return ret;
	}

	/* 5. Set Gyro full scale range to +/- 250 deg/s */
	ret = reg_write_retry(dev->bus, dev->addr, MPU6050_REG_GYRO_CONFIG, MPU6050_GYRO_FS_250DPS, 5);
	if (ret != 0) {
		printk("Gyro config failed (%d)\n", ret);
		return ret;
	}

	/* 6. Set Accel full scale range to +/- 2g */
	ret = reg_write_retry(dev->bus, dev->addr, MPU6050_REG_ACCEL_CONFIG, MPU6050_ACCEL_FS_2G, 5);
	if (ret != 0) {
		printk("Accel config failed (%d)\n", ret);
		return ret;
	}

	return 0;
}

/* ── Reading Sensor Data ──────────────────────────────────────────────────── */

struct gy521_sample {
	int16_t accel_raw[3];
	int16_t temp_raw;
	int16_t gyro_raw[3];

	int32_t accel_mg[3];
	int32_t temp_c_x10;
	int32_t gyro_dps_x10[3];
};

static int read_all(const struct gy521_dev *dev, struct gy521_sample *s)
{
	uint8_t raw[14];

	int ret = reg_read(dev->bus, dev->addr, MPU6050_REG_ACCEL_XOUT_H, raw, sizeof(raw));
	if (ret != 0) {
		return ret;
	}

	/* Parse 16-bit big-endian values */
	s->accel_raw[0] = (int16_t)(((uint16_t)raw[0]  << 8) | raw[1]);
	s->accel_raw[1] = (int16_t)(((uint16_t)raw[2]  << 8) | raw[3]);
	s->accel_raw[2] = (int16_t)(((uint16_t)raw[4]  << 8) | raw[5]);

	s->temp_raw     = (int16_t)(((uint16_t)raw[6]  << 8) | raw[7]);

	s->gyro_raw[0]  = (int16_t)(((uint16_t)raw[8]  << 8) | raw[9]);
	s->gyro_raw[1]  = (int16_t)(((uint16_t)raw[10] << 8) | raw[11]);
	s->gyro_raw[2]  = (int16_t)(((uint16_t)raw[12] << 8) | raw[13]);

	/* Convert Accel to mg (+/- 2g -> 16384 LSB/g) */
	for (int i = 0; i < 3; i++) {
		s->accel_mg[i] = ((int32_t)s->accel_raw[i] * 1000) / MPU6050_ACCEL_SENS_2G;
	}

	/* Convert Temperature: T = (raw / 340.0) + 36.53 deg C */
	s->temp_c_x10 = (((int32_t)s->temp_raw * 10) / 340) + 365;

	/* Convert Gyro to deg/s * 10 (+/- 250 deg/s -> 131.072 LSB/dps) */
	for (int i = 0; i < 3; i++) {
		s->gyro_dps_x10[i] = ((int32_t)s->gyro_raw[i] * 100) / 1310;
	}

	return 0;
}

/* ── Help / Diagnostics ───────────────────────────────────────────────────── */

static void print_no_device_help(void)
{
	printk("\n"
	       "No MPU-6050 responded on i2c0 or i2c1 at 0x68 or 0x69.\n"
	       "\n"
	       "Checklist:\n"
	       "  1. Power: 5V (or 3.3V to VCC) and GND connected.\n"
	       "  2. I2C Lines: SDA and SCL connected.\n"
	       "  3. Address pin: AD0 floating/low = 0x68, AD0 high = 0x69.\n"
	       "  4. Pull-up resistors: GY-521 board typically includes 4.7k pull-ups.\n"
	       "\n"
	       "The shell is running on this console. Useful commands:\n"
	       "  device list\n"
	       "  i2c read_byte i2c@2010a000 0x68 0x75   - WHO_AM_I on controller 0\n"
	       "  i2c read_byte i2c@2010b000 0x68 0x75   - WHO_AM_I on controller 1\n");
}

/* ── Main ─────────────────────────────────────────────────────────────────── */

int main(void)
{
	printk("\n=== AZ-Delivery GY-521 (MPU-6050) on PIC64GX Curiosity Kit ===\n\n");
	printk("Probing I2C controllers (i2c0, i2c1) and addresses (0x68, 0x69):\n");

	struct gy521_dev dev;

	if (!detect(&dev)) {
		print_no_device_help();
		return 0;
	}

	printk("\nUsing %s at address 0x%02x\n", dev.bus_name, dev.addr);

	if (gy521_init(&dev) != 0) {
		printk("Initialisation failed.\n");
		return 0;
	}

	printk("Configured: Accel +/-2g (16384 LSB/g), Gyro +/-250 dps (131 LSB/dps), DLPF 44Hz\n");
	printk("\nStreaming 6-DOF IMU samples:\n\n");

	int consecutive_errors = 0;

	while (1) {
		struct gy521_sample s;
		int ret = read_all(&dev, &s);

		if (ret == 0) {
			int temp_int = s.temp_c_x10 / 10;
			int temp_frac = abs(s.temp_c_x10 % 10);

			int gx_int = s.gyro_dps_x10[0] / 10;
			int gx_frac = abs(s.gyro_dps_x10[0] % 10);
			int gy_int = s.gyro_dps_x10[1] / 10;
			int gy_frac = abs(s.gyro_dps_x10[1] % 10);
			int gz_int = s.gyro_dps_x10[2] / 10;
			int gz_frac = abs(s.gyro_dps_x10[2] % 10);

			printk("ACC [mg]: X=%+5d Y=%+5d Z=%+5d | GYR [dps]: X=%+4d.%1d Y=%+4d.%1d Z=%+4d.%1d | T=%2d.%1d C\n",
			       s.accel_mg[0], s.accel_mg[1], s.accel_mg[2],
			       gx_int, gx_frac,
			       gy_int, gy_frac,
			       gz_int, gz_frac,
			       temp_int, temp_frac);

			consecutive_errors = 0;
		} else {
			printk("Sample read error (%d)\n", ret);
			if (++consecutive_errors >= 10) {
				printk("10 consecutive errors, pausing stream.\n");
				return 0;
			}
		}

		k_msleep(250);
	}

	return 0;
}
