/* SPDX-License-Identifier: Apache-2.0 */
/*
 * GY-521 (InvenSense MPU-6050) tilt input for Tilt Maze.
 *
 * A reader thread samples the accelerometer at 100 Hz and turns the gravity
 * vector on the X/Y axes into a smoothed tilt that the game polls once per
 * frame through gy521_get_tilt().
 *
 * The register map, detection and init sequence come from examples/i2c-gy521.
 * As there, the PIC64GX I2C driver ignores the devicetree clock-frequency
 * (i2c_configure() is mandatory) and rejects zero length transfers (so presence
 * is detected by reading WHO_AM_I).
 */

#include "gy521.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

/* ── Tuning ───────────────────────────────────────────────────────────────── */

/*
 * Which way the ball rolls for a given tilt depends on how the GY-521 is mounted
 * relative to the screen. If the ball rolls the wrong way along an axis, flip
 * its sign here. If the axes are crossed (tilt left/right moves the ball up and
 * down), set TILT_SWAP_XY to 1.
 */
#define TILT_INVERT_X   1
#define TILT_INVERT_Y   0
#define TILT_SWAP_XY    0

/* Weight of each new sample in the low-pass filter. Lower is smoother but laggier. */
#define TILT_FILTER_ALPHA  0.1f

#define SAMPLE_PERIOD_MS   10   /* 100 Hz, the sensor runs at 200 Hz */

/*
 * Set to 1 to print the filtered tilt every few samples, e.g. to check the
 * orientation. Leave it 0 for normal play: the console is slow (115200 baud).
 */
#define GY521_DEBUG_PRINT    0
#define DEBUG_PRINT_EVERY_N  10

/* ── MPU-6050 registers (subset) ──────────────────────────────────────────── */
#define MPU6050_REG_SMPLRT_DIV   0x19
#define MPU6050_REG_CONFIG       0x1A
#define MPU6050_REG_GYRO_CONFIG  0x1B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_PWR_MGMT_1   0x6B
#define MPU6050_REG_WHO_AM_I     0x75

#define MPU6050_WHO_AM_I_VAL     0x68

#define MPU6050_PWR1_DEVICE_RESET   0x80
#define MPU6050_PWR1_CLK_PLL_XGYRO  0x01

#define MPU6050_DLPF_44HZ        0x03
#define MPU6050_SMPLRT_DIV_200HZ 0x04   /* 1 kHz / (1 + 4) */
#define MPU6050_GYRO_FS_250DPS   0x00
#define MPU6050_ACCEL_FS_2G      0x00

/* +/-2 g -> 16384 LSB/g */
#define MPU6050_ACCEL_LSB_PER_G  16384.0f

/* ── State shared with the game ───────────────────────────────────────────── */

/* Tilt in micro-g, so that x and y can be shared as plain atomics. */
static atomic_t tilt_x_ug;
static atomic_t tilt_y_ug;
static atomic_t sensor_ready;

bool gy521_ready(void)
{
	return atomic_get(&sensor_ready) != 0;
}

void gy521_get_tilt(float *x, float *y)
{
	*x = (float)atomic_get(&tilt_x_ug) / 1e6f;
	*y = (float)atomic_get(&tilt_y_ug) / 1e6f;
}

/* ── I2C helpers ──────────────────────────────────────────────────────────── */

static const struct device *const buses[] = {
	DEVICE_DT_GET(DT_ALIAS(i2c0)),
	DEVICE_DT_GET(DT_ALIAS(i2c1)),
};

static const char *const bus_names[] = { "i2c0", "i2c1" };

/* AD0 low gives 0x68 (GY-521 default pull-down), AD0 high gives 0x69. */
static const uint8_t addresses[] = { 0x68, 0x69 };

struct gy521 {
	const struct device *bus;
	uint8_t addr;
};

static int reg_read(const struct gy521 *dev, uint8_t reg, uint8_t *buf, size_t len)
{
	return i2c_write_read(dev->bus, dev->addr, &reg, 1, buf, len);
}

static int reg_write(const struct gy521 *dev, uint8_t reg, uint8_t val)
{
	const uint8_t out[2] = { reg, val };

	return i2c_write(dev->bus, out, sizeof(out), dev->addr);
}

/* The bus can be busy right after a reset, so writes are retried a few times. */
static int reg_write_retry(const struct gy521 *dev, uint8_t reg, uint8_t val)
{
	int ret = -EIO;

	for (int i = 0; i < 5; i++) {
		ret = reg_write(dev, reg, val);
		if (ret == 0) {
			return 0;
		}
		k_msleep(20);
	}
	return ret;
}

/*
 * Which controller reaches the header is decided by the FPGA design, so both are
 * tried, each at both addresses.
 */
static bool detect(struct gy521 *out)
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
			struct gy521 cand = { .bus = buses[b], .addr = addresses[a] };
			uint8_t id = 0;

			/* Bit 0 and bit 7 are not part of the ID on some clones. */
			if (reg_read(&cand, MPU6050_REG_WHO_AM_I, &id, 1) == 0 &&
			    (id & 0x7E) == MPU6050_WHO_AM_I_VAL) {
				printk("[gy521] MPU-6050 on %s at 0x%02x (WHO_AM_I 0x%02x)\n",
				       bus_names[b], addresses[a], id);
				*out = cand;
				return true;
			}
		}
	}

	return false;
}

