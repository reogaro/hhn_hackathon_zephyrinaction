/* SPDX-License-Identifier: Apache-2.0
 *
 * GY-521 (InvenSense MPU-6050) motion input for Flappy Microchip.
 *
 * A reader thread samples the accelerometer and gyroscope at 100 Hz and turns a
 * sharp flick or twist of the board into a "flap" event. The game polls
 * gy521_take_flap() from its 33 ms tick and treats it like a press of SW2.
 *
 * The register map, detection and init sequence come from examples/i2c-gy521.
 * As there, the PIC64GX I2C driver ignores the devicetree clock-frequency
 * (i2c_configure() is mandatory) and rejects zero length transfers (so presence
 * is detected by reading WHO_AM_I).
 *
 * Both detectors are orientation independent, they look at the size of a vector
 * and not at any one axis:
 *
 *   flick  the acceleration vector. At rest it is 1 g (gravity). A linear flick
 *          pushes it well above or below 1 g for a few samples.
 *   twist  the angular rate vector. At rest it is ~0 dps. Turning the board
 *          quickly, like a wrist flick, pushes it well above that.
 *
 * Either one raises a flap, so the player can use whichever motion feels right.
 */

#include "gy521.h"

#include <math.h>

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
 * How fast the board has to turn to count as a twist. The range is +/-250 dps
 * per axis, so the largest useful value is a bit under 430 dps (all three axes
 * saturated).
 */
#define TWIST_THRESHOLD_DPS  150

/*
 * Ignore further motion for this long after a flap fired. A flick has a push and
 * a braking phase, both of which cross the threshold; this keeps it to one flap.
 */
#define FLAP_HOLDOFF_MS      250

#define SAMPLE_PERIOD_MS     10   /* matches the 100 Hz output data rate */

/*
 * Set to 1 to print the acceleration magnitude (mg) and angular rate (dps) every
 * few samples. Use it to see what "at rest", "handling" and "flap" look like on
 * your board and pick the thresholds from real numbers. Leave it 0 for normal
 * play: the console is slow (115200 baud) and printing costs time.
 */
#define GY521_DEBUG_PRINT    0
#define DEBUG_PRINT_EVERY_N  5

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
#define MPU6050_SMPLRT_DIV_200HZ 0x04   /* 1 kHz / (1 + 4); sampled at 100 Hz */
#define MPU6050_GYRO_FS_250DPS   0x00
#define MPU6050_ACCEL_FS_2G      0x00

/* +/-2 g -> 16384 LSB/g, +/-250 dps -> 131.072 LSB/dps */
#define MPU6050_ACCEL_LSB_PER_G  16384
#define MPU6050_GYRO_LSB_PER_DPS 131

/* ── State shared with the game ───────────────────────────────────────────── */
static atomic_t flap_pending;
static atomic_t sensor_ready;

bool gy521_ready(void)
{
	return atomic_get(&sensor_ready) != 0;
}

bool gy521_take_flap(void)
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

