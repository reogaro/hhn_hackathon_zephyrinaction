/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — LVGL Presentation & Visual Widgets
 */

#include "maze_ui.h"
#include "maze_map.h"
#include "maze_physics.h"
#include "maze_game.h"
#include "maze_cores.h"
#include "img_gameover.h"
#include "img_victory.h"
#include "img_player.h"
#include <zephyr/kernel.h>
#include <lvgl.h>
#include <stdio.h>

#define UI_STACK      16384
#define UI_PRIO       5
#define UI_PERIOD_MS  16

/* ── Quake-esque Castle & Industrial Palette ─────────────────────────────── */
#define C_VOID            0x0B0908  /* Soot dungeon background */
#define C_FLOOR_TOP       0x241D18  /* Worn castle flagstone */
#define C_FLOOR_BOT       0x14100E  /* Deep shadow stone floor */
#define C_FLOOR_RIM       0x544030  /* Riveted rusted iron border */
#define C_WALL_BASE       0x362C24  /* Dungeon fortress masonry */
#define C_WALL_BEVEL      0x78624C  /* Chiseled sandstone / bronze edge */
#define C_PIT_VOID        0x060504  /* Bottomless dark abyss */
#define C_PIT_RIM         0xC0392B  /* Blood iron / molten slag rim */
#define C_GATE_VOID       0x241A04  /* Slipgate portal void */
#define C_GATE_RIM        0xF39C12  /* Runic slipgate gold */
#define C_GATE_TEXT       0xF1C40F  /* Glowing portal text */
#define C_PLAYER_CORE     0xE65100  /* Molten lava core */
#define C_PLAYER_RIM      0xFFD54F  /* Incandescent gold highlight */
#define C_SPAWN_RIM       0x4A3B2C  /* Floor spawn rune */
#define C_PANEL_BG        0x14110E  /* Heavy iron armor plate */
#define C_PANEL_BORDER    0x8A6D3B  /* Quake hammered bronze */
#define C_TEXT_TITLE      0xF39C12  /* Quake amber-gold */
#define C_TEXT_SUB        0xC8B89E  /* Worn parchment stone silver */
#define C_TEXT_MUTED      0x8B7E72  /* Weathered stone gray */
#define C_BTN_RED_BG      0x7A1E12  /* Blood-iron button */
#define C_BTN_RED_RIM     0xD63031  /* Hazard red border */
#define C_BTN_RED_TXT     0xFFEAA7  /* Warm golden ivory */
#define C_BTN_GREEN_BG    0x1B4324  /* Slipgate green button */
#define C_BTN_GREEN_RIM   0x2ECC71  /* Toxic green border */
#define C_HUD_BG          0x14110E  /* Industrial gauge casing */
#define C_HUD_RIM         0x4D3D2E  /* Riveted bronze bezel */
#define C_HUD_TIME        0xF39C12  /* Digital amber readout */
#define C_HUD_BEST        0xD4AC0D  /* Hazard gold */

static K_THREAD_STACK_DEFINE(ui_stack, UI_STACK);
static struct k_thread ui_thread;

#define N_MAX_WALLS 16
static lv_point_precise_t maze_line_pts[N_MAX_WALLS][MAZE_MAX_WALL_PTS];

static lv_obj_t *platform_obj;
static lv_obj_t *ball_obj;
static lv_obj_t *status_panel;
static lv_obj_t *status_img;
static lv_obj_t *badge_lbl;
static lv_obj_t *status_lbl;
static lv_obj_t *sub_lbl;
static lv_obj_t *prompt_btn;
static lv_obj_t *prompt_lbl;
static lv_obj_t *gemini_badge;
static lv_obj_t *hud_panel;
static lv_obj_t *hud_time_lbl;
static lv_obj_t *hud_best_lbl;

