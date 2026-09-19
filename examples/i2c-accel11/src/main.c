/*
 * Copyright (c) 2026 Microchip Technology Inc
 * SPDX-License-Identifier: MIT
 *
 * MikroElektronika Accel 11 Click on the mikroBUS header of the
 * PIC64GX1000 Curiosity Kit.
 *
 * The Accel 11 Click carries a Bosch BMA456 accelerometer. Zephyr's in-tree
 * bma4xx driver cannot be used: it only accepts chip IDs 0x12 (BMA422),
 * 0x13 (BMA423) and 0x90 (BMA400), and rejects the BMA456 ID 0x16 with
 * -ENODEV. This example therefore talks to the chip directly through the
 * Zephyr I2C API.
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
 *      actually reading the chip ID register.
 */

#include <math.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

/* ── BMA456 register map (subset used here) ───────────────────────────────── */
#define BMA456_REG_CHIP_ID      0x00
#define BMA456_REG_ERROR        0x02
#define BMA456_REG_STATUS       0x03
#define BMA456_REG_ACC_X_LSB    0x12   /* X LSB, MSB, Y LSB, MSB, Z LSB, MSB */
#define BMA456_REG_TEMPERATURE  0x22
#define BMA456_REG_ACC_CONF     0x40
#define BMA456_REG_ACC_RANGE    0x41
#define BMA456_REG_POWER_CONF   0x7C
#define BMA456_REG_POWER_CTRL   0x7D
#define BMA456_REG_CMD          0x7E

#define BMA456_CHIP_ID          0x16   /* BMA456 */

#define BMA456_STATUS_DRDY_ACC  0x80

#define BMA456_CMD_SOFTRESET    0xB6

/*
 * ACC_CONF: performance mode (bit 7) | normal filter, 4x averaging (bits 6:4)
 * | ODR 100 Hz (bits 3:0). Performance mode keeps the sensor fully awake, so a
 * new sample really arrives every 10 ms. Without it the sensor averages and
 * duty-cycles, which adds latency.
 */
#define BMA456_ACC_CONF_DEFAULT 0xA8
/* ACC_RANGE: +/- 2 g */
#define BMA456_ACC_RANGE_2G     0x00

/* POWER_CTRL: accelerometer enable */
#define BMA456_POWER_CTRL_ACC_EN 0x04
/* POWER_CONF: advanced power save off, for the lowest latency */
#define BMA456_POWER_CONF_DEFAULT 0x00

/* Accelerometer output is 12 bit, left aligned inside a 16 bit word, so the
 * usable range is -2048..2047 counts. At +/-2 g full scale one count is
 * 2000 mg / 2048.
 */
#define BMA456_RAW_BITS         12
#define BMA456_RANGE_MG         2000

/*
 * Low-pass filter weight of the newest sample: filt += ALPHA * (new - filt).
 * At 100 Hz, 0.2 settles in roughly 0.1 s. Raise it for a snappier but noisier
 * angle, lower it for a smoother but laggier one.
 */
#define TILT_FILTER_ALPHA       0.2f

/*
 * Samples are read at the sensor rate (100 Hz) but only every Nth one is
 * printed. The console is 115200 baud, about 11 KB/s, and a full line takes
 * roughly 8 ms to send, so printing every sample would make the UART the
 * bottleneck and show up as lag.
 */
#define PRINT_EVERY_N_SAMPLES   5
#define RAD_TO_DEG              57.2957795f

/* ── Candidate buses and addresses ────────────────────────────────────────── */
/*
 * Which I2C controller reaches the mikroBUS header is decided by the Libero
 * FPGA design, not by Zephyr, so both are tried. The BMA456 address depends on
 * the SDO pin: low gives 0x18 (Click board default), high gives 0x19.
 */
static const struct device *const buses[] = {
	DEVICE_DT_GET(DT_ALIAS(i2c0)),
	DEVICE_DT_GET(DT_ALIAS(i2c1)),
};

static const char *const bus_names[] = { "i2c0", "i2c1" };

static const uint8_t addresses[] = { 0x18, 0x19 };

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

struct accel11 {
	const struct device *bus;
	const char *bus_name;
	uint8_t addr;
};

static bool detect(struct accel11 *out)
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

			ret = reg_read(bus, addresses[a], BMA456_REG_CHIP_ID, &id, 1);
			if (ret != 0) {
				printk("  %-5s 0x%02x : no response (%d)\n",
				       bus_names[b], addresses[a], ret);
				continue;
			}

			if (id != BMA456_CHIP_ID) {
				printk("  %-5s 0x%02x : device answered, chip ID 0x%02x "
				       "(expected 0x%02x) - not a BMA456\n",
				       bus_names[b], addresses[a], id, BMA456_CHIP_ID);
				continue;
			}

			printk("  %-5s 0x%02x : BMA456 found, chip ID 0x%02x\n",
			       bus_names[b], addresses[a], id);

			out->bus = bus;
			out->bus_name = bus_names[b];
			out->addr = addresses[a];
			return true;
		}
	}

	return false;
}

