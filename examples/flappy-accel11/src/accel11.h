/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SRC_ACCEL11_H
#define SRC_ACCEL11_H

#include <stdbool.h>

/**
 * Start the Accel 11 Click (Bosch BMA456) reader thread.
 *
 * The thread probes i2c0/i2c1 for the sensor, configures it and then samples
 * at 100 Hz. If no sensor answers the thread exits and the game keeps working
 * on the SW2 button alone, so it is safe to call without a Click attached.
 */
void accel11_start(void);

/** True once the sensor has been found and is streaming. */
bool accel11_ready(void);

/**
 * Returns true once per detected flick of the board, then clears the event.
 * Meant for the game tick: OR it with the SW2 button edge.
 */
bool accel11_take_flap(void);

#endif /* SRC_ACCEL11_H */
