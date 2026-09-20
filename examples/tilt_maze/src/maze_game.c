/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — Game Rules
 *
 * Watches the ball position published by the physics thread, applies the pit
 * and goal rules and tells the UI which banner to show. It never touches
 * LVGL or Chipmunk directly.
 */

#include "maze_game.h"
#include "maze_map.h"
#include "maze_physics.h"
#include "maze_cores.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <math.h>

#define GAME_STACK         4096
#define GAME_PRIO          5
#define GAME_PERIOD_MS     16
#define BANNER_DURATION_MS 2000

static K_THREAD_STACK_DEFINE(game_stack, GAME_STACK);
static struct k_thread game_thread;

static atomic_t banner = MAZE_BANNER_NONE;
static uint32_t banner_expire_ms;

static void show_banner(maze_banner_t b, uint32_t now)
{
	atomic_set(&banner, b);
	banner_expire_ms = now + BANNER_DURATION_MS;
}

static void game_step(uint32_t now)
{
	/* Handle banner expiration */
	if (banner_expire_ms && (int32_t)(now - banner_expire_ms) >= 0) {
		atomic_set(&banner, MAZE_BANNER_NONE);
		banner_expire_ms = 0;
	}

	/* The ball is still where it died until physics applies the reset. */
	if (maze_physics_reset_pending()) {
		return;
	}

	float ball_x, ball_y;

	maze_physics_get_ball_pos(&ball_x, &ball_y);

	/* Check pit collisions */
	size_t n_pits = 0;
	const maze_hole_t *pits = maze_map_get_pits(&n_pits);

	for (size_t i = 0; i < n_pits; i++) {
		float dx = ball_x - pits[i].x;
		float dy = ball_y - pits[i].y;

		if (sqrtf(dx * dx + dy * dy) < pits[i].r) {
			printk("[maze] Ball fell in hole %d — restarting\n", (int)i);
			maze_physics_request_reset();
			show_banner(MAZE_BANNER_HOLE, now);
			return;
		}
	}

	/* Check goal condition */
	const maze_hole_t *goal = maze_map_get_goal();
	float gx = ball_x - goal->x;
	float gy = ball_y - goal->y;

	if (sqrtf(gx * gx + gy * gy) < goal->r) {
		printk("[maze] Goal reached!\n");
		maze_physics_request_reset();
		show_banner(MAZE_BANNER_GOAL, now);
	}
}

static void game_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	int64_t next_ms = k_uptime_get();

	while (true) {
		game_step(k_uptime_get_32());

		next_ms += GAME_PERIOD_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next_ms));
	}
}

void maze_game_init(void)
{
	atomic_set(&banner, MAZE_BANNER_NONE);
	banner_expire_ms = 0;
}

void maze_game_start(void)
{
	maze_thread_spawn(&game_thread, game_stack,
			  K_THREAD_STACK_SIZEOF(game_stack),
			  game_fn, GAME_PRIO, MAZE_CORE_GAME, "game");
}

maze_banner_t maze_game_get_banner(void)
{
	return (maze_banner_t)atomic_get(&banner);
}
