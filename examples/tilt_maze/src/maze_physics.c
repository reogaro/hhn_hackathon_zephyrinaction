/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — Chipmunk2D Physics Simulation
 */

#include "maze_physics.h"
#include "maze_map.h"
#include <chipmunk/chipmunk.h>
#include <zephyr/sys/util.h>

static cpSpace *space;
static cpBody  *ball_body;
static cpShape *ball_shape;

void maze_physics_init(void)
{
	space = cpSpaceNew();
	cpSpaceSetGravity(space, cpvzero);
	cpSpaceSetDamping(space, 0.94f);

	cpBody *static_body = cpSpaceGetStaticBody(space);

	/* 1. Perimeter Boundaries (inner edges: X: 70..730, Y: 70..530) */
	struct { cpVect a, b; } perim_segs[] = {
		{ cpv(70, 70),   cpv(730, 70) },  /* Top */
		{ cpv(70, 530),  cpv(730, 530) }, /* Bottom */
		{ cpv(70, 70),   cpv(70, 530) },  /* Left */
		{ cpv(730, 70),  cpv(730, 530) }, /* Right */
	};

	for (size_t i = 0; i < ARRAY_SIZE(perim_segs); i++) {
		cpShape *seg = cpSegmentShapeNew(static_body, perim_segs[i].a, perim_segs[i].b, 0.0f);
		cpShapeSetElasticity(seg, 0.5f);
		cpShapeSetFriction(seg, 0.4f);
		cpSpaceAddShape(space, seg);
	}

	/* 2. Interior Maze Wall Segments */
	size_t n_walls = 0;
	const maze_wall_path_t *walls = maze_map_get_walls(&n_walls);

	for (size_t i = 0; i < n_walls; i++) {
		for (int j = 0; j < walls[i].n - 1; j++) {
			cpVect p1 = cpv(walls[i].pts[j].x, walls[i].pts[j].y);
			cpVect p2 = cpv(walls[i].pts[j + 1].x, walls[i].pts[j + 1].y);
			cpShape *seg = cpSegmentShapeNew(static_body, p1, p2, walls[i].half_w);
			cpShapeSetElasticity(seg, 0.5f);
			cpShapeSetFriction(seg, 0.4f);
			cpSpaceAddShape(space, seg);
		}
	}

	/* 3. Marble Dynamic Body */
	cpFloat mass = 1.0f;
	cpFloat moment = cpMomentForCircle(mass, 0, MAZE_BALL_RADIUS, cpvzero);
	ball_body = cpSpaceAddBody(space, cpBodyNew(mass, moment));
	cpBodySetPosition(ball_body, cpv(MAZE_START_X, MAZE_START_Y));

	ball_shape = cpSpaceAddShape(space, cpCircleShapeNew(ball_body, MAZE_BALL_RADIUS, cpvzero));
	cpShapeSetElasticity(ball_shape, 0.55f);
	cpShapeSetFriction(ball_shape, 0.35f);
}

void maze_physics_reset_ball(void)
{
	if (ball_body) {
		cpBodySetPosition(ball_body, cpv(MAZE_START_X, MAZE_START_Y));
		cpBodySetVelocity(ball_body, cpvzero);
		cpBodySetAngularVelocity(ball_body, 0.0f);
	}
}

void maze_physics_update(float tilt_x, float tilt_y, float dt)
{
	if (!space || !ball_body) {
		return;
	}

	const float accel_scale = 1600.0f;
	cpSpaceSetGravity(space, cpv(tilt_x * accel_scale, tilt_y * accel_scale));

	/* Substep Chipmunk2D physics simulation for maximum numerical stability */
	cpSpaceStep(space, dt / 2.0f);
	cpSpaceStep(space, dt / 2.0f);
}

void maze_physics_get_ball_pos(float *x, float *y)
{
	if (ball_body) {
		cpVect pos = cpBodyGetPosition(ball_body);
		if (x) *x = (float)pos.x;
		if (y) *y = (float)pos.y;
	} else {
		if (x) *x = MAZE_START_X;
		if (y) *y = MAZE_START_Y;
	}
}