static void maze_ui_init(void)
{
	lv_obj_t *scr = lv_screen_active();
	if (!scr) {
		scr = lv_obj_create(NULL);
		lv_screen_load(scr);
	}

	lv_obj_set_style_bg_color(scr, lv_color_hex(C_VOID), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

	/* Platform Container */
	platform_obj = lv_obj_create(scr);
	lv_obj_set_pos(platform_obj, MAZE_OFFSET_X + MAZE_PLATFORM_X, MAZE_OFFSET_Y + MAZE_PLATFORM_Y);
	lv_obj_set_size(platform_obj, MAZE_PLATFORM_W, MAZE_PLATFORM_H);

	/* Quake Castle Flagstone Floor */
	lv_obj_set_style_bg_color(platform_obj, lv_color_hex(C_FLOOR_TOP), 0);
	lv_obj_set_style_bg_grad_color(platform_obj, lv_color_hex(C_FLOOR_BOT), 0);
	lv_obj_set_style_bg_grad_dir(platform_obj, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_border_color(platform_obj, lv_color_hex(C_FLOOR_RIM), 0);
	lv_obj_set_style_border_width(platform_obj, 4, 0);
	lv_obj_set_style_radius(platform_obj, 6, 0);
	lv_obj_set_style_pad_all(platform_obj, 0, 0);
	lv_obj_set_scrollbar_mode(platform_obj, LV_SCROLLBAR_MODE_OFF);

	/* 0. Start Spawn Marker on floor */
	lv_obj_t *spawn_obj = lv_obj_create(platform_obj);
	int32_t sp_r = (int32_t)MAZE_BALL_RADIUS + 6;
	lv_obj_set_pos(spawn_obj, (int32_t)(MAZE_START_X - sp_r - MAZE_PLATFORM_X),
				   (int32_t)(MAZE_START_Y - sp_r - MAZE_PLATFORM_Y));
	lv_obj_set_size(spawn_obj, sp_r * 2, sp_r * 2);
	lv_obj_set_style_radius(spawn_obj, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(spawn_obj, lv_color_hex(0x1A1511), 0);
	lv_obj_set_style_bg_opa(spawn_obj, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(spawn_obj, lv_color_hex(C_SPAWN_RIM), 0);
	lv_obj_set_style_border_width(spawn_obj, 2, 0);
	lv_obj_set_style_pad_all(spawn_obj, 0, 0);
	lv_obj_set_scrollbar_mode(spawn_obj, LV_SCROLLBAR_MODE_OFF);

	/* 1. Perimeter Fortified Walls */
	size_t n_perim = 0;
	const maze_rect_t *perim = maze_map_get_perimeter(&n_perim);
	for (size_t i = 0; i < n_perim; i++) {
		lv_obj_t *w = lv_obj_create(platform_obj);
		lv_obj_set_pos(w, (int32_t)(perim[i].x - MAZE_PLATFORM_X),
				  (int32_t)(perim[i].y - MAZE_PLATFORM_Y));
		lv_obj_set_size(w, (int32_t)perim[i].w, (int32_t)perim[i].h);
		lv_obj_set_style_bg_color(w, lv_color_hex(C_WALL_BASE), 0);
		lv_obj_set_style_border_color(w, lv_color_hex(C_WALL_BEVEL), 0);
		lv_obj_set_style_border_width(w, 2, 0);
		lv_obj_set_style_radius(w, 2, 0);
		lv_obj_set_style_pad_all(w, 0, 0);
		lv_obj_set_scrollbar_mode(w, LV_SCROLLBAR_MODE_OFF);
	}

	/* 2. Interior Maze Walls with 3D bronze highlight bevel */
	size_t n_walls = 0;
	const maze_wall_path_t *walls = maze_map_get_walls(&n_walls);
	for (size_t i = 0; i < n_walls && i < N_MAX_WALLS; i++) {
		for (int j = 0; j < walls[i].n; j++) {
			maze_line_pts[i][j].x = (lv_value_precise_t)(walls[i].pts[j].x - MAZE_PLATFORM_X);
			maze_line_pts[i][j].y = (lv_value_precise_t)(walls[i].pts[j].y - MAZE_PLATFORM_Y);
		}

		/* Main Wall Line (dark dungeon masonry) */
		lv_obj_t *line = lv_line_create(platform_obj);
		lv_line_set_points(line, maze_line_pts[i], walls[i].n);
		lv_obj_set_style_line_width(line, (int32_t)(walls[i].half_w * 2), 0);
		lv_obj_set_style_line_color(line, lv_color_hex(C_WALL_BASE), 0);
		lv_obj_set_style_line_rounded(line, true, 0);

		/* 3D Highlight Bevel Line (chiseled bronze ridge) */
		lv_obj_t *hi = lv_line_create(platform_obj);
		lv_line_set_points(hi, maze_line_pts[i], walls[i].n);
		lv_obj_set_style_line_width(hi, (int32_t)((walls[i].half_w - 3.0f) * 2), 0);
		lv_obj_set_style_line_color(hi, lv_color_hex(C_WALL_BEVEL), 0);
		lv_obj_set_style_line_rounded(hi, true, 0);
	}

	/* 3. Bottomless Trap Pits */
	size_t n_pits = 0;
	const maze_hole_t *pits = maze_map_get_pits(&n_pits);
	for (size_t i = 0; i < n_pits; i++) {
		lv_obj_t *pit = lv_obj_create(platform_obj);
		int32_t r_outer = (int32_t)pits[i].r + 3;
		lv_obj_set_pos(pit, (int32_t)(pits[i].x - r_outer - MAZE_PLATFORM_X),
				    (int32_t)(pits[i].y - r_outer - MAZE_PLATFORM_Y));
		lv_obj_set_size(pit, r_outer * 2, r_outer * 2);
		lv_obj_set_style_radius(pit, LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_bg_color(pit, lv_color_hex(C_PIT_VOID), 0);
		lv_obj_set_style_border_color(pit, lv_color_hex(C_PIT_RIM), 0);
		lv_obj_set_style_border_width(pit, 3, 0);
		lv_obj_set_style_pad_all(pit, 0, 0);
		lv_obj_set_scrollbar_mode(pit, LV_SCROLLBAR_MODE_OFF);
	}

	/* 4. Slipgate Extraction Portal (Exit Gate) */
	const maze_hole_t *goal = maze_map_get_goal();
	lv_obj_t *goal_obj = lv_obj_create(platform_obj);
	int32_t g_outer = (int32_t)goal->r + 6;
	lv_obj_set_pos(goal_obj, (int32_t)(goal->x - g_outer - MAZE_PLATFORM_X),
				 (int32_t)(goal->y - g_outer - MAZE_PLATFORM_Y));
	lv_obj_set_size(goal_obj, g_outer * 2, g_outer * 2);
	lv_obj_set_style_radius(goal_obj, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(goal_obj, lv_color_hex(C_GATE_VOID), 0);
	lv_obj_set_style_border_color(goal_obj, lv_color_hex(C_GATE_RIM), 0);
	lv_obj_set_style_border_width(goal_obj, 3, 0);
	lv_obj_set_style_pad_all(goal_obj, 0, 0);
	lv_obj_set_scrollbar_mode(goal_obj, LV_SCROLLBAR_MODE_OFF);

	lv_obj_t *star_lbl = lv_label_create(goal_obj);
	lv_label_set_text(star_lbl, "EXIT");
	lv_obj_set_style_text_font(star_lbl, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(star_lbl, lv_color_hex(C_GATE_TEXT), 0);
	lv_obj_align(star_lbl, LV_ALIGN_CENTER, 0, 0);

	/* 5. Player Character: PIC8 Microchip Sprite */
	ball_obj = lv_image_create(platform_obj);
	lv_image_set_src(ball_obj, &img_player);
	lv_image_set_antialias(ball_obj, false);
	lv_obj_set_scrollbar_mode(ball_obj, LV_SCROLLBAR_MODE_OFF);

	/* 6. Status Overlay Panel (Start screen, Game Over, Victory) */
	status_panel = lv_obj_create(scr);
	lv_obj_set_size(status_panel, 880, 260);
	lv_obj_align(status_panel, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_color(status_panel, lv_color_hex(C_PANEL_BG), 0);
	lv_obj_set_style_bg_opa(status_panel, LV_OPA_90, 0);
	lv_obj_set_style_border_color(status_panel, lv_color_hex(C_PANEL_BORDER), 0);
	lv_obj_set_style_border_width(status_panel, 3, 0);
	lv_obj_set_style_radius(status_panel, 8, 0);
	lv_obj_set_style_pad_all(status_panel, 0, 0);
	lv_obj_set_scrollbar_mode(status_panel, LV_SCROLLBAR_MODE_OFF);

	status_img = lv_image_create(status_panel);
	lv_image_set_antialias(status_img, false);
	lv_obj_add_flag(status_img, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_scrollbar_mode(status_img, LV_SCROLLBAR_MODE_OFF);

	badge_lbl = lv_label_create(status_panel);
	lv_label_set_text(badge_lbl, "");
	lv_obj_set_style_text_font(badge_lbl, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(badge_lbl, lv_color_hex(0x9E7844), 0);
	lv_obj_align(badge_lbl, LV_ALIGN_CENTER, 0, -82);

	status_lbl = lv_label_create(status_panel);
	lv_label_set_text(status_lbl, "");
	lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_32, 0);
	lv_obj_set_style_text_color(status_lbl, lv_color_hex(C_TEXT_TITLE), 0);
	lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, -46);

	sub_lbl = lv_label_create(status_panel);
	lv_label_set_text(sub_lbl, "");
	lv_obj_set_style_text_font(sub_lbl, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(sub_lbl, lv_color_hex(C_TEXT_SUB), 0);
	lv_obj_align(sub_lbl, LV_ALIGN_CENTER, 0, 6);

	prompt_btn = lv_obj_create(status_panel);
	lv_obj_set_size(prompt_btn, 480, 48);
	lv_obj_align(prompt_btn, LV_ALIGN_CENTER, 0, 72);
	lv_obj_set_style_bg_color(prompt_btn, lv_color_hex(C_BTN_RED_BG), 0);
	lv_obj_set_style_bg_opa(prompt_btn, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(prompt_btn, lv_color_hex(C_BTN_RED_RIM), 0);
	lv_obj_set_style_border_width(prompt_btn, 2, 0);
	lv_obj_set_style_radius(prompt_btn, 6, 0);
	lv_obj_set_scrollbar_mode(prompt_btn, LV_SCROLLBAR_MODE_OFF);

	prompt_lbl = lv_label_create(prompt_btn);
	lv_label_set_text(prompt_lbl, "");
	lv_obj_set_style_text_font(prompt_lbl, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(prompt_lbl, lv_color_hex(C_BTN_RED_TXT), 0);
	lv_obj_align(prompt_lbl, LV_ALIGN_CENTER, 0, 0);

	/* 7. Permanent Gemini Attribution Badge (Bottom Left) */
	gemini_badge = lv_label_create(scr);
	lv_label_set_text(gemini_badge, "graphics with help of google gemini");
	lv_obj_set_style_text_font(gemini_badge, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(gemini_badge, lv_color_hex(0xB0A696), 0);
	lv_obj_align(gemini_badge, LV_ALIGN_BOTTOM_LEFT, 20, -12);

	/* 7. Top-Right Timer HUD Card (Industrial gauge styling) */
	hud_panel = lv_obj_create(scr);
	lv_obj_set_size(hud_panel, 230, 72);
	lv_obj_align(hud_panel, LV_ALIGN_TOP_RIGHT, -25, 20);
	lv_obj_set_style_bg_color(hud_panel, lv_color_hex(C_HUD_BG), 0);
	lv_obj_set_style_bg_opa(hud_panel, LV_OPA_90, 0);
	lv_obj_set_style_border_color(hud_panel, lv_color_hex(C_HUD_RIM), 0);
	lv_obj_set_style_border_width(hud_panel, 2, 0);
	lv_obj_set_style_radius(hud_panel, 6, 0);
	lv_obj_set_style_pad_hor(hud_panel, 14, 0);
	lv_obj_set_style_pad_ver(hud_panel, 8, 0);
	lv_obj_set_scrollbar_mode(hud_panel, LV_SCROLLBAR_MODE_OFF);

	hud_time_lbl = lv_label_create(hud_panel);
	lv_label_set_text(hud_time_lbl, "TIME: 00:00.0");
	lv_obj_set_style_text_font(hud_time_lbl, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(hud_time_lbl, lv_color_hex(C_HUD_TIME), 0);
	lv_obj_align(hud_time_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

	hud_best_lbl = lv_label_create(hud_panel);
	lv_label_set_text(hud_best_lbl, "BEST: --:--.-");
	lv_obj_set_style_text_font(hud_best_lbl, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(hud_best_lbl, lv_color_hex(C_HUD_BEST), 0);
	lv_obj_align(hud_best_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void format_time(char *buf, size_t sz, uint32_t ms)
{
	if (ms == 0) {
		snprintf(buf, sz, "--:--.-");
		return;
	}
	uint32_t mins = ms / 60000;
	uint32_t secs = (ms % 60000) / 1000;
	uint32_t tenths = (ms % 1000) / 100;
	snprintf(buf, sz, "%02u:%02u.%u", mins, secs, tenths);
}

static void maze_ui_set_ball_pos(float x, float y)
{
	if (ball_obj) {
		lv_obj_set_pos(ball_obj,
			       (int32_t)(x - (img_player.header.w / 2.0f) - MAZE_PLATFORM_X),
			       (int32_t)(y - (img_player.header.h / 2.0f) - MAZE_PLATFORM_Y));
	}
}

static void maze_ui_update_screen(maze_state_t state)
{
	switch (state) {
	case MAZE_STATE_START:
		lv_image_set_scale(status_img, LV_SCALE_NONE);
		lv_image_set_pivot(status_img, 0, 0);
		lv_obj_set_size(status_panel, 880, 260);
		lv_obj_align(status_panel, LV_ALIGN_CENTER, 0, 0);
		lv_obj_set_style_bg_color(status_panel, lv_color_hex(C_PANEL_BG), 0);
		lv_obj_set_style_bg_opa(status_panel, LV_OPA_90, 0);
		lv_obj_set_style_border_color(status_panel, lv_color_hex(C_PANEL_BORDER), 0);
		lv_obj_set_style_border_width(status_panel, 3, 0);
		lv_obj_set_style_radius(status_panel, 8, 0);

		lv_obj_add_flag(status_img, LV_OBJ_FLAG_HIDDEN);

		lv_obj_clear_flag(badge_lbl, LV_OBJ_FLAG_HIDDEN);
		lv_label_set_text(badge_lbl, "[ CASTLE MICROCHIP ]");
		lv_obj_set_style_text_color(badge_lbl, lv_color_hex(C_TEXT_SUB), 0);
		lv_obj_align(badge_lbl, LV_ALIGN_CENTER, 0, -82);

		lv_obj_clear_flag(status_lbl, LV_OBJ_FLAG_HIDDEN);
		lv_label_set_text(status_lbl, "ESCAPE FROM CASTLE MICROCHIP");
		lv_obj_set_style_text_color(status_lbl, lv_color_hex(C_TEXT_TITLE), 0);
		lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, -46);

		lv_obj_clear_flag(sub_lbl, LV_OBJ_FLAG_HIDDEN);
		lv_label_set_text(sub_lbl, "TILT THE CONTROLLER TO ROLL");
		lv_obj_set_style_text_color(sub_lbl, lv_color_hex(C_TEXT_MUTED), 0);
		lv_obj_align(sub_lbl, LV_ALIGN_CENTER, 0, 6);

		lv_obj_set_size(prompt_btn, 480, 48);
		lv_obj_align(prompt_btn, LV_ALIGN_CENTER, 0, 72);
		lv_obj_set_style_bg_color(prompt_btn, lv_color_hex(C_BTN_RED_BG), 0);
		lv_obj_set_style_bg_opa(prompt_btn, LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(prompt_btn, lv_color_hex(C_BTN_RED_RIM), 0);
		lv_obj_set_style_border_width(prompt_btn, 2, 0);
		lv_obj_set_style_radius(prompt_btn, 6, 0);

		lv_label_set_text(prompt_lbl, "PRESS THE RED BUTTON TO START");
		lv_obj_set_style_text_color(prompt_lbl, lv_color_hex(C_BTN_RED_TXT), 0);
		lv_obj_align(prompt_lbl, LV_ALIGN_CENTER, 0, 0);

		lv_obj_clear_flag(status_panel, LV_OBJ_FLAG_HIDDEN);
		break;

	case MAZE_STATE_PLAYING:
		lv_obj_add_flag(status_panel, LV_OBJ_FLAG_HIDDEN);
		break;

	case MAZE_STATE_GAME_OVER: {
		/* Full-screen transparent container with floating banner & text */
		lv_obj_set_size(status_panel, 1280, 720);
		lv_obj_set_pos(status_panel, 0, 0);
		lv_obj_set_style_bg_opa(status_panel, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_width(status_panel, 0, 0);

		lv_obj_add_flag(badge_lbl, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(status_lbl, LV_OBJ_FLAG_HIDDEN);

		/* Defeat graphic banner covering 2/3rds screen height (480px) with pixelated sharp look */
		lv_image_set_src(status_img, &img_gameover);
		lv_image_set_pivot(status_img, img_gameover.header.w / 2, 0);
		lv_image_set_scale(status_img, 559); /* 220px * (559/256) ≈ 480px (2/3 of 720) */
		lv_image_set_antialias(status_img, false);
		lv_obj_align(status_img, LV_ALIGN_TOP_MID, 0, 20);
		lv_obj_clear_flag(status_img, LV_OBJ_FLAG_HIDDEN);

		/* Floating subtitle below banner in bright pure white */
		char tstr[16], sub_msg[48];
		format_time(tstr, sizeof(tstr), maze_game_get_time_ms());
		snprintf(sub_msg, sizeof(sub_msg), "SURVIVED: %s", tstr);
		lv_label_set_text(sub_lbl, sub_msg);
		lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0xFFFFFF), 0);
		lv_obj_align(sub_lbl, LV_ALIGN_TOP_MID, 0, 520);
		lv_obj_clear_flag(sub_lbl, LV_OBJ_FLAG_HIDDEN);

		/* Floating prompt text below subtitle in bright pure white */
		lv_obj_set_size(prompt_btn, 600, 48);
		lv_obj_align(prompt_btn, LV_ALIGN_TOP_MID, 0, 570);
		lv_obj_set_style_bg_opa(prompt_btn, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_width(prompt_btn, 0, 0);

		lv_label_set_text(prompt_lbl, "PRESS BUTTON TO RETRY");
		lv_obj_set_style_text_color(prompt_lbl, lv_color_hex(0xFFFFFF), 0);
		lv_obj_align(prompt_lbl, LV_ALIGN_CENTER, 0, 0);

		lv_obj_clear_flag(status_panel, LV_OBJ_FLAG_HIDDEN);
		break;
	}

	case MAZE_STATE_VICTORY: {
		/* Full-screen transparent container with floating banner & text */
		lv_obj_set_size(status_panel, 1280, 720);
		lv_obj_set_pos(status_panel, 0, 0);
		lv_obj_set_style_bg_opa(status_panel, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_width(status_panel, 0, 0);

		lv_obj_add_flag(badge_lbl, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(status_lbl, LV_OBJ_FLAG_HIDDEN);

		/* Victory graphic banner covering 2/3rds screen height (480px) with pixelated sharp look */
		lv_image_set_src(status_img, &img_victory);
		lv_image_set_pivot(status_img, img_victory.header.w / 2, 0);
		lv_image_set_scale(status_img, 690); /* 178px * (690/256) ≈ 480px (2/3 of 720) */
		lv_image_set_antialias(status_img, false);
		lv_obj_align(status_img, LV_ALIGN_TOP_MID, 0, 20);
		lv_obj_clear_flag(status_img, LV_OBJ_FLAG_HIDDEN);

		/* Floating subtitle below banner in bright pure white */
		char tstr[16], bstr[16], sub_msg[96];
		format_time(tstr, sizeof(tstr), maze_game_get_time_ms());
		format_time(bstr, sizeof(bstr), maze_game_get_best_time_ms());

		if (maze_game_is_new_best()) {
			snprintf(sub_msg, sizeof(sub_msg), "NEW RECORD!  TIME: %s  |  BEST: %s", tstr, bstr);
		} else {
			snprintf(sub_msg, sizeof(sub_msg), "ESCAPE TIME: %s  |  BEST: %s", tstr, bstr);
		}
		lv_label_set_text(sub_lbl, sub_msg);
		lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0xFFFFFF), 0);
		lv_obj_align(sub_lbl, LV_ALIGN_TOP_MID, 0, 520);
		lv_obj_clear_flag(sub_lbl, LV_OBJ_FLAG_HIDDEN);

		/* Floating prompt text below subtitle in bright pure white */
		lv_obj_set_size(prompt_btn, 600, 48);
		lv_obj_align(prompt_btn, LV_ALIGN_TOP_MID, 0, 570);
		lv_obj_set_style_bg_opa(prompt_btn, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_width(prompt_btn, 0, 0);

		lv_label_set_text(prompt_lbl, "PRESS BUTTON TO RETURN TO TITLE");
		lv_obj_set_style_text_color(prompt_lbl, lv_color_hex(0xFFFFFF), 0);
		lv_obj_align(prompt_lbl, LV_ALIGN_CENTER, 0, 0);

		lv_obj_clear_flag(status_panel, LV_OBJ_FLAG_HIDDEN);
		break;
	}
	}
}

/* Pull the latest ball position, timer and game state from the physics/game threads. */
static void maze_ui_sync_cb(lv_timer_t *timer)
{
	static maze_state_t shown_state = (maze_state_t)-1;
	static uint32_t last_sync_tenths = (uint32_t)-1;

	ARG_UNUSED(timer);

	float x, y;

	maze_physics_get_ball_pos(&x, &y);
	maze_ui_set_ball_pos(x, y);

	maze_state_t wanted_state = maze_game_get_state();

	if (wanted_state != shown_state) {
		shown_state = wanted_state;
		maze_ui_update_screen(wanted_state);
	}

	/* Update top-right HUD timer */
	uint32_t cur_ms = maze_game_get_time_ms();
	uint32_t cur_tenths = cur_ms / 100;

	if (cur_tenths != last_sync_tenths || wanted_state != shown_state) {
		last_sync_tenths = cur_tenths;

		char tbuf[32], bbuf[32], bstr[16];
		uint32_t best_ms = maze_game_get_best_time_ms();

		if (wanted_state == MAZE_STATE_START && cur_ms == 0) {
			snprintf(tbuf, sizeof(tbuf), "TIME: 00:00.0");
		} else {
			uint32_t m = cur_ms / 60000;
			uint32_t s = (cur_ms % 60000) / 1000;
			uint32_t t = (cur_ms % 1000) / 100;
			snprintf(tbuf, sizeof(tbuf), "TIME: %02u:%02u.%u", m, s, t);
		}
		lv_label_set_text(hud_time_lbl, tbuf);

		format_time(bstr, sizeof(bstr), best_ms);
		snprintf(bbuf, sizeof(bbuf), "BEST: %s", bstr);
		lv_label_set_text(hud_best_lbl, bbuf);
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
