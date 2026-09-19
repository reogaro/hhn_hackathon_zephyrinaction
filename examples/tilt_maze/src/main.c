/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — 2D Marble Labyrinth for PIC64GX Curiosity Kit
 *
 * Controlled by a GY-521 (InvenSense MPU-6050) IMU over I2C.
 * Rendered using native LVGL v9 scene graph objects and styles (zero static canvas buffers).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <chipmunk/chipmunk.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#include "gy521.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define WIN_W 1280
#define WIN_H  720
#define GAME_W 800
#define GAME_H 600
#define GAME_OFFSET_X ((WIN_W - GAME_W) / 2)
#define GAME_OFFSET_Y ((WIN_H - GAME_H) / 2)

#define BALL_RADIUS 14.0f
#define PLATFORM_X  50
#define PLATFORM_Y  50
#define PLATFORM_W 700
#define PLATFORM_H 500

/* Banner duration after restart */
#define BANNER_MS 2000

typedef struct { float x, y; } Vec2;
typedef struct { float x, y, w, h; } Rect;
typedef struct { float x, y, r; } Hole;

#define MAX_PTS 24
typedef struct {
	Vec2 pts[MAX_PTS];
	int n;
	float half_w;
} WallPath;

static void path_add(WallPath *p, float x, float y)
{
	if (p->n < MAX_PTS) {
		p->pts[p->n].x = x;
		p->pts[p->n].y = y;
		p->n++;
	}
}

static Rect perimeter[] = {
	{ PLATFORM_X, PLATFORM_Y, PLATFORM_W, 20 },
	{ PLATFORM_X, PLATFORM_Y + PLATFORM_H - 20, PLATFORM_W, 20 },
	{ PLATFORM_X, PLATFORM_Y, 20, PLATFORM_H },
	{ PLATFORM_X + PLATFORM_W - 20, PLATFORM_Y, 20, PLATFORM_H },
};
#define N_PERIM ((int)(sizeof(perimeter) / sizeof(perimeter[0])))

#define N_MAZE_WALLS 10
static WallPath maze_walls[N_MAZE_WALLS];

static void build_maze(void)
{
	float hw = 10.0f;
	int i = 0;

	maze_walls[i].half_w = hw;
	path_add(&maze_walls[i], 70, 162);
	path_add(&maze_walls[i], 640, 162);
	i++;

	maze_walls[i].half_w = hw;
	path_add(&maze_walls[i], 730, 220);
	path_add(&maze_walls[i], 480, 220);
	path_add(&maze_walls[i], 480, 270);
	path_add(&maze_walls[i], 250, 270);
	path_add(&maze_walls[i], 250, 220);
	path_add(&maze_walls[i], 170, 220);
	i++;

	maze_walls[i].half_w = hw;
	path_add(&maze_walls[i], 70, 346);
	path_add(&maze_walls[i], 640, 346);
	i++;

	maze_walls[i].half_w = hw;
	path_add(&maze_walls[i], 190, 438);
	path_add(&maze_walls[i], 730, 438);
	i++;

	maze_walls[i].half_w = hw;
	path_add(&maze_walls[i], 430, 132);
	path_add(&maze_walls[i], 430, 162);
	i++;

	maze_walls[i].half_w = hw;
	path_add(&maze_walls[i], 300, 438);
	path_add(&maze_walls[i], 300, 478);
	i++;

	maze_walls[i].half_w = hw;
	path_add(&maze_walls[i], 550, 438);
	path_add(&maze_walls[i], 550, 478);
	i++;

	maze_walls[i].half_w = 9.0f;
	path_add(&maze_walls[i], 490, 70);
	path_add(&maze_walls[i], 490, 105);
	i++;

	maze_walls[i].half_w = 9.0f;
	path_add(&maze_walls[i], 570, 152);
	path_add(&maze_walls[i], 570, 117);
	i++;

	maze_walls[i].half_w = 9.0f;
	path_add(&maze_walls[i], 650, 70);
	path_add(&maze_walls[i], 650, 105);
	i++;
}

static Hole pits[] = {
	{ 214, 91,  13 },
	{ 337, 125, 13 },
	{ 359, 215, 13 },
	{ 110, 253, 13 },
	{ 210, 302, 13 },
	{ 535, 264, 13 },
	{ 643, 273, 13 },
	{ 702, 348, 13 },
	{ 121, 440, 13 },
	{ 263, 367, 13 },
};
#define N_PITS ((int)(sizeof(pits) / sizeof(pits[0])))

static Hole goal = { 685, 490, 16 };
static const float START_X = 100.0f, START_Y = 110.0f;

/* Static points buffer for LVGL lines */
static lv_point_precise_t maze_line_pts[N_MAZE_WALLS][MAX_PTS];

/* LVGL UI Object handles */
static lv_obj_t *platform_obj;
static lv_obj_t *ball_obj;
static lv_obj_t *banner_obj;

