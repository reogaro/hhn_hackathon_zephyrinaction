/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SRC_GY521_H
#define SRC_GY521_H

#include <stdbool.h>

/**
 * Start the GY-521 (InvenSense MPU-6050) reader thread.
 *
 * The thread probes i2c0/i2c1 for the sensor, configures it and then samples
 * the accelerometer and gyroscope at 100 Hz. If no sensor answers the thread
 * exits and the game keeps working on the SW2 button alone, so it is safe to
 * call without a GY-521 attached.
 */
void gy521_start(void);

/** True once the sensor has been found and is streaming. */
bool gy521_ready(void);

/**
 * Returns true once per detected flick or twist of the board, then clears the
 * event. Meant for the game tick: OR it with the SW2 button edge.
 */
bool gy521_take_flap(void);

#endif /* SRC_GY521_H */
