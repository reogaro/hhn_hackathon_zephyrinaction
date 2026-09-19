/*
 * Copyright (c) 2026 Microchip Technology Inc
 * SPDX-License-Identifier: MIT
 *
 * GY-521 breakout (InvenSense MPU-6050, accelerometer + gyroscope) wired to the
 * I2C pins of the PIC64GX1000 Curiosity Kit.
 *
 * Two things about the PIC64GX I2C driver (drivers/i2c/i2c_mchp_mss.c) shape
 * the code below:
 *
 *   1. The clock-frequency devicetree property is parsed but never applied.
 *      The bus speed is only set by i2c_configure(), so calling it is
 *      mandatory, not optional.
 *
 *   2. Zero length transfers are rejected with -EINVAL. The usual address
 *      scan trick (a 0-byte write) does not work; presence is detected by
 *      actually reading the WHO_AM_I register.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

/* ── MPU-6050 register map (subset used here) ─────────────────────────────── */
#define MPU_REG_SMPLRT_DIV      0x19
#define MPU_REG_CONFIG          0x1A
#define MPU_REG_GYRO_CONFIG     0x1B
#define MPU_REG_ACCEL_CONFIG    0x1C
#define MPU_REG_ACCEL_XOUT_H    0x3B   /* ACC X/Y/Z, TEMP, GYRO X/Y/Z: 14 bytes, big endian */
#define MPU_REG_PWR_MGMT_1      0x6B
#define MPU_REG_WHO_AM_I        0x75

#define MPU_WHO_AM_I_MPU6050    0x68

#define MPU_PWR_MGMT_1_RESET    0x80
/* Clear SLEEP (set out of reset) and use the X gyro PLL as clock source. */
#define MPU_PWR_MGMT_1_RUN      0x01

/* CONFIG: DLPF_CFG = 3 -> 44 Hz accel / 42 Hz gyro bandwidth, 1 kHz gyro rate */
#define MPU_CONFIG_DLPF_44HZ    0x03
/* Sample rate = 1 kHz / (1 + 19) = 50 Hz */
#define MPU_SMPLRT_DIV_50HZ     19
/* FS_SEL / AFS_SEL = 0: gyro +/-250 dps, accel +/-2 g */
#define MPU_GYRO_RANGE_250DPS   0x00
#define MPU_ACCEL_RANGE_2G      0x00

#define MPU_ACCEL_LSB_PER_G     16384
#define MPU_GYRO_LSB_PER_DPS    131

/* ── Candidate buses and addresses ────────────────────────────────────────── */
/*
 * Which I2C controller reaches the header is decided by the Libero FPGA design,
 * not by Zephyr, so both are tried. The GY-521 address depends on the AD0 pin:
 * low (board default, pulled down) gives 0x68, high gives 0x69.
 */
static const struct device *const buses[] = {
	DEVICE_DT_GET(DT_ALIAS(i2c0)),
	DEVICE_DT_GET(DT_ALIAS(i2c1)),
};

static const char *const bus_names[] = { "i2c0", "i2c1" };

static const uint8_t addresses[] = { 0x68, 0x69 };

/* ── Register helpers ─────────────────────────────────────────────────────── */

static int reg_read(const struct device *bus, uint8_t addr, uint8_t reg, uint8_t *buf, size_t len)
{
	/*
	 * i2c_write_read() emits write[reg] then read[len] with a repeated start
	 * between the two. That is exactly the message pattern the PIC64GX driver
	 * demands: restart on a direction change, stop only on the last message.
	 */
	return i2c_write_read(bus, addr, &reg, 1, buf, len);
}

static int reg_write(const struct device *bus, uint8_t addr, uint8_t reg, uint8_t val)
{
	const uint8_t out[2] = { reg, val };

	return i2c_write(bus, out, sizeof(out), addr);
}

/* ── Detection ────────────────────────────────────────────────────────────── */

struct mpu6050 {
	const struct device *bus;
	const char *bus_name;
	uint8_t addr;
};