/* ── Chipmunk2D Physics Simulation ────────────────────────────────────────── */
static cpSpace *physics_space;
static cpBody  *ball_body;
static cpShape *ball_shape;

static void init_physics(void)
{
	physics_space = cpSpaceNew();
	cpSpaceSetGravity(physics_space, cpvzero);
	cpSpaceSetDamping(physics_space, 0.94f);

	cpBody *static_body = cpSpaceGetStaticBody(physics_space);

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
		cpSpaceAddShape(physics_space, seg);
	}

	/* 2. Interior Maze Wall Segments */
	for (int i = 0; i < N_MAZE_WALLS; i++) {
		for (int j = 0; j < maze_walls[i].n - 1; j++) {
			cpVect p1 = cpv(maze_walls[i].pts[j].x, maze_walls[i].pts[j].y);
			cpVect p2 = cpv(maze_walls[i].pts[j + 1].x, maze_walls[i].pts[j + 1].y);
			cpShape *seg = cpSegmentShapeNew(static_body, p1, p2, maze_walls[i].half_w);
			cpShapeSetElasticity(seg, 0.5f);
			cpShapeSetFriction(seg, 0.4f);
			cpSpaceAddShape(physics_space, seg);
		}
	}

	/* 3. Marble Dynamic Body */
	cpFloat mass = 1.0f;
	cpFloat moment = cpMomentForCircle(mass, 0, BALL_RADIUS, cpvzero);
	ball_body = cpSpaceAddBody(physics_space, cpBodyNew(mass, moment));
	cpBodySetPosition(ball_body, cpv(START_X, START_Y));

	ball_shape = cpSpaceAddShape(physics_space, cpCircleShapeNew(ball_body, BALL_RADIUS, cpvzero));
	cpShapeSetElasticity(ball_shape, 0.55f);
	cpShapeSetFriction(ball_shape, 0.35f);
}

static void reset_ball(void)
{
	cpBodySetPosition(ball_body, cpv(START_X, START_Y));
	cpBodySetVelocity(ball_body, cpvzero);
	cpBodySetAngularVelocity(ball_body, 0.0f);
}

