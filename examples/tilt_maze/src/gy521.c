/* SPDX-License-Identifier: Apache-2.0 */
/*
 * GY-521 (InvenSense MPU-6050) tilt input for Tilt Maze.
 *
 * Uses Zephyr's standard sensor subsystem (<zephyr/drivers/sensor.h>)
 * and the upstream MPU-6050 driver (CONFIG_MPU6050=y).
 *
 * A reader thread samples the accelerometer at 100 Hz and turns the gravity
 * vector on the X/Y axes into a smoothed tilt that the game polls once per
 * frame through gy521_get_tilt(). It runs pinned to its own core.
 */

#include "gy521.h"
#include "maze_cores.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

/* ── Tuning ───────────────────────────────────────────────────────────────── */
#define TILT_INVERT_X   1
#define TILT_INVERT_Y   1
#define TILT_SWAP_XY    1

/* Weight of each new sample in the low-pass filter. Lower is smoother but laggier. */
#define TILT_FILTER_ALPHA  0.1f

#define SAMPLE_PERIOD_MS   10   /* 100 Hz */

/* Standard gravity in m/s^2 (Zephyr sensor subsystem reports m/s^2) */
#define GRAVITY_MSS        9.80665f

#define GY521_DEBUG_PRINT    0
#define DEBUG_PRINT_EVERY_N  10

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

/* ── Reader thread ────────────────────────────────────────────────────────── */

#define GY521_STACK 2048
#define GY521_PRIO  5
#define MAX_CONSECUTIVE_ERRORS 20

static K_THREAD_STACK_DEFINE(gy521_stack, GY521_STACK);
static struct k_thread gy521_thread;

static void gy521_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	const struct device *const dev = DEVICE_DT_GET_ANY(invensense_mpu6050);

	if (!dev || !device_is_ready(dev)) {
		printk("[gy521] MPU-6050 sensor device not ready / not found\n");
		return;
	}

	atomic_set(&sensor_ready, 1);
	printk("[gy521] MPU-6050 ready via Zephyr sensor subsystem, streaming tilt\n");

	float fx = 0.0f, fy = 0.0f;
	int errors = 0;
	unsigned int n = 0;
	struct sensor_value accel[3];

	while (true) {
		int ret = sensor_sample_fetch_chan(dev, SENSOR_CHAN_ACCEL_XYZ);
		if (ret == 0) {
			ret = sensor_channel_get(dev, SENSOR_CHAN_ACCEL_XYZ, accel);
		}

		if (ret != 0) {
			if (++errors >= MAX_CONSECUTIVE_ERRORS) {
				printk("[gy521] too many sensor read errors (%d), stopping\n", ret);
				break;
			}
		} else {
			errors = 0;

			/* Convert m/s^2 to g */
			float ax = (float)sensor_value_to_double(&accel[0]) / GRAVITY_MSS;
			float ay = (float)sensor_value_to_double(&accel[1]) / GRAVITY_MSS;

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
	maze_thread_spawn(&gy521_thread, gy521_stack,
			  K_THREAD_STACK_SIZEOF(gy521_stack),
			  gy521_fn, GY521_PRIO, MAZE_CORE_SENSOR, "gy521");
}
