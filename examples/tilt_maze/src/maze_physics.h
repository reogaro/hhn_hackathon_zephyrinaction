/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — Chipmunk2D Physics Simulation (own thread, own core)
 */

#ifndef SRC_MAZE_PHYSICS_H
#define SRC_MAZE_PHYSICS_H

#include <stdbool.h>

/** Build the Chipmunk space. Call once, before maze_physics_start(). */
void maze_physics_init(void);

/** Start the physics thread (pinned to MAZE_CORE_PHYSICS). */
void maze_physics_start(void);

/** Latest ball position; safe to call from any thread. */
void maze_physics_get_ball_pos(float *x, float *y);

/** Ask the physics thread to put the ball back at the start. Any thread. */
void maze_physics_request_reset(void);

/** True from maze_physics_request_reset() until the reset has been applied. */
bool maze_physics_reset_pending(void);

/** Enable or pause physics simulation. When paused, ball stays at start position. */
void maze_physics_set_active(bool active);
bool maze_physics_is_active(void);

#endif /* SRC_MAZE_PHYSICS_H */