static bool detect(struct mpu6050 *out)
{
	for (size_t b = 0; b < ARRAY_SIZE(buses); b++) {
		const struct device *bus = buses[b];

		if (!device_is_ready(bus)) {
			printk("  %-5s : driver not ready, skipping\n", bus_names[b]);
			continue;
		}

		/*
		 * Mandatory: the devicetree clock-frequency is ignored by this
		 * driver, so without this call the bus divider is whatever reset
		 * left behind.
		 */
		int ret = i2c_configure(bus, I2C_SPEED_SET(I2C_SPEED_STANDARD) | I2C_MODE_CONTROLLER);

		if (ret != 0) {
			printk("  %-5s : i2c_configure() failed (%d), skipping\n", bus_names[b], ret);
			continue;
		}

		for (size_t a = 0; a < ARRAY_SIZE(addresses); a++) {
			uint8_t id = 0;

			ret = reg_read(bus, addresses[a], MPU_REG_WHO_AM_I, &id, 1);
			if (ret != 0) {
				printk("  %-5s 0x%02x : no response (%d)\n",
				       bus_names[b], addresses[a], ret);
				continue;
			}

			/*
			 * A genuine MPU-6050 reports 0x68. Clones and the pin compatible
			 * MPU-6500 family report other values (0x70, 0x71, 0x72, ...) but
			 * share the accel/gyro/temp register layout used here, so anything
			 * that answers is accepted and the ID is only reported.
			 */
			if (id == MPU_WHO_AM_I_MPU6050) {
				printk("  %-5s 0x%02x : MPU-6050 found, WHO_AM_I 0x%02x\n",
				       bus_names[b], addresses[a], id);
			} else {
				printk("  %-5s 0x%02x : device answered, WHO_AM_I 0x%02x "
				       "(MPU-6050 is 0x%02x) - continuing, may be a clone\n",
				       bus_names[b], addresses[a], id, MPU_WHO_AM_I_MPU6050);
			}

			out->bus = bus;
			out->bus_name = bus_names[b];
			out->addr = addresses[a];
			return true;
		}
	}

	return false;
}

/* ── Initialisation ───────────────────────────────────────────────────────── */

static int mpu6050_init(const struct mpu6050 *dev)
{
	int ret;

	ret = reg_write(dev->bus, dev->addr, MPU_REG_PWR_MGMT_1, MPU_PWR_MGMT_1_RESET);
	if (ret != 0) {
		printk("reset failed (%d)\n", ret);
		return ret;
	}

	/* The datasheet asks for 100 ms after a device reset. */
	k_msleep(100);

	/* The chip powers up asleep; clearing SLEEP is what starts sampling. */
	ret = reg_write(dev->bus, dev->addr, MPU_REG_PWR_MGMT_1, MPU_PWR_MGMT_1_RUN);
	if (ret != 0) {
		printk("PWR_MGMT_1 write failed (%d)\n", ret);
		return ret;
	}
	k_msleep(10);

	ret = reg_write(dev->bus, dev->addr, MPU_REG_CONFIG, MPU_CONFIG_DLPF_44HZ);
	if (ret != 0) {
		printk("CONFIG write failed (%d)\n", ret);
		return ret;
	}

	ret = reg_write(dev->bus, dev->addr, MPU_REG_SMPLRT_DIV, MPU_SMPLRT_DIV_50HZ);
	if (ret != 0) {
		printk("SMPLRT_DIV write failed (%d)\n", ret);
		return ret;
	}

	ret = reg_write(dev->bus, dev->addr, MPU_REG_GYRO_CONFIG, MPU_GYRO_RANGE_250DPS);
	if (ret != 0) {
		printk("GYRO_CONFIG write failed (%d)\n", ret);
		return ret;
	}

	ret = reg_write(dev->bus, dev->addr, MPU_REG_ACCEL_CONFIG, MPU_ACCEL_RANGE_2G);
	if (ret != 0) {
		printk("ACCEL_CONFIG write failed (%d)\n", ret);
		return ret;
	}

	return 0;
}

/* ── Sampling ─────────────────────────────────────────────────────────────── */

struct sample {
	int16_t accel[3];
	int16_t temp;
	int16_t gyro[3];
};

