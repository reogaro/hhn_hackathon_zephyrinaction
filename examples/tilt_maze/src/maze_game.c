/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — Game State Machine & LVGL 60 Hz Timer Callback
 */

#include "maze_game.h"
#include "maze_map.h"
#include "maze_physics.h"
#include "maze_ui.h"
#include "gy521.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <lvgl.h>
#include <math.h>

#define BANNER_DURATION_MS 2000

static uint32_t last_tick_ms;
static uint32_t banner_expire_ms;

static void maze_game_step_timer_cb(lv_timer_t *timer)
{
	ARG_UNUSED(timer);

	uint32_t now = k_uptime_get_32();

	/* Handle banner expiration */
	if (banner_expire_ms && (int32_t)(now - banner_expire_ms) >= 0) {
		maze_ui_hide_banner();
		banner_expire_ms = 0;
	}

	float dt = (now - last_tick_ms) / 1000.0f;
	last_tick_ms = now;
	if (dt > 0.05f) dt = 0.05f;
	if (dt <= 0.001f) dt = 0.001f;

	/* 1. Poll smoothed tilt from sensor */
	float tilt_x = 0.0f, tilt_y = 0.0f;
	gy521_get_tilt(&tilt_x, &tilt_y);

	/* 2. Step Chipmunk2D physics */
	maze_physics_update(tilt_x, tilt_y, dt);

	/* 3. Query current ball position */
	float ball_x = 0.0f, ball_y = 0.0f;
	maze_physics_get_ball_pos(&ball_x, &ball_y);

	/* 4. Check pit collisions */
	size_t n_pits = 0;
	const maze_hole_t *pits = maze_map_get_pits(&n_pits);
	for (size_t i = 0; i < n_pits; i++) {
		float dx = ball_x - pits[i].x;
		float dy = ball_y - pits[i].y;
		if (sqrtf(dx * dx + dy * dy) < pits[i].r) {
			printk("[maze] Ball fell in hole %d — restarting\n", (int)i);
			maze_physics_reset_ball();
			maze_physics_get_ball_pos(&ball_x, &ball_y);
			maze_ui_show_banner("You fell in a hole! Restarting...");
			banner_expire_ms = now + BANNER_DURATION_MS;
			break;
		}
	}

	/* 5. Check goal condition */
	const maze_hole_t *goal = maze_map_get_goal();
	float gx = ball_x - goal->x;
	float gy = ball_y - goal->y;
	if (sqrtf(gx * gx + gy * gy) < goal->r) {
		printk("[maze] Goal reached!\n");
		maze_physics_reset_ball();
		maze_physics_get_ball_pos(&ball_x, &ball_y);
		maze_ui_show_banner("You made it out! Restarting...");
		banner_expire_ms = now + BANNER_DURATION_MS;
	}

	/* 6. Update ball position on LVGL scene graph */
	maze_ui_set_ball_pos(ball_x, ball_y);
}

void maze_game_init(void)
{
	last_tick_ms = k_uptime_get_32();
	banner_expire_ms = 0;

	/* Create 60 Hz (~16 ms period) LVGL timer callback */
	lv_timer_create(maze_game_step_timer_cb, 16, NULL);
}