/* ── Initialisation ───────────────────────────────────────────────────────── */

static int accel11_init(const struct accel11 *dev)
{
	int ret;

	ret = reg_write(dev->bus, dev->addr, BMA456_REG_CMD, BMA456_CMD_SOFTRESET);
	if (ret != 0) {
		printk("soft reset failed (%d)\n", ret);
		return ret;
	}

	/* The reference driver waits half a second after a soft reset. */
	k_msleep(500);

	/* Enable the accelerometer before configuring it. */
	ret = reg_write(dev->bus, dev->addr, BMA456_REG_POWER_CTRL, BMA456_POWER_CTRL_ACC_EN);
	if (ret != 0) {
		printk("POWER_CTRL write failed (%d)\n", ret);
		return ret;
	}
	k_msleep(10);

	ret = reg_write(dev->bus, dev->addr, BMA456_REG_POWER_CONF, BMA456_POWER_CONF_DEFAULT);
	if (ret != 0) {
		printk("POWER_CONF write failed (%d)\n", ret);
		return ret;
	}
	k_msleep(10);

	ret = reg_write(dev->bus, dev->addr, BMA456_REG_ACC_CONF, BMA456_ACC_CONF_DEFAULT);
	if (ret != 0) {
		printk("ACC_CONF write failed (%d)\n", ret);
		return ret;
	}

	ret = reg_write(dev->bus, dev->addr, BMA456_REG_ACC_RANGE, BMA456_ACC_RANGE_2G);
	if (ret != 0) {
		printk("ACC_RANGE write failed (%d)\n", ret);
		return ret;
	}

	uint8_t err = 0;

	if (reg_read(dev->bus, dev->addr, BMA456_REG_ERROR, &err, 1) == 0 && err != 0) {
		printk("warning: ERROR register reads 0x%02x after init\n", err);
	}

	return 0;
}

/* ── Sampling ─────────────────────────────────────────────────────────────── */

/* Wait for the data-ready bit. Returns 0 on success, -EAGAIN on timeout. */
static int wait_drdy(const struct accel11 *dev)
{
	for (int tries = 0; tries < 200; tries++) {
		uint8_t status = 0;
		int ret = reg_read(dev->bus, dev->addr, BMA456_REG_STATUS, &status, 1);

		if (ret != 0) {
			return ret;
		}

		if (status & BMA456_STATUS_DRDY_ACC) {
			return 0;
		}

		/* Poll at 1 ms so a fresh sample is picked up within ~1 ms of the
		 * sensor producing it.
		 */
		k_msleep(1);
	}

	return -EAGAIN;
}

static inline int16_t to_counts(uint8_t lsb, uint8_t msb)
{
	/* 12 bit sample, left aligned in the 16 bit word. */
	int16_t word = (int16_t)(((uint16_t)msb << 8) | lsb);

	return (int16_t)(word >> (16 - BMA456_RAW_BITS));
}

static inline int counts_to_mg(int16_t counts)
{
	return ((int)counts * BMA456_RANGE_MG) >> (BMA456_RAW_BITS - 1);
}

static int read_sample(const struct accel11 *dev, int16_t axes[3])
{
	uint8_t raw[6];
	int ret = reg_read(dev->bus, dev->addr, BMA456_REG_ACC_X_LSB, raw, sizeof(raw));

	if (ret != 0) {
		return ret;
	}

	axes[0] = to_counts(raw[0], raw[1]);
	axes[1] = to_counts(raw[2], raw[3]);
	axes[2] = to_counts(raw[4], raw[5]);

	return 0;
}

/*
 * Tilt from the direction of gravity. Only valid while the board is still or
 * moving slowly: any other acceleration adds to gravity and skews the result.
 * Yaw cannot be recovered from an accelerometer alone.
 *
 * Signs depend on how the Click sits on the mikroBUS header; flip them here if
 * "tilt forward" comes out with the wrong direction.
 *
 * Results are in hundredths of a degree because printk() cannot print floats.
 */
struct tilt {
	float filt[3];
	bool primed;
};

static void tilt_update(struct tilt *t, const int16_t axes[3], int *roll_cdeg, int *pitch_cdeg)
{
	for (int i = 0; i < 3; i++) {
		if (t->primed) {
			t->filt[i] += TILT_FILTER_ALPHA * ((float)axes[i] - t->filt[i]);
		} else {
			t->filt[i] = axes[i];
		}
	}
	t->primed = true;

	float ax = t->filt[0];
	float ay = t->filt[1];
	float az = t->filt[2];

	float roll = atan2f(ay, az) * RAD_TO_DEG;
	float pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;

	*roll_cdeg = (int)(roll * 100.0f);
	*pitch_cdeg = (int)(pitch * 100.0f);
}

