/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SRC_GY521_H
#define SRC_GY521_H

#include <stdbool.h>

/**
 * Start the GY-521 (InvenSense MPU-6050) reader thread.
 *
 * The thread probes i2c0/i2c1 for the sensor, configures it and then samples
 * the accelerometer at 100 Hz. The tilt of the board is the gravity vector seen
 * on the X and Y axes. If no sensor answers the thread exits and the tilt stays
 * at zero, so it is safe to call without a GY-521 attached.
 */
void gy521_start(void);

/** True once the sensor has been found and is streaming. */
bool gy521_ready(void);

/**
 * Low-pass filtered tilt in g. With the board lying flat both are ~0, and
 * they approach +/-1 as it is tipped up on its side.
 */
void gy521_get_tilt(float *x, float *y);

#endif /* SRC_GY521_H */