/* ── Build LVGL Scene Graph ───────────────────────────────────────────────── */
static void create_maze_gui(void)
{
	lv_obj_t *scr = lv_screen_active();
	lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D1117), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

	/* Platform Container */
	platform_obj = lv_obj_create(scr);
	lv_obj_set_pos(platform_obj, GAME_OFFSET_X + PLATFORM_X, GAME_OFFSET_Y + PLATFORM_Y);
	lv_obj_set_size(platform_obj, PLATFORM_W, PLATFORM_H);

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
	for (int i = 0; i < N_PERIM; i++) {
		lv_obj_t *w = lv_obj_create(platform_obj);
		lv_obj_set_pos(w, (int32_t)(perimeter[i].x - PLATFORM_X),
				  (int32_t)(perimeter[i].y - PLATFORM_Y));
		lv_obj_set_size(w, (int32_t)perimeter[i].w, (int32_t)perimeter[i].h);
		lv_obj_set_style_bg_color(w, lv_color_hex(0x415FA5), 0);
		lv_obj_set_style_border_color(w, lv_color_hex(0x8CAFE1), 0);
		lv_obj_set_style_border_width(w, 2, 0);
		lv_obj_set_style_radius(w, 2, 0);
		lv_obj_set_style_pad_all(w, 0, 0);
		lv_obj_set_scrollbar_mode(w, LV_SCROLLBAR_MODE_OFF);
	}

	/* 2. Interior Maze Walls using native lv_line */
	for (int i = 0; i < N_MAZE_WALLS; i++) {
		for (int j = 0; j < maze_walls[i].n; j++) {
			maze_line_pts[i][j].x = (lv_value_precise_t)(maze_walls[i].pts[j].x - PLATFORM_X);
			maze_line_pts[i][j].y = (lv_value_precise_t)(maze_walls[i].pts[j].y - PLATFORM_Y);
		}

		/* Main Wall Line */
		lv_obj_t *line = lv_line_create(platform_obj);
		lv_line_set_points(line, maze_line_pts[i], maze_walls[i].n);
		lv_obj_set_style_line_width(line, (int32_t)(maze_walls[i].half_w * 2), 0);
		lv_obj_set_style_line_color(line, lv_color_hex(0x415FA5), 0);
		lv_obj_set_style_line_rounded(line, true, 0);

		/* 3D Highlight Bevel Line */
		lv_obj_t *hi = lv_line_create(platform_obj);
		lv_line_set_points(hi, maze_line_pts[i], maze_walls[i].n);
		lv_obj_set_style_line_width(hi, (int32_t)((maze_walls[i].half_w - 3.0f) * 2), 0);
		lv_obj_set_style_line_color(hi, lv_color_hex(0x8CAFE1), 0);
		lv_obj_set_style_line_rounded(hi, true, 0);
	}

	/* 3. Pits / Traps */
	for (int i = 0; i < N_PITS; i++) {
		lv_obj_t *pit = lv_obj_create(platform_obj);
		int32_t r_outer = (int32_t)pits[i].r + 3;
		lv_obj_set_pos(pit, (int32_t)(pits[i].x - r_outer - PLATFORM_X),
				    (int32_t)(pits[i].y - r_outer - PLATFORM_Y));
		lv_obj_set_size(pit, r_outer * 2, r_outer * 2);
		lv_obj_set_style_radius(pit, LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_bg_color(pit, lv_color_hex(0x0F0F12), 0);
		lv_obj_set_style_border_color(pit, lv_color_hex(0xC83C3C), 0);
		lv_obj_set_style_border_width(pit, 3, 0);
		lv_obj_set_style_pad_all(pit, 0, 0);
		lv_obj_set_scrollbar_mode(pit, LV_SCROLLBAR_MODE_OFF);
	}

	/* 4. Goal Area */
	lv_obj_t *goal_obj = lv_obj_create(platform_obj);
	int32_t g_outer = (int32_t)goal.r + 6;
	lv_obj_set_pos(goal_obj, (int32_t)(goal.x - g_outer - PLATFORM_X),
				 (int32_t)(goal.y - g_outer - PLATFORM_Y));
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
	lv_obj_set_size(ball_obj, (int32_t)(BALL_RADIUS * 2), (int32_t)(BALL_RADIUS * 2));
	lv_obj_set_style_radius(ball_obj, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(ball_obj, lv_color_hex(0xF0C83C), 0);
	lv_obj_set_style_border_color(ball_obj, lv_color_hex(0xFFE680), 0);
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

int main(void)
{
	printk("[tilt_maze] Booting Tilt Maze...\n");
	build_maze();
	init_physics();

	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display)) {
		printk("[tilt_maze] Error: Display device not ready!\n");
		return 0;
	}
	display_blanking_off(display);

	printk("[tilt_maze] Creating maze GUI...\n");
	create_maze_gui();
	printk("[tilt_maze] Maze GUI created successfully.\n");

	gy521_start();

	const float accel_scale = 1600.0f;
	uint32_t banner_until = 0;
	uint32_t last = k_uptime_get_32();
	struct { float x, y; } tilt;

	while (1) {
		gy521_get_tilt(&tilt.x, &tilt.y);

		uint32_t now = k_uptime_get_32();
		if (banner_until && (int32_t)(now - banner_until) >= 0) {
			lv_obj_add_flag(banner_obj, LV_OBJ_FLAG_HIDDEN);
			banner_until = 0;
		}
		float dt = (now - last) / 1000.0f;
		last = now;
		if (dt > 0.05f) dt = 0.05f;
		if (dt <= 0.001f) dt = 0.001f;

		/* Update dynamic gravity vector from IMU tilt */
		cpSpaceSetGravity(physics_space, cpv(tilt.x * accel_scale, tilt.y * accel_scale));

		/* Substep Chipmunk2D physics simulation */
		cpSpaceStep(physics_space, dt / 2.0f);
		cpSpaceStep(physics_space, dt / 2.0f);

		cpVect pos = cpBodyGetPosition(ball_body);

		/* Hole / Pit collision check */
		for (int i = 0; i < N_PITS; i++) {
			float dx = (float)pos.x - pits[i].x;
			float dy = (float)pos.y - pits[i].y;
			if (sqrtf(dx * dx + dy * dy) < pits[i].r) {
				printk("Fell in a hole - resetting\n");
				reset_ball();
				pos = cpBodyGetPosition(ball_body);
				lv_label_set_text(banner_obj, "You fell in a hole! Restarting...");
				lv_obj_align(banner_obj, LV_ALIGN_TOP_MID, 0, 10);
				lv_obj_remove_flag(banner_obj, LV_OBJ_FLAG_HIDDEN);
				banner_until = now + BANNER_MS;
				break;
			}
		}

		/* Goal check */
		{
			float dx = (float)pos.x - goal.x;
			float dy = (float)pos.y - goal.y;
			if (sqrtf(dx * dx + dy * dy) < goal.r) {
				printk("Reached the goal!\n");
				reset_ball();
				pos = cpBodyGetPosition(ball_body);
				lv_label_set_text(banner_obj, "You made it out! Restarting...");
				lv_obj_align(banner_obj, LV_ALIGN_TOP_MID, 0, 10);
				lv_obj_remove_flag(banner_obj, LV_OBJ_FLAG_HIDDEN);
				banner_until = now + BANNER_MS;
			}
		}

		/* Update ball position on platform */
		lv_obj_set_pos(ball_obj,
			       (int32_t)(pos.x - BALL_RADIUS - PLATFORM_X),
			       (int32_t)(pos.y - BALL_RADIUS - PLATFORM_Y));

		lv_timer_handler();
		k_msleep(10);
	}
}
