/* SPDX-License-Identifier: Apache-2.0
 *
 * Accel 11 Click (Bosch BMA456) motion input for Flappy Microchip.
 *
 * A reader thread samples the accelerometer at 100 Hz and turns a sharp flick
 * of the board into a "flap" event. The game polls accel11_take_flap() from its
 * 33 ms tick and treats it like a press of SW2.
 *
 * The register access, detection and init sequence come from
 * examples/i2c-accel11. Zephyr's in-tree bma4xx driver rejects the BMA456 chip
 * ID (0x16), so the chip is driven directly through the I2C API. As there, the
 * PIC64GX I2C driver ignores the devicetree clock-frequency (i2c_configure() is
 * mandatory) and rejects zero length transfers (so presence is detected by
 * reading the chip ID).
 *
 * Flick detection is orientation independent: it looks at the size of the
 * acceleration vector, not at any one axis. At rest that is 1 g (gravity). A
 * flick pushes it well above or below 1 g for a few samples.
 */

#include "accel11.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

/* ── Tuning ───────────────────────────────────────────────────────────────── */

/*
 * How far the acceleration magnitude has to move away from 1 g to count as a
 * flick. Lower is more sensitive; too low and normal handling of the board
 * flaps the player. The range is +/-2 g per axis.
 */
#define FLICK_THRESHOLD_MG   600

/*
 * Ignore further flicks for this long after one fired. A flick has a push and
 * a braking phase, both of which cross the threshold; this keeps it to one flap.
 */
#define FLICK_HOLDOFF_MS     250

#define SAMPLE_PERIOD_MS     10   /* matches the 100 Hz output data rate */

/* ── BMA456 registers (subset) ────────────────────────────────────────────── */
#define BMA456_REG_CHIP_ID      0x00
#define BMA456_REG_ACC_X_LSB    0x12
#define BMA456_REG_ACC_CONF     0x40
#define BMA456_REG_ACC_RANGE    0x41
#define BMA456_REG_POWER_CONF   0x7C
#define BMA456_REG_POWER_CTRL   0x7D
#define BMA456_REG_CMD          0x7E

#define BMA456_CHIP_ID          0x16
#define BMA456_CMD_SOFTRESET    0xB6

/* Performance mode | normal filter, 4x averaging | ODR 100 Hz */
#define BMA456_ACC_CONF_DEFAULT 0xA8
#define BMA456_ACC_RANGE_2G     0x00
#define BMA456_POWER_CTRL_ACC_EN 0x04
#define BMA456_POWER_CONF_DEFAULT 0x00

/* 12 bit output, left aligned in a 16 bit word; +/-2 g full scale. */
#define BMA456_RAW_BITS         12
#define BMA456_RANGE_MG         2000

/* ── State shared with the game ───────────────────────────────────────────── */
static atomic_t flap_pending;
static atomic_t sensor_ready;

bool accel11_ready(void)
{
	return atomic_get(&sensor_ready) != 0;
}

bool accel11_take_flap(void)
{
	/* atomic_clear() returns the previous value, so this consumes the event. */
	return atomic_clear(&flap_pending) != 0;
}

/* ── I2C helpers ──────────────────────────────────────────────────────────── */

static const struct device *const buses[] = {
	DEVICE_DT_GET(DT_ALIAS(i2c0)),
	DEVICE_DT_GET(DT_ALIAS(i2c1)),
};

static const char *const bus_names[] = { "i2c0", "i2c1" };

/* SDO low gives 0x18 (Click default), SDO high gives 0x19. */
static const uint8_t addresses[] = { 0x18, 0x19 };

struct accel11 {
	const struct device *bus;
	uint8_t addr;
};

static int reg_read(const struct accel11 *dev, uint8_t reg, uint8_t *buf, size_t len)
{
	return i2c_write_read(dev->bus, dev->addr, &reg, 1, buf, len);
}

static int reg_write(const struct accel11 *dev, uint8_t reg, uint8_t val)
{
	const uint8_t out[2] = { reg, val };

	return i2c_write(dev->bus, out, sizeof(out), dev->addr);
}

/*
 * Which controller reaches the mikroBUS header is decided by the FPGA design,
 * so both are tried, each at both addresses.
 */
static bool detect(struct accel11 *out)
{
	for (size_t b = 0; b < ARRAY_SIZE(buses); b++) {
		if (!device_is_ready(buses[b])) {
			continue;
		}

		if (i2c_configure(buses[b],
				  I2C_SPEED_SET(I2C_SPEED_STANDARD) | I2C_MODE_CONTROLLER) != 0) {
			continue;
		}

		for (size_t a = 0; a < ARRAY_SIZE(addresses); a++) {
			struct accel11 cand = { .bus = buses[b], .addr = addresses[a] };
			uint8_t id = 0;

			if (reg_read(&cand, BMA456_REG_CHIP_ID, &id, 1) == 0 &&
			    id == BMA456_CHIP_ID) {
				printk("[accel11] BMA456 on %s at 0x%02x\n",
				       bus_names[b], addresses[a]);
				*out = cand;
				return true;
			}
		}
	}

	return false;
}