static void print_cdeg(const char *label, int cdeg)
{
	printk("%s %c%d.%02d deg", label, cdeg < 0 ? '-' : '+', abs(cdeg) / 100, abs(cdeg) % 100);
}

static int read_temperature(const struct accel11 *dev, int *celsius)
{
	uint8_t raw = 0;
	int ret = reg_read(dev->bus, dev->addr, BMA456_REG_TEMPERATURE, &raw, 1);

	if (ret != 0) {
		return ret;
	}

	/* Two's complement offset from 23 degrees Celsius. */
	*celsius = (int)(int8_t)raw + 23;
	return 0;
}

/* ── Diagnostics when nothing answers ─────────────────────────────────────── */

static void print_no_device_help(void)
{
	printk("\n"
	       "No BMA456 responded on i2c0 or i2c1 at 0x18 or 0x19.\n"
	       "\n"
	       "Things to check, most likely first:\n"
	       "  1. Is an Accel 11 Click actually seated on the mikroBUS header,\n"
	       "     oriented so the notch matches the silkscreen?\n"
	       "  2. Does your Libero FPGA design route an I2C controller to the\n"
	       "     mikroBUS SCL/SDA pins? Zephyr cannot know this; the board DTS\n"
	       "     enables both controllers but the fabric decides what is wired.\n"
	       "  3. SDO jumper on the Click board: low selects 0x18, high 0x19.\n"
	       "     Both are probed, so a different reading means a different chip.\n"
	       "  4. Pull-up resistors present on SCL and SDA?\n"
	       "\n"
	       "The shell is running on this console. Useful commands:\n"
	       "  device list\n"
	       "  i2c read_byte i2c@2010a000 0x18 0x00   - chip ID on controller 0\n"
	       "  i2c read_byte i2c@2010b000 0x18 0x00   - chip ID on controller 1\n"
	       "\n"
	       "Do not bother with 'i2c scan'. It probes using zero length writes,\n"
	       "which this driver always rejects with -EINVAL, so it reports an empty\n"
	       "bus even when a device is present. read_byte is the reliable check.\n");
}

/* ── Main ─────────────────────────────────────────────────────────────────── */

int main(void)
{
	printk("\n=== Accel 11 Click (Bosch BMA456) on PIC64GX1000 Curiosity Kit ===\n\n");
	printk("Probing I2C controllers and addresses:\n");

	struct accel11 dev;

	if (!detect(&dev)) {
		print_no_device_help();
		/* Return instead of spinning, so the shell stays usable. */
		return 0;
	}

	printk("\nUsing %s at address 0x%02x\n", dev.bus_name, dev.addr);

	if (accel11_init(&dev) != 0) {
		printk("Initialisation failed. The device answered the chip ID read but "
		       "not the configuration writes; check the wiring for marginal "
		       "signal quality.\n");
		return 0;
	}

	printk("Configured: ODR 100 Hz (performance mode), +/-2 g range\n");

	int temperature;

	if (read_temperature(&dev, &temperature) == 0) {
		printk("Die temperature: %d C\n", temperature);
	}

	printk("\nStreaming samples (raw counts are 12 bit, -2048..2047):\n\n");

	int consecutive_errors = 0;
	struct tilt tilt = { 0 };
	unsigned int sample_count = 0;

	while (1) {
		int ret = wait_drdy(&dev);

		if (ret == -EAGAIN) {
			printk("no data ready within 200 ms\n");
		} else if (ret != 0) {
			printk("status read failed (%d)\n", ret);
		} else {
			int16_t axes[3];

			ret = read_sample(&dev, axes);
			if (ret == 0) {
				int roll, pitch;

				/* Filter every sample, print only some of them. */
				tilt_update(&tilt, axes, &roll, &pitch);

				if (++sample_count % PRINT_EVERY_N_SAMPLES == 0) {
					print_cdeg("roll", roll);
					printk("   ");
					print_cdeg("pitch", pitch);
					printk("   (X %5d  Y %5d  Z %5d mg)\n",
					       counts_to_mg(axes[0]),
					       counts_to_mg(axes[1]),
					       counts_to_mg(axes[2]));
				}
			} else {
				printk("sample read failed (%d)\n", ret);
			}
		}

		if (ret != 0) {
			if (++consecutive_errors == 10) {
				printk("\n10 consecutive failures, giving up. The shell "
				       "remains available on this console.\n");
				return 0;
			}
			/* Back off between retries; the success path never sleeps. */
			k_msleep(100);
		} else {
			consecutive_errors = 0;
		}
	}

	return 0;
}