static int sensor_init(const struct gy521 *dev)
{
	int ret;

	k_msleep(50);

	ret = reg_write_retry(dev, MPU6050_REG_PWR_MGMT_1, MPU6050_PWR1_DEVICE_RESET);
	if (ret != 0) {
		return ret;
	}
	k_msleep(100);

	/* Wake up and take the PLL clock referenced to the X gyro. */
	ret = reg_write_retry(dev, MPU6050_REG_PWR_MGMT_1, MPU6050_PWR1_CLK_PLL_XGYRO);
	if (ret != 0) {
		return ret;
	}
	k_msleep(20);

	ret = reg_write_retry(dev, MPU6050_REG_CONFIG, MPU6050_DLPF_44HZ);
	if (ret != 0) {
		return ret;
	}

	ret = reg_write_retry(dev, MPU6050_REG_SMPLRT_DIV, MPU6050_SMPLRT_DIV_200HZ);
	if (ret != 0) {
		return ret;
	}

	ret = reg_write_retry(dev, MPU6050_REG_GYRO_CONFIG, MPU6050_GYRO_FS_250DPS);
	if (ret != 0) {
		return ret;
	}

	return reg_write_retry(dev, MPU6050_REG_ACCEL_CONFIG, MPU6050_ACCEL_FS_2G);
}

/* ── Sampling ─────────────────────────────────────────────────────────────── */

static inline int16_t be16(const uint8_t *p)
{
	return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/* Acceleration in g on X and Y (bytes 0..3 of the accel block, big endian). */
static int read_accel_xy(const struct gy521 *dev, float *x, float *y)
{
	uint8_t raw[6];
	int ret = reg_read(dev, MPU6050_REG_ACCEL_XOUT_H, raw, sizeof(raw));

	if (ret != 0) {
		return ret;
	}

	/*
	 * A dead or glitching bus tends to read as all zeros or all ones. Neither
	 * can be a real sample (at rest one accel axis always carries ~1 g), and
	 * both would otherwise look like a sudden tilt change.
	 */
	bool all_zero = true;
	bool all_ones = true;

	for (size_t i = 0; i < sizeof(raw); i++) {
		all_zero = all_zero && raw[i] == 0x00;
		all_ones = all_ones && raw[i] == 0xFF;
	}
	if (all_zero || all_ones) {
		return -EIO;
	}

	*x = (float)be16(&raw[0]) / MPU6050_ACCEL_LSB_PER_G;
	*y = (float)be16(&raw[2]) / MPU6050_ACCEL_LSB_PER_G;
	return 0;
}

/* ── Reader thread ────────────────────────────────────────────────────────── */

#define GY521_STACK 2048
#define GY521_PRIO  5
#define MAX_CONSECUTIVE_ERRORS 20

static K_THREAD_STACK_DEFINE(gy521_stack, GY521_STACK);
static struct k_thread gy521_thread;

static void gy521_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	struct gy521 dev;

	if (!detect(&dev)) {
		printk("[gy521] no MPU-6050 found on i2c0/i2c1 at 0x68/0x69, no tilt input\n");
		return;
	}

	if (sensor_init(&dev) != 0) {
		printk("[gy521] init failed, no tilt input\n");
		return;
	}

	atomic_set(&sensor_ready, 1);
	printk("[gy521] streaming tilt\n");

	float fx = 0.0f, fy = 0.0f;
	int errors = 0;
	unsigned int n = 0;

	while (true) {
		float ax, ay;

		if (read_accel_xy(&dev, &ax, &ay) != 0) {
			if (++errors >= MAX_CONSECUTIVE_ERRORS) {
				printk("[gy521] too many read errors, stopping\n");
				break;
			}
		} else {
			errors = 0;

			if (TILT_SWAP_XY) {
				float t = ax; ax = ay; ay = t;
			}
			if (TILT_INVERT_X) {
				ax = -ax;
			}
			if (TILT_INVERT_Y) {
				ay = -ay;
			}

			fx += TILT_FILTER_ALPHA * (ax - fx);
			fy += TILT_FILTER_ALPHA * (ay - fy);
			atomic_set(&tilt_x_ug, (atomic_val_t)(fx * 1e6f));
			atomic_set(&tilt_y_ug, (atomic_val_t)(fy * 1e6f));

			if (GY521_DEBUG_PRINT && ++n % DEBUG_PRINT_EVERY_N == 0) {
				printk("[gy521] tilt x=%d y=%d (mg)\n",
				       (int)(fx * 1000.0f), (int)(fy * 1000.0f));
			}
		}

		k_msleep(SAMPLE_PERIOD_MS);
	}

	/* Sensor lost: stop steering rather than freezing on the last tilt. */
	atomic_set(&sensor_ready, 0);
	atomic_set(&tilt_x_ug, 0);
	atomic_set(&tilt_y_ug, 0);
}

void gy521_start(void)
{
	k_tid_t tid = k_thread_create(&gy521_thread, gy521_stack,
				      K_THREAD_STACK_SIZEOF(gy521_stack),
				      gy521_fn, NULL, NULL, NULL,
				      GY521_PRIO, 0, K_NO_WAIT);

	k_thread_name_set(tid, "gy521");
}