static int sensor_init(const struct accel11 *dev)
{
	int ret = reg_write(dev, BMA456_REG_CMD, BMA456_CMD_SOFTRESET);

	if (ret != 0) {
		return ret;
	}
	k_msleep(500);   /* the reference driver waits this long after a reset */

	ret = reg_write(dev, BMA456_REG_POWER_CTRL, BMA456_POWER_CTRL_ACC_EN);
	if (ret != 0) {
		return ret;
	}
	k_msleep(10);

	ret = reg_write(dev, BMA456_REG_POWER_CONF, BMA456_POWER_CONF_DEFAULT);
	if (ret != 0) {
		return ret;
	}
	k_msleep(10);

	ret = reg_write(dev, BMA456_REG_ACC_CONF, BMA456_ACC_CONF_DEFAULT);
	if (ret != 0) {
		return ret;
	}

	return reg_write(dev, BMA456_REG_ACC_RANGE, BMA456_ACC_RANGE_2G);
}

/* ── Sampling ─────────────────────────────────────────────────────────────── */

static inline int counts_to_mg(uint8_t lsb, uint8_t msb)
{
	int16_t word = (int16_t)(((uint16_t)msb << 8) | lsb);
	int16_t counts = (int16_t)(word >> (16 - BMA456_RAW_BITS));

	return ((int)counts * BMA456_RANGE_MG) >> (BMA456_RAW_BITS - 1);
}

static int read_mg(const struct accel11 *dev, int mg[3])
{
	uint8_t raw[6];
	int ret = reg_read(dev, BMA456_REG_ACC_X_LSB, raw, sizeof(raw));

	if (ret != 0) {
		return ret;
	}

	mg[0] = counts_to_mg(raw[0], raw[1]);
	mg[1] = counts_to_mg(raw[2], raw[3]);
	mg[2] = counts_to_mg(raw[4], raw[5]);
	return 0;
}

/*
 * True when |a| is more than FLICK_THRESHOLD_MG away from 1 g. Compares squared
 * magnitudes so no square root (and no FPU state) is needed. The largest
 * possible sum is 3 * 2000^2 = 12e6, which fits an int32.
 */
static bool is_flick(const int mg[3])
{
	const int hi = 1000 + FLICK_THRESHOLD_MG;
	const int lo = 1000 - FLICK_THRESHOLD_MG;
	int mag2 = mg[0] * mg[0] + mg[1] * mg[1] + mg[2] * mg[2];

	return mag2 > hi * hi || mag2 < lo * lo;
}

/* ── Reader thread ────────────────────────────────────────────────────────── */

#define ACCEL_STACK 2048
#define ACCEL_PRIO  5
#define MAX_CONSECUTIVE_ERRORS 20

static K_THREAD_STACK_DEFINE(accel_stack, ACCEL_STACK);
static struct k_thread accel_thread;

static void accel_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	struct accel11 dev;

	if (!detect(&dev)) {
		printk("[accel11] no BMA456 found, SW2 is the only input\n");
		return;
	}

	if (sensor_init(&dev) != 0) {
		printk("[accel11] init failed, SW2 is the only input\n");
		return;
	}

	atomic_set(&sensor_ready, 1);
	printk("[accel11] streaming, flick threshold %d mg\n", FLICK_THRESHOLD_MG);

	int64_t last_flap = 0;
	int errors = 0;

	while (true) {
		int mg[3];

		if (read_mg(&dev, mg) != 0) {
			if (++errors >= MAX_CONSECUTIVE_ERRORS) {
				printk("[accel11] too many read errors, stopping\n");
				break;
			}
		} else {
			errors = 0;

			int64_t now = k_uptime_get();

			if (is_flick(mg) && now - last_flap >= FLICK_HOLDOFF_MS) {
				last_flap = now;
				atomic_set(&flap_pending, 1);
			}
		}

		k_msleep(SAMPLE_PERIOD_MS);
	}

	atomic_set(&sensor_ready, 0);
}

void accel11_start(void)
{
	k_tid_t tid = k_thread_create(&accel_thread, accel_stack,
				      K_THREAD_STACK_SIZEOF(accel_stack),
				      accel_fn, NULL, NULL, NULL,
				      ACCEL_PRIO, 0, K_NO_WAIT);

	k_thread_name_set(tid, "accel11");
}