static inline int16_t be16(const uint8_t *p)
{
	return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static int read_sample(const struct mpu6050 *dev, struct sample *s)
{
	uint8_t raw[14];
	int ret = reg_read(dev->bus, dev->addr, MPU_REG_ACCEL_XOUT_H, raw, sizeof(raw));

	if (ret != 0) {
		return ret;
	}

	s->accel[0] = be16(&raw[0]);
	s->accel[1] = be16(&raw[2]);
	s->accel[2] = be16(&raw[4]);
	s->temp     = be16(&raw[6]);
	s->gyro[0]  = be16(&raw[8]);
	s->gyro[1]  = be16(&raw[10]);
	s->gyro[2]  = be16(&raw[12]);

	return 0;
}

static inline int accel_to_mg(int16_t counts)
{
	return ((int)counts * 1000) / MPU_ACCEL_LSB_PER_G;
}

static inline int gyro_to_dps(int16_t counts)
{
	return (int)counts / MPU_GYRO_LSB_PER_DPS;
}

/* Datasheet: temperature in degrees C = raw / 340 + 36.53. Returned in 1/100 C. */
static inline int temp_to_centi_c(int16_t counts)
{
	return ((int)counts * 100) / 340 + 3653;
}

/* ── Diagnostics when nothing answers ─────────────────────────────────────── */

static void print_no_device_help(void)
{
	printk("\n"
	       "No MPU-6050 responded on i2c0 or i2c1 at 0x68 or 0x69.\n"
	       "\n"
	       "Things to check, most likely first:\n"
	       "  1. Wiring: VCC->3V3, GND->GND, SCL->SCL, SDA->SDA. SDA and SCL\n"
	       "     swapped is the most common mistake. Leave XDA, XCL, AD0, INT open.\n"
	       "  2. Does your Libero FPGA design route an I2C controller to the\n"
	       "     header SCL/SDA pins? Zephyr cannot know this; the board DTS\n"
	       "     enables both controllers but the fabric decides what is wired.\n"
	       "  3. Is the power LED on the GY-521 lit?\n"
	       "  4. Pull-ups: the GY-521 has 4.7k pull-ups on SCL and SDA already.\n"
	       "\n"
	       "The shell is running on this console. Useful commands:\n"
	       "  device list\n"
	       "  i2c read_byte i2c@2010a000 0x68 0x75   - WHO_AM_I on controller 0\n"
	       "  i2c read_byte i2c@2010b000 0x68 0x75   - WHO_AM_I on controller 1\n"
	       "\n"
	       "Do not bother with 'i2c scan'. It probes using zero length writes,\n"
	       "which this driver always rejects with -EINVAL, so it reports an empty\n"
	       "bus even when a device is present. read_byte is the reliable check.\n");
}

/* ── Main ─────────────────────────────────────────────────────────────────── */

int main(void)
{
	printk("\n=== GY-521 (MPU-6050) on PIC64GX1000 Curiosity Kit ===\n\n");
	printk("Probing I2C controllers and addresses:\n");

	struct mpu6050 dev;

	if (!detect(&dev)) {
		print_no_device_help();
		/* Return instead of spinning, so the shell stays usable. */
		return 0;
	}

	printk("\nUsing %s at address 0x%02x\n", dev.bus_name, dev.addr);

	if (mpu6050_init(&dev) != 0) {
		printk("Initialisation failed. The device answered the WHO_AM_I read but "
		       "not the configuration writes; check the wiring for marginal "
		       "signal quality.\n");
		return 0;
	}

	printk("Configured: 50 Hz, accel +/-2 g, gyro +/-250 dps\n");
	printk("\nStreaming samples (raw counts are 16 bit):\n\n");

	int consecutive_errors = 0;

	while (1) {
		struct sample s;
		int ret = read_sample(&dev, &s);

		if (ret == 0) {
			int centi = temp_to_centi_c(s.temp);

			printk("A[mg] X %6d Y %6d Z %6d | G[dps] X %4d Y %4d Z %4d | T %d.%02d C\n",
			       accel_to_mg(s.accel[0]), accel_to_mg(s.accel[1]),
			       accel_to_mg(s.accel[2]),
			       gyro_to_dps(s.gyro[0]), gyro_to_dps(s.gyro[1]),
			       gyro_to_dps(s.gyro[2]),
			       centi / 100, centi % 100);
			consecutive_errors = 0;
		} else {
			printk("sample read failed (%d)\n", ret);

			if (++consecutive_errors == 10) {
				printk("\n10 consecutive failures, giving up. The shell "
				       "remains available on this console.\n");
				return 0;
			}
		}

		k_msleep(500);
	}

	return 0;
}