/* One burst read: accel X/Y/Z, temperature, gyro X/Y/Z (14 bytes, big endian). */
static int read_sample(const struct gy521 *dev, int accel_mg[3], int gyro_dps[3])
{
	uint8_t raw[14];
	int ret = reg_read(dev, MPU6050_REG_ACCEL_XOUT_H, raw, sizeof(raw));

	if (ret != 0) {
		return ret;
	}

	/*
	 * A dead or glitching bus tends to read as all zeros or all ones. Neither
	 * can be a real sample (at rest one accel axis always carries ~1 g), and both
	 * would otherwise look like a spike or a free fall to the detectors.
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

	for (int i = 0; i < 3; i++) {
		accel_mg[i] = ((int)be16(&raw[2 * i]) * 1000) / MPU6050_ACCEL_LSB_PER_G;
		gyro_dps[i] = (int)be16(&raw[8 + 2 * i]) / MPU6050_GYRO_LSB_PER_DPS;
	}
	return 0;
}

/*
 * Squared magnitude. The largest possible accel sum is 3 * 2000^2 = 12e6 (mg^2),
 * the largest gyro sum 3 * 250^2 = 187500 (dps^2), both well inside an int32, and
 * comparing squares avoids a square root.
 */
static int mag2_of(const int v[3])
{
	return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
}

/* Median of three: drops a single wild sample without adding any lag. */
static int median3(int a, int b, int c)
{
	if (a > b) {
		int t = a; a = b; b = t;
	}
	if (b > c) {
		b = c;
	}
	return a > b ? a : b;
}

/* True when |a| is more than FLICK_THRESHOLD_MG away from 1 g (squared input). */
static bool is_flick(int accel_mag2)
{
	const int hi = 1000 + FLICK_THRESHOLD_MG;
	const int lo = 1000 - FLICK_THRESHOLD_MG;

	return accel_mag2 > hi * hi || accel_mag2 < lo * lo;
}

/* True when the board turns faster than TWIST_THRESHOLD_DPS (squared input). */
static bool is_twist(int gyro_mag2)
{
	return gyro_mag2 > TWIST_THRESHOLD_DPS * TWIST_THRESHOLD_DPS;
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
		printk("[gy521] no MPU-6050 found, SW2 is the only input\n");
		return;
	}

	if (sensor_init(&dev) != 0) {
		printk("[gy521] init failed, SW2 is the only input\n");
		return;
	}

	atomic_set(&sensor_ready, 1);
	printk("[gy521] streaming, flick threshold %d mg, twist threshold %d dps\n",
	       FLICK_THRESHOLD_MG, TWIST_THRESHOLD_DPS);

	int64_t last_flap = 0;
	int errors = 0;
	int accel_hist[3] = { 1000 * 1000, 1000 * 1000, 1000 * 1000 };  /* start at 1 g */
	int gyro_hist[3] = { 0, 0, 0 };                                 /* start at rest */
	unsigned int n = 0;

	while (true) {
		int accel_mg[3];
		int gyro_dps[3];

		if (read_sample(&dev, accel_mg, gyro_dps) != 0) {
			if (++errors >= MAX_CONSECUTIVE_ERRORS) {
				printk("[gy521] too many read errors, stopping\n");
				break;
			}
		} else {
			errors = 0;

			/*
			 * Judge the median of the last three samples, so a motion has to
			 * show up in at least two consecutive samples (20 ms). A lone
			 * spike from an I2C glitch or a knock does not qualify.
			 */
			accel_hist[n % 3] = mag2_of(accel_mg);
			gyro_hist[n % 3] = mag2_of(gyro_dps);
			n++;

			int accel_mag2 = median3(accel_hist[0], accel_hist[1], accel_hist[2]);
			int gyro_mag2 = median3(gyro_hist[0], gyro_hist[1], gyro_hist[2]);

			if (GY521_DEBUG_PRINT && n % DEBUG_PRINT_EVERY_N == 0) {
				printk("[gy521] |a| %d mg  |w| %d dps (median)\n",
				       (int)sqrtf((float)accel_mag2),
				       (int)sqrtf((float)gyro_mag2));
			}

			int64_t now = k_uptime_get();

			if ((is_flick(accel_mag2) || is_twist(gyro_mag2)) &&
			    now - last_flap >= FLAP_HOLDOFF_MS) {
				last_flap = now;
				atomic_set(&flap_pending, 1);
			}
		}

		k_msleep(SAMPLE_PERIOD_MS);
	}

	atomic_set(&sensor_ready, 0);
}

void gy521_start(void)
{
	k_tid_t tid = k_thread_create(&gy521_thread, gy521_stack,
				      K_THREAD_STACK_SIZEOF(gy521_stack),
				      gy521_fn, NULL, NULL, NULL,
				      GY521_PRIO, 0, K_NO_WAIT);

	k_thread_name_set(tid, "gy521");
}
