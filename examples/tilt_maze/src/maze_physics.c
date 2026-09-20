/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — Chipmunk2D Physics Simulation
 */

#include "maze_physics.h"
#include "maze_map.h"
#include "maze_cores.h"
#include "gy521.h"
#include <chipmunk/chipmunk.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#define PHYSICS_STACK      8192
#define PHYSICS_PRIO       5
#define PHYSICS_PERIOD_MS  16

/* Chipmunk state — owned exclusively by the physics thread after start. */
static cpSpace *space;
static cpBody  *ball_body;
static cpShape *ball_shape;

/* Published ball position, read by the game and UI threads on other cores. */
static struct k_spinlock pos_lock;
static float pub_x = MAZE_START_X;
static float pub_y = MAZE_START_Y;

static atomic_t reset_req;
static atomic_t is_active = ATOMIC_INIT(0);

static K_THREAD_STACK_DEFINE(physics_stack, PHYSICS_STACK);
static struct k_thread physics_thread;

static void publish_ball_pos(void)
{
	cpVect pos = cpBodyGetPosition(ball_body);

	K_SPINLOCK(&pos_lock) {
		pub_x = (float)pos.x;
		pub_y = (float)pos.y;
	}
}

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

void maze_physics_request_reset(void)
{
	atomic_set(&reset_req, 1);
}

bool maze_physics_reset_pending(void)
{
	return atomic_get(&reset_req) != 0;
}

void maze_physics_set_active(bool active)
{
	atomic_set(&is_active, active ? 1 : 0);
}

bool maze_physics_is_active(void)
{
	return atomic_get(&is_active) != 0;
}

/* Physics thread only. */
static void reset_ball(void)
{
	cpBodySetPosition(ball_body, cpv(MAZE_START_X, MAZE_START_Y));
	cpBodySetVelocity(ball_body, cpvzero);
	cpBodySetAngularVelocity(ball_body, 0.0f);
	publish_ball_pos();
}

static void step_physics(float tilt_x, float tilt_y, float dt)
{
	const float accel_scale = 1600.0f;

	cpSpaceSetGravity(space, cpv(tilt_x * accel_scale, tilt_y * accel_scale));

	/* Substep Chipmunk2D physics simulation for maximum numerical stability */
	cpSpaceStep(space, dt / 2.0f);
	cpSpaceStep(space, dt / 2.0f);
}

static void physics_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	int64_t next_ms = k_uptime_get();
	uint32_t last_ms = k_uptime_get_32();

	while (true) {
		uint32_t now = k_uptime_get_32();
		float dt = (now - last_ms) / 1000.0f;

		last_ms = now;
		if (dt > 0.05f) dt = 0.05f;
		if (dt <= 0.001f) dt = 0.001f;

		/* Reset is applied here so Chipmunk stays single-threaded. The flag
		 * is cleared only after the new position is published, so the game
		 * thread never sees the stale one.
		 */
		if (atomic_get(&reset_req)) {
			reset_ball();
			atomic_set(&reset_req, 0);
		}

		if (atomic_get(&is_active)) {
			float tilt_x = 0.0f, tilt_y = 0.0f;

			gy521_get_tilt(&tilt_x, &tilt_y);
			step_physics(tilt_x, tilt_y, dt);
		} else {
			/* Keep marble stationary at start position when game is paused */
			cpBodySetPosition(ball_body, cpv(MAZE_START_X, MAZE_START_Y));
			cpBodySetVelocity(ball_body, cpvzero);
			cpBodySetAngularVelocity(ball_body, 0.0f);
		}
		publish_ball_pos();

		next_ms += PHYSICS_PERIOD_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next_ms));
	}
}

void maze_physics_start(void)
{
	maze_thread_spawn(&physics_thread, physics_stack,
			  K_THREAD_STACK_SIZEOF(physics_stack),
			  physics_fn, PHYSICS_PRIO, MAZE_CORE_PHYSICS, "physics");
}

void maze_physics_get_ball_pos(float *x, float *y)
{
	K_SPINLOCK(&pos_lock) {
		if (x) *x = pub_x;
		if (y) *y = pub_y;
	}
}
