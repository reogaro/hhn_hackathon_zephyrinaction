/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — LVGL Presentation & Visual Widgets
 */

#include "maze_ui.h"
#include "maze_map.h"
#include "maze_physics.h"
#include "maze_game.h"
#include "maze_cores.h"
#include <zephyr/kernel.h>
#include <lvgl.h>

#define UI_STACK      16384
#define UI_PRIO       5
#define UI_PERIOD_MS  16

static K_THREAD_STACK_DEFINE(ui_stack, UI_STACK);
static struct k_thread ui_thread;

#define N_MAX_WALLS 16
static lv_point_precise_t maze_line_pts[N_MAX_WALLS][MAZE_MAX_WALL_PTS];

static lv_obj_t *platform_obj;
static lv_obj_t *ball_obj;
static lv_obj_t *banner_obj;

static void maze_ui_init(void)
{
	lv_obj_t *scr = lv_screen_active();
	if (!scr) {
		scr = lv_obj_create(NULL);
		lv_screen_load(scr);
	}

	lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D1117), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

	/* Platform Container */
	platform_obj = lv_obj_create(scr);
	lv_obj_set_pos(platform_obj, MAZE_OFFSET_X + MAZE_PLATFORM_X, MAZE_OFFSET_Y + MAZE_PLATFORM_Y);
	lv_obj_set_size(platform_obj, MAZE_PLATFORM_W, MAZE_PLATFORM_H);

	/* Native Vertical Gradient: 0x2196D8 (cyan-blue) to 0x5A2D96 (purple) */
	lv_obj_set_style_bg_color(platform_obj, lv_color_hex(0x2196D8), 0);
	lv_obj_set_style_bg_grad_color(platform_obj, lv_color_hex(0x5A2D96), 0);
	lv_obj_set_style_bg_grad_dir(platform_obj, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_border_color(platform_obj, lv_color_hex(0x1E2850), 0);
	lv_obj_set_style_border_width(platform_obj, 3, 0);
	lv_obj_set_style_radius(platform_obj, 6, 0);
	lv_obj_set_style_pad_all(platform_obj, 0, 0);
	lv_obj_set_scrollbar_mode(platform_obj, LV_SCROLLBAR_MODE_OFF);

	/* 1. Perimeter Walls */
	size_t n_perim = 0;
	const maze_rect_t *perim = maze_map_get_perimeter(&n_perim);
	for (size_t i = 0; i < n_perim; i++) {
		lv_obj_t *w = lv_obj_create(platform_obj);
		lv_obj_set_pos(w, (int32_t)(perim[i].x - MAZE_PLATFORM_X),
				  (int32_t)(perim[i].y - MAZE_PLATFORM_Y));
		lv_obj_set_size(w, (int32_t)perim[i].w, (int32_t)perim[i].h);
		lv_obj_set_style_bg_color(w, lv_color_hex(0x415FA5), 0);
		lv_obj_set_style_border_color(w, lv_color_hex(0x8CAFE1), 0);
		lv_obj_set_style_border_width(w, 2, 0);
		lv_obj_set_style_radius(w, 2, 0);
		lv_obj_set_style_pad_all(w, 0, 0);
		lv_obj_set_scrollbar_mode(w, LV_SCROLLBAR_MODE_OFF);
	}

	/* 2. Interior Maze Walls using native lv_line */
	size_t n_walls = 0;
	const maze_wall_path_t *walls = maze_map_get_walls(&n_walls);
	for (size_t i = 0; i < n_walls && i < N_MAX_WALLS; i++) {
		for (int j = 0; j < walls[i].n; j++) {
			maze_line_pts[i][j].x = (lv_value_precise_t)(walls[i].pts[j].x - MAZE_PLATFORM_X);
			maze_line_pts[i][j].y = (lv_value_precise_t)(walls[i].pts[j].y - MAZE_PLATFORM_Y);
		}

		/* Main Wall Line */
		lv_obj_t *line = lv_line_create(platform_obj);
		lv_line_set_points(line, maze_line_pts[i], walls[i].n);
		lv_obj_set_style_line_width(line, (int32_t)(walls[i].half_w * 2), 0);
		lv_obj_set_style_line_color(line, lv_color_hex(0x415FA5), 0);
		lv_obj_set_style_line_rounded(line, true, 0);

		/* 3D Highlight Bevel Line */
		lv_obj_t *hi = lv_line_create(platform_obj);
		lv_line_set_points(hi, maze_line_pts[i], walls[i].n);
		lv_obj_set_style_line_width(hi, (int32_t)((walls[i].half_w - 3.0f) * 2), 0);
		lv_obj_set_style_line_color(hi, lv_color_hex(0x8CAFE1), 0);
		lv_obj_set_style_line_rounded(hi, true, 0);
	}

	/* 3. Pits / Traps */
	size_t n_pits = 0;
	const maze_hole_t *pits = maze_map_get_pits(&n_pits);
	for (size_t i = 0; i < n_pits; i++) {
		lv_obj_t *pit = lv_obj_create(platform_obj);
		int32_t r_outer = (int32_t)pits[i].r + 3;
		lv_obj_set_pos(pit, (int32_t)(pits[i].x - r_outer - MAZE_PLATFORM_X),
				    (int32_t)(pits[i].y - r_outer - MAZE_PLATFORM_Y));
		lv_obj_set_size(pit, r_outer * 2, r_outer * 2);
		lv_obj_set_style_radius(pit, LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_bg_color(pit, lv_color_hex(0x0F0F12), 0);
		lv_obj_set_style_border_color(pit, lv_color_hex(0xC83C3C), 0);
		lv_obj_set_style_border_width(pit, 3, 0);
		lv_obj_set_style_pad_all(pit, 0, 0);
		lv_obj_set_scrollbar_mode(pit, LV_SCROLLBAR_MODE_OFF);
	}

	/* 4. Goal Area */
	const maze_hole_t *goal = maze_map_get_goal();
	lv_obj_t *goal_obj = lv_obj_create(platform_obj);
	int32_t g_outer = (int32_t)goal->r + 6;
	lv_obj_set_pos(goal_obj, (int32_t)(goal->x - g_outer - MAZE_PLATFORM_X),
				 (int32_t)(goal->y - g_outer - MAZE_PLATFORM_Y));
	lv_obj_set_size(goal_obj, g_outer * 2, g_outer * 2);
	lv_obj_set_style_radius(goal_obj, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(goal_obj, lv_color_hex(0x1B4B2E), 0);
	lv_obj_set_style_border_color(goal_obj, lv_color_hex(0x3CC864), 0);
	lv_obj_set_style_border_width(goal_obj, 3, 0);
	lv_obj_set_style_pad_all(goal_obj, 0, 0);
	lv_obj_set_scrollbar_mode(goal_obj, LV_SCROLLBAR_MODE_OFF);

	lv_obj_t *star_lbl = lv_label_create(goal_obj);
	lv_label_set_text(star_lbl, "GOAL");
	lv_obj_set_style_text_font(star_lbl, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(star_lbl, lv_color_hex(0x3CC864), 0);
	lv_obj_align(star_lbl, LV_ALIGN_CENTER, 0, 0);

	/* 5. Marble / Ball */
	ball_obj = lv_obj_create(platform_obj);
	lv_obj_set_size(ball_obj, (int32_t)(MAZE_BALL_RADIUS * 2), (int32_t)(MAZE_BALL_RADIUS * 2));
	lv_obj_set_style_radius(ball_obj, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(ball_obj, lv_color_hex(0xE72C1C), 0);
	lv_obj_set_style_border_color(ball_obj, lv_color_hex(0xFF7F7F), 0);
	lv_obj_set_style_border_width(ball_obj, 2, 0);
	lv_obj_set_style_pad_all(ball_obj, 0, 0);
	lv_obj_set_scrollbar_mode(ball_obj, LV_SCROLLBAR_MODE_OFF);

	/* 6. Banner Message */
	banner_obj = lv_label_create(scr);
	lv_obj_set_style_text_font(banner_obj, &lv_font_montserrat_32, 0);
	lv_obj_set_style_text_color(banner_obj, lv_color_hex(0xE6EDF3), 0);
	lv_obj_align(banner_obj, LV_ALIGN_TOP_MID, 0, 10);
	lv_obj_add_flag(banner_obj, LV_OBJ_FLAG_HIDDEN);
}

static void maze_ui_set_ball_pos(float x, float y)
{
	if (ball_obj) {
		lv_obj_set_pos(ball_obj,
			       (int32_t)(x - MAZE_BALL_RADIUS - MAZE_PLATFORM_X),
			       (int32_t)(y - MAZE_BALL_RADIUS - MAZE_PLATFORM_Y));
	}
}

static void maze_ui_show_banner(const char *msg)
{
	if (banner_obj && msg) {
		lv_label_set_text(banner_obj, msg);
		lv_obj_align(banner_obj, LV_ALIGN_TOP_MID, 0, 10);
		lv_obj_remove_flag(banner_obj, LV_OBJ_FLAG_HIDDEN);
	}
}

static void maze_ui_hide_banner(void)
{
	if (banner_obj) {
		lv_obj_add_flag(banner_obj, LV_OBJ_FLAG_HIDDEN);
	}
}

/* Pull the latest ball position and banner from the physics/game threads. */
static void maze_ui_sync_cb(lv_timer_t *timer)
{
	static maze_banner_t shown = MAZE_BANNER_NONE;

	ARG_UNUSED(timer);

	float x, y;

	maze_physics_get_ball_pos(&x, &y);
	maze_ui_set_ball_pos(x, y);

	maze_banner_t wanted = maze_game_get_banner();

	if (wanted != shown) {
		shown = wanted;
		switch (wanted) {
		case MAZE_BANNER_HOLE:
			maze_ui_show_banner("You fell in a hole! Restarting...");
			break;
		case MAZE_BANNER_GOAL:
			maze_ui_show_banner("You made it out! Restarting...");
			break;
		default:
			maze_ui_hide_banner();
			break;
		}
	}
}

/* The only thread that ever touches LVGL. */
static void ui_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	maze_ui_init();
	lv_timer_create(maze_ui_sync_cb, UI_PERIOD_MS, NULL);

	while (true) {
		uint32_t sleep_ms = lv_timer_handler();

		if (sleep_ms > UI_PERIOD_MS) {
			sleep_ms = UI_PERIOD_MS;
		} else if (sleep_ms == 0) {
			sleep_ms = 1;
		}
		k_msleep(sleep_ms);
	}
}

void maze_ui_start(void)
{
	maze_thread_spawn(&ui_thread, ui_stack, K_THREAD_STACK_SIZEOF(ui_stack),
			  ui_fn, UI_PRIO, MAZE_CORE_UI, "ui");
}
