/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — Chipmunk2D Physics Simulation
 */

#ifndef SRC_MAZE_PHYSICS_H
#define SRC_MAZE_PHYSICS_H

void maze_physics_init(void);
void maze_physics_reset_ball(void);
void maze_physics_update(float tilt_x, float tilt_y, float dt);
void maze_physics_get_ball_pos(float *x, float *y);

#endif /* SRC_MAZE_PHYSICS_H */
