/* SPDX-License-Identifier: Apache-2.0
 *
 * Flappy Bird mini-game for PIC64GX Curiosity Kit powered by Chipmunk2D Physics.
 *
 * Controls:
 *   SW1 (sw0 alias, gpio2 pin 28) — exit to HUD demo (handled by demo_manager)
 *   SW2 (sw2 node,  gpio2 pin 6 ) — flap / start / restart
 *
 * Physics engine:
 *   Chipmunk2D rigid-body simulation running in 33 ms steps on the UI thread.
 *   - Player: Dynamic rigid body with mass, moment of inertia, elasticity, and friction.
 *   - Pipes: Kinematic rigid bodies advancing at constant horizontal velocity.
 *   - Ground / Ceiling: Static line segment shapes with restitution and friction.
 *   - Collisions: Detected and resolved via Chipmunk2D collision handler callbacks.
 */

#include "screen_game.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <lvgl.h>
#include <math.h>

#include "microchip_logo_img.h"
#include "chipmunk/chipmunk.h"

/* ── Collision Types ──────────────────────────────────────────────────────── */
enum collision_types {
	COLLISION_TYPE_NONE = 0,
	COLLISION_TYPE_PLAYER,
	COLLISION_TYPE_PIPE,
	COLLISION_TYPE_GROUND,
	COLLISION_TYPE_CEILING,
};

/* ── GPIO buttons ─────────────────────────────────────────────────────────── */
static const struct gpio_dt_spec btn_flap =
	GPIO_DT_SPEC_GET(DT_NODELABEL(sw2), gpios);
static const struct gpio_dt_spec btn_enter =
	GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

/* ── Colours ──────────────────────────────────────────────────────────────── */
#define C_SKY      0x0D1117u
#define C_GROUND   0x3D2B1Fu
#define C_GROUND_H 0x6B4423u
#define C_PIPE     0x3FB950u
#define C_PIPE_CAP 0x2D8A3Au
#define C_MC_RED   0xC0392Bu
#define C_WHITE    0xE6EDF3u
#define C_YELLOW   0xD29922u
#define C_SUBTLE   0x8B949Eu
#define C_OVERLAY  0x161B22u

/* ── Physics constants ────────────────────────────────────────────────────── */
#define GRAVITY_Y    980.0f
#define FLAP_VEL    -360.0f
#define SCROLL_SPD   130.0f  /* px / sec */
#define SPEED_STEP    10.0f  /* added per passed pipe */
#define SPEED_MAX    320.0f  /* speed cap */

/* ── Layout constants ─────────────────────────────────────────────────────── */
#define SCR_W        1280
#define SCR_H         720
#define PLAYER_X      180
#define PLAYER_SZ      44
#define PIPE_W         80
#define PIPE_CAP_H     18
#define GAP_H         280   /* gap height */
#define PIPE_SPACING  430
#define N_PIPES         3
#define GROUND_Y      672
#define GROUND_H       48
#define GAP_MIN       (GAP_H / 2 + 50)
#define GAP_MAX       (GROUND_Y - GAP_H / 2 - 50)
#define N_STARS        20

/* ── Game state ───────────────────────────────────────────────────────────── */
typedef enum { GAME_WAIT, GAME_PLAY, GAME_OVER } game_state_t;

static game_state_t state;
static int          score;
static int          best_score;
static float        scroll_spd;       /* current scroll speed in px/sec */
static int          gap_cy[N_PIPES];  /* centre Y of pipe gap */
static bool         scored[N_PIPES];  /* has this pipe been scored */
static int64_t      over_time_ms;
static bool         game_done;

/* ── Chipmunk2D objects ───────────────────────────────────────────────────── */
static cpSpace *space;
static cpBody  *player_body;
static cpShape *player_shape;

static cpShape *ground_shape;
static cpShape *ceil_shape;

static cpBody  *pipe_body[N_PIPES];
static cpShape *pipe_top_shape[N_PIPES];
static cpShape *pipe_bot_shape[N_PIPES];

/* ── Button edge detection ────────────────────────────────────────────────── */
static int prev_flap_state;
static int prev_enter_state;
static bool btn_inited;

static void btn_init_once(void)
{
	if (btn_inited) {
		return;
	}
	if (gpio_is_ready_dt(&btn_flap)) {
		gpio_pin_configure_dt(&btn_flap, GPIO_INPUT);
	}
	if (gpio_is_ready_dt(&btn_enter)) {
		gpio_pin_configure_dt(&btn_enter, GPIO_INPUT);
	}
	prev_flap_state  = gpio_pin_get_dt(&btn_flap);
	prev_enter_state = gpio_pin_get_dt(&btn_enter);
	btn_inited = true;
}

static bool flap_edge(void)
{
	int cur = gpio_pin_get_dt(&btn_flap);
	bool edge = (cur == 1) && (prev_flap_state == 0);

	prev_flap_state = cur;
	return edge;
}

static bool enter_edge(void)
{
	int cur = gpio_pin_get_dt(&btn_enter);
	bool edge = (cur == 1) && (prev_enter_state == 0);

	prev_enter_state = cur;
	return edge;
}

/* ── LVGL widget handles ──────────────────────────────────────────────────── */
static lv_obj_t *player_obj;
static lv_obj_t *pipe_top_body[N_PIPES];
static lv_obj_t *pipe_top_cap[N_PIPES];
static lv_obj_t *pipe_bot_body[N_PIPES];
static lv_obj_t *pipe_bot_cap[N_PIPES];
static lv_obj_t *score_lbl;
static lv_obj_t *status_panel;
static lv_obj_t *status_lbl;
static lv_obj_t *sub_lbl;
static lv_timer_t *game_timer;

/* ── Forward declarations ─────────────────────────────────────────────────── */
static void enter_over(void);
static void enter_wait(void);
static void enter_play(void);

/* ── Random number (xorshift) ─────────────────────────────────────────────── */
static uint32_t rng_state = 0xDEADBEEFu;

static int rand_gap_cy(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return GAP_MIN + (int)(rng_state % (uint32_t)(GAP_MAX - GAP_MIN));
}

/* ── Chipmunk Collision Callbacks ─────────────────────────────────────────── */
static cpBool player_hit_obstacle_cb(cpArbiter *arb, cpSpace *sp, cpDataPointer data)
{
	ARG_UNUSED(sp);
	ARG_UNUSED(data);

	if (state == GAME_PLAY) {
		enter_over();
		/* Add dynamic bounce + angular kick */
		cpVect ppos = cpBodyGetPosition(player_body);
		cpBodyApplyImpulseAtWorldPoint(player_body, cpv(-60.0f, -80.0f),
					       cpv(ppos.x, ppos.y - 10.0f));
		cpBodySetAngularVelocity(player_body, 8.0f);
	}
	return cpTrue;
}

static cpBool player_hit_ground_cb(cpArbiter *arb, cpSpace *sp, cpDataPointer data)
{
	ARG_UNUSED(sp);
	ARG_UNUSED(data);

	if (state == GAME_PLAY) {
		enter_over();
	}
	return cpTrue;
}

/* ── Pipe helpers ─────────────────────────────────────────────────────────── */
static void pipe_update_shapes(int i, float x)
{
	gap_cy[i] = rand_gap_cy();
	scored[i] = false;

	int top_h = gap_cy[i] - GAP_H / 2;
	int bot_y = gap_cy[i] + GAP_H / 2;

	if (top_h < 0) {
		top_h = 0;
	}

	/* Reposition kinematic body */
	cpBodySetPosition(pipe_body[i], cpv(x + (cpFloat)(PIPE_W / 2), 0.0f));
	cpBodySetVelocity(pipe_body[i], cpv(-scroll_spd, 0.0f));

	/* Remove previous shapes */
	if (pipe_top_shape[i]) {
		cpSpaceRemoveShape(space, pipe_top_shape[i]);
		cpShapeFree(pipe_top_shape[i]);
		pipe_top_shape[i] = NULL;
	}
	if (pipe_bot_shape[i]) {
		cpSpaceRemoveShape(space, pipe_bot_shape[i]);
		cpShapeFree(pipe_bot_shape[i]);
		pipe_bot_shape[i] = NULL;
	}

	/* Top pipe shape */
	if (top_h > 0) {
		cpBB top_bb = cpBBNew(-(cpFloat)(PIPE_W / 2), 0.0f,
				      (cpFloat)(PIPE_W / 2), (cpFloat)top_h);
		pipe_top_shape[i] = cpSpaceAddShape(space, cpBoxShapeNew2(pipe_body[i], top_bb, 0.0f));
		cpShapeSetCollisionType(pipe_top_shape[i], COLLISION_TYPE_PIPE);
		cpShapeSetElasticity(pipe_top_shape[i], 0.3f);
		cpShapeSetFriction(pipe_top_shape[i], 0.6f);
	}

	/* Bottom pipe shape */
	if (bot_y < GROUND_Y) {
		cpBB bot_bb = cpBBNew(-(cpFloat)(PIPE_W / 2), (cpFloat)bot_y,
				      (cpFloat)(PIPE_W / 2), (cpFloat)GROUND_Y);
		pipe_bot_shape[i] = cpSpaceAddShape(space, cpBoxShapeNew2(pipe_body[i], bot_bb, 0.0f));
		cpShapeSetCollisionType(pipe_bot_shape[i], COLLISION_TYPE_PIPE);
		cpShapeSetElasticity(pipe_bot_shape[i], 0.3f);
		cpShapeSetFriction(pipe_bot_shape[i], 0.6f);
	}
}

static float pipes_rightmost_x(void)
{
	float mx = (float)cpBodyGetPosition(pipe_body[0]).x - (float)(PIPE_W / 2);

	for (int i = 1; i < N_PIPES; i++) {
		float px = (float)cpBodyGetPosition(pipe_body[i]).x - (float)(PIPE_W / 2);
		if (px > mx) {
			mx = px;
		}
	}
	return mx;
}

static void pipe_update_objs(int i)
{
	cpVect ppos = cpBodyGetPosition(pipe_body[i]);
	int ix = (int)(ppos.x - (cpFloat)(PIPE_W / 2));
	int top_h = gap_cy[i] - GAP_H / 2;
	int bot_y = gap_cy[i] + GAP_H / 2;
	int bot_h = GROUND_Y - bot_y;

	if (top_h < 0) {
		top_h = 0;
	}
	if (bot_h < 0) {
		bot_h = 0;
	}

	/* Top pipe body */
	lv_obj_set_pos(pipe_top_body[i], ix, 0);
	lv_obj_set_size(pipe_top_body[i], PIPE_W, (top_h > PIPE_CAP_H) ?
			top_h - PIPE_CAP_H : 0);

	/* Top pipe cap */
	lv_obj_set_pos(pipe_top_cap[i], ix - 5, top_h - PIPE_CAP_H);
	lv_obj_set_size(pipe_top_cap[i], PIPE_W + 10,
			(top_h > 0) ? PIPE_CAP_H : 0);

	/* Bottom pipe body */
	lv_obj_set_pos(pipe_bot_body[i], ix, bot_y + PIPE_CAP_H);
	lv_obj_set_size(pipe_bot_body[i], PIPE_W,
			(bot_h > PIPE_CAP_H) ? bot_h - PIPE_CAP_H : 0);

	/* Bottom pipe cap */
	lv_obj_set_pos(pipe_bot_cap[i], ix - 5, bot_y);
	lv_obj_set_size(pipe_bot_cap[i], PIPE_W + 10,
			(bot_h > 0) ? PIPE_CAP_H : 0);
}

/* ── Status overlay helpers ───────────────────────────────────────────────── */
static void status_show(const char *main_text, const char *sub_text)
{
	lv_obj_clear_flag(status_panel, LV_OBJ_FLAG_HIDDEN);
	lv_label_set_text(status_lbl, main_text);
	lv_label_set_text(sub_lbl, sub_text ? sub_text : "");
}

static void status_hide(void)
{
	lv_obj_add_flag(status_panel, LV_OBJ_FLAG_HIDDEN);
}

static void score_update(void)
{
	char buf[16];

	snprintk(buf, sizeof(buf), "%d", score);
	lv_label_set_text(score_lbl, buf);
}

/* ── Physics space initialization ─────────────────────────────────────────── */
static void physics_init(void)
{
	if (space) {
		return;
	}

	space = cpSpaceNew();
	cpSpaceSetGravity(space, cpv(0.0f, GRAVITY_Y));
	cpSpaceSetIterations(space, 10);
	cpSpaceSetDamping(space, 0.98f);

	/* Static borders: Ground and ceiling */
	cpBody *static_body = cpSpaceGetStaticBody(space);

	ground_shape = cpSpaceAddShape(space, cpSegmentShapeNew(
		static_body,
		cpv(-100.0f, (cpFloat)GROUND_Y),
		cpv((cpFloat)(SCR_W + 100), (cpFloat)GROUND_Y),
		0.0f));
	cpShapeSetCollisionType(ground_shape, COLLISION_TYPE_GROUND);
	cpShapeSetElasticity(ground_shape, 0.4f);
	cpShapeSetFriction(ground_shape, 0.8f);

	ceil_shape = cpSpaceAddShape(space, cpSegmentShapeNew(
		static_body,
		cpv(-100.0f, 0.0f),
		cpv((cpFloat)(SCR_W + 100), 0.0f),
		0.0f));
	cpShapeSetCollisionType(ceil_shape, COLLISION_TYPE_CEILING);
	cpShapeSetElasticity(ceil_shape, 0.2f);
	cpShapeSetFriction(ceil_shape, 0.2f);

	/* Player body and box shape */
	cpFloat mass = 1.0f;
	cpFloat moment = cpMomentForBox(mass, (cpFloat)PLAYER_SZ, (cpFloat)PLAYER_SZ);
	player_body = cpSpaceAddBody(space, cpBodyNew(mass, moment));
	cpBodySetPosition(player_body, cpv((cpFloat)PLAYER_X, 300.0f));

	player_shape = cpSpaceAddShape(space, cpBoxShapeNew(player_body,
		(cpFloat)(PLAYER_SZ - 6), (cpFloat)(PLAYER_SZ - 6), 3.0f));
	cpShapeSetCollisionType(player_shape, COLLISION_TYPE_PLAYER);
	cpShapeSetElasticity(player_shape, 0.45f);
	cpShapeSetFriction(player_shape, 0.6f);

	/* Kinematic pipe bodies */
	for (int i = 0; i < N_PIPES; i++) {
		pipe_body[i] = cpSpaceAddBody(space, cpBodyNewKinematic());
		pipe_top_shape[i] = NULL;
		pipe_bot_shape[i] = NULL;
	}

	/* Collision handlers */
	cpCollisionHandler *pipe_h = cpSpaceAddCollisionHandler(
		space, COLLISION_TYPE_PLAYER, COLLISION_TYPE_PIPE);
	pipe_h->beginFunc = player_hit_obstacle_cb;

	cpCollisionHandler *ground_h = cpSpaceAddCollisionHandler(
		space, COLLISION_TYPE_PLAYER, COLLISION_TYPE_GROUND);
	ground_h->beginFunc = player_hit_ground_cb;
}

static void physics_cleanup(void)
{
	if (!space) {
		return;
	}

	for (int i = 0; i < N_PIPES; i++) {
		if (pipe_top_shape[i]) {
			cpSpaceRemoveShape(space, pipe_top_shape[i]);
			cpShapeFree(pipe_top_shape[i]);
			pipe_top_shape[i] = NULL;
		}
		if (pipe_bot_shape[i]) {
			cpSpaceRemoveShape(space, pipe_bot_shape[i]);
			cpShapeFree(pipe_bot_shape[i]);
			pipe_bot_shape[i] = NULL;
		}
		if (pipe_body[i]) {
			cpSpaceRemoveBody(space, pipe_body[i]);
			cpBodyFree(pipe_body[i]);
			pipe_body[i] = NULL;
		}
	}

	if (player_shape) {
		cpSpaceRemoveShape(space, player_shape);
		cpShapeFree(player_shape);
		player_shape = NULL;
	}
	if (player_body) {
		cpSpaceRemoveBody(space, player_body);
		cpBodyFree(player_body);
		player_body = NULL;
	}
	if (ground_shape) {
		cpSpaceRemoveShape(space, ground_shape);
		cpShapeFree(ground_shape);
		ground_shape = NULL;
	}
	if (ceil_shape) {
		cpSpaceRemoveShape(space, ceil_shape);
		cpShapeFree(ceil_shape);
		ceil_shape = NULL;
	}

	cpSpaceFree(space);
	space = NULL;
}

/* ── Game state transitions ───────────────────────────────────────────────── */
static void game_reset(void)
{
	score = 0;
	scroll_spd = SCROLL_SPD;

	/* Reset player position and motion */
	cpBodySetPosition(player_body, cpv((cpFloat)PLAYER_X, 300.0f));
	cpBodySetVelocity(player_body, cpv(0.0f, 0.0f));
	cpBodySetAngle(player_body, 0.0f);
	cpBodySetAngularVelocity(player_body, 0.0f);

	/* Reset pipes */
	for (int i = 0; i < N_PIPES; i++) {
		pipe_update_shapes(i, (float)(SCR_W + i * PIPE_SPACING));
		pipe_update_objs(i);
	}

	score_update();
	lv_obj_set_pos(player_obj, PLAYER_X - PLAYER_SZ / 2, 300 - PLAYER_SZ / 2);
	lv_image_set_rotation(player_obj, 0);
}

static void enter_wait(void)
{
	state = GAME_WAIT;
	game_reset();
	status_show("CHIPMUNK2D FLAPPY",
		    "Press JUMP (SW2) to start");
	lv_obj_set_style_text_color(status_lbl, lv_color_hex(C_MC_RED),
				    LV_PART_MAIN);
}

static void enter_play(void)
{
	state = GAME_PLAY;
	status_hide();
	/* Initial flap impulse */
	cpBodySetVelocity(player_body, cpv(0.0f, FLAP_VEL));
	cpBodySetAngle(player_body, -0.35f);
}

static void enter_over(void)
{
	char buf[64];

	state = GAME_OVER;
	over_time_ms = k_uptime_get();

	if (score > best_score) {
		best_score = score;
	}

	/* Stop moving pipes */
	for (int i = 0; i < N_PIPES; i++) {
		cpBodySetVelocity(pipe_body[i], cpv(0.0f, 0.0f));
	}

	snprintk(buf, sizeof(buf), "GAME OVER    %d pts", score);
	if (score > 0 && score == best_score) {
		lv_label_set_text(sub_lbl, "New best! SW2 restarts");
		lv_obj_set_style_text_color(sub_lbl, lv_color_hex(C_YELLOW),
					    LV_PART_MAIN);
	} else {
		char sub[48];

		snprintk(sub, sizeof(sub), "Best: %d  |  SW2 restarts", best_score);
		lv_label_set_text(sub_lbl, sub);
		lv_obj_set_style_text_color(sub_lbl, lv_color_hex(C_SUBTLE),
					    LV_PART_MAIN);
	}
	lv_obj_clear_flag(status_panel, LV_OBJ_FLAG_HIDDEN);
	lv_label_set_text(status_lbl, buf);
	lv_obj_set_style_text_color(status_lbl, lv_color_hex(C_WHITE),
				    LV_PART_MAIN);
}

/* ── 33 ms physics tick (LVGL timer, runs on UI thread) ──────────────────── */
static void game_tick_cb(lv_timer_t *t)
{
	ARG_UNUSED(t);

	btn_init_once();

	bool flap  = flap_edge();
	bool enter = enter_edge();

	if (enter) {
		game_done = true;
		return;
	}

	const cpFloat dt = 0.033f;

	switch (state) {
	case GAME_WAIT: {
		if (flap) {
			enter_play();
			break;
		}
		/* Smooth sinusoidal hover while waiting */
		int64_t now = k_uptime_get();
		float hover_y = 300.0f + 16.0f * sinf((float)now * 0.005f);
		cpBodySetPosition(player_body, cpv((cpFloat)PLAYER_X, (cpFloat)hover_y));
		cpBodySetVelocity(player_body, cpv(0.0f, 0.0f));
		cpBodySetAngle(player_body, 0.0f);

		lv_obj_set_pos(player_obj, PLAYER_X - PLAYER_SZ / 2, (int)hover_y - PLAYER_SZ / 2);
		lv_image_set_rotation(player_obj, 0);
		break;
	}

	case GAME_PLAY: {
		if (flap) {
			/* Physics flap impulse */
			cpBodySetVelocity(player_body, cpv(0.0f, FLAP_VEL));
			cpBodySetAngle(player_body, -0.40f);
			cpBodySetAngularVelocity(player_body, 0.0f);
		}

		/* Step Chipmunk physics (2 sub-steps for high accuracy) */
		cpSpaceStep(space, dt / 2.0f);
		cpSpaceStep(space, dt / 2.0f);

		/* Aerodynamic tilt based on vertical velocity */
		cpVect vel = cpBodyGetVelocity(player_body);
		float target_angle = (float)vel.y * 0.0018f;
		target_angle = CLAMP(target_angle, -0.45f, 1.25f);
		cpBodySetAngle(player_body, (cpFloat)target_angle);

		/* Update player LVGL object */
		cpVect pos = cpBodyGetPosition(player_body);
		cpFloat angle = cpBodyGetAngle(player_body);
		lv_obj_set_pos(player_obj, (int)(pos.x - (cpFloat)(PLAYER_SZ / 2)),
			       (int)(pos.y - (cpFloat)(PLAYER_SZ / 2)));
		/* LVGL angle in 0.1 degree units (1800 = 180 deg) */
		lv_image_set_rotation(player_obj, (int32_t)(angle * 1800.0f / 3.14159265f));

		/* Update & recycle pipes */
		for (int i = 0; i < N_PIPES; i++) {
			cpVect ppos = cpBodyGetPosition(pipe_body[i]);

			/* Score when player passes pipe center */
			if (!scored[i] && ppos.x < (cpFloat)PLAYER_X) {
				scored[i] = true;
				score++;
				score_update();
				/* Speed up slightly */
				scroll_spd = CLAMP(scroll_spd + SPEED_STEP,
						   SCROLL_SPD, SPEED_MAX);
				for (int j = 0; j < N_PIPES; j++) {
					cpBodySetVelocity(pipe_body[j],
							  cpv(-scroll_spd, 0.0f));
				}
			}

			/* Respawn pipe when completely off-screen to the left */
			if (ppos.x + (cpFloat)(PIPE_W / 2) < 0.0f) {
				pipe_update_shapes(i, pipes_rightmost_x() + PIPE_SPACING);
			}

			pipe_update_objs(i);
		}
		break;
	}

	case GAME_OVER: {
		/* Step Chipmunk rigid-body physics so bird bounces and tumbles onto ground */
		cpSpaceStep(space, dt / 2.0f);
		cpSpaceStep(space, dt / 2.0f);

		cpVect pos = cpBodyGetPosition(player_body);
		cpFloat angle = cpBodyGetAngle(player_body);
		lv_obj_set_pos(player_obj, (int)(pos.x - (cpFloat)(PLAYER_SZ / 2)),
			       (int)(pos.y - (cpFloat)(PLAYER_SZ / 2)));
		lv_image_set_rotation(player_obj, (int32_t)(angle * 1800.0f / 3.14159265f));

		/* SW2 restarts game */
		if (flap) {
			enter_wait();
		}
		break;
	}
	}
}

/* ── Screen delete cleanup ────────────────────────────────────────────────── */
static void game_screen_delete_cb(lv_event_t *e)
{
	ARG_UNUSED(e);

	if (game_timer) {
		lv_timer_delete(game_timer);
		game_timer = NULL;
	}
	physics_cleanup();

	player_obj = NULL;
	score_lbl  = NULL;
	status_panel = NULL;
	status_lbl = NULL;
	sub_lbl    = NULL;
	for (int i = 0; i < N_PIPES; i++) {
		pipe_top_body[i] = NULL;
		pipe_top_cap[i]  = NULL;
		pipe_bot_body[i] = NULL;
		pipe_bot_cap[i]  = NULL;
	}
}

/* ── Screen construction ──────────────────────────────────────────────────── */
lv_obj_t *screen_game_create(void)
{
	game_done        = false;
	if (gpio_is_ready_dt(&btn_flap)) {
		gpio_pin_configure_dt(&btn_flap, GPIO_INPUT);
	}
	if (gpio_is_ready_dt(&btn_enter)) {
		gpio_pin_configure_dt(&btn_enter, GPIO_INPUT);
	}
	prev_flap_state  = gpio_pin_get_dt(&btn_flap);
	prev_enter_state = gpio_pin_get_dt(&btn_enter);
	btn_inited       = true;

	/* Initialize Chipmunk2D physics space */
	physics_init();

	lv_obj_t *scr = lv_obj_create(NULL);

	lv_obj_set_style_bg_color(scr, lv_color_hex(C_SKY), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

	/* ── Stars (decorative background dots) ─────────────────────────── */
	uint32_t star_rng = 0xCAFEBABEu;

	for (int i = 0; i < N_STARS; i++) {
		star_rng ^= star_rng << 13;
		star_rng ^= star_rng >> 17;
		star_rng ^= star_rng << 5;
		int sx = (int)(star_rng % (uint32_t)SCR_W);

		star_rng ^= star_rng << 13;
		star_rng ^= star_rng >> 17;
		star_rng ^= star_rng << 5;
		int sy = (int)(star_rng % (uint32_t)(GROUND_Y - 20));

		lv_obj_t *star = lv_obj_create(scr);
		int sz = (i % 3 == 0) ? 4 : 3;

		lv_obj_set_pos(star, sx, sy);
		lv_obj_set_size(star, sz, sz);
		lv_obj_set_style_bg_color(star, lv_color_white(), LV_PART_MAIN);
		lv_obj_set_style_bg_opa(star, LV_OPA_60, LV_PART_MAIN);
		lv_obj_set_style_border_width(star, 0, LV_PART_MAIN);
		lv_obj_set_style_radius(star, LV_RADIUS_CIRCLE, LV_PART_MAIN);
	}

	/* ── Ground ──────────────────────────────────────────────────────── */
	lv_obj_t *ground = lv_obj_create(scr);

	lv_obj_set_pos(ground, 0, GROUND_Y);
	lv_obj_set_size(ground, SCR_W, GROUND_H);
	lv_obj_set_style_bg_color(ground, lv_color_hex(C_GROUND), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(ground, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_border_width(ground, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(ground, 0, LV_PART_MAIN);

	/* Ground highlight strip */
	lv_obj_t *gh = lv_obj_create(scr);

	lv_obj_set_pos(gh, 0, GROUND_Y);
	lv_obj_set_size(gh, SCR_W, 4);
	lv_obj_set_style_bg_color(gh, lv_color_hex(C_GROUND_H), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(gh, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_border_width(gh, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(gh, 0, LV_PART_MAIN);

	/* ── Pipes ───────────────────────────────────────────────────────── */
	for (int i = 0; i < N_PIPES; i++) {
		pipe_top_body[i] = lv_obj_create(scr);
		lv_obj_set_style_bg_color(pipe_top_body[i], lv_color_hex(C_PIPE),
					   LV_PART_MAIN);
		lv_obj_set_style_bg_opa(pipe_top_body[i], LV_OPA_COVER, LV_PART_MAIN);
		lv_obj_set_style_border_width(pipe_top_body[i], 0, LV_PART_MAIN);
		lv_obj_set_style_radius(pipe_top_body[i], 0, LV_PART_MAIN);

		pipe_top_cap[i] = lv_obj_create(scr);
		lv_obj_set_style_bg_color(pipe_top_cap[i], lv_color_hex(C_PIPE_CAP),
					   LV_PART_MAIN);
		lv_obj_set_style_bg_opa(pipe_top_cap[i], LV_OPA_COVER, LV_PART_MAIN);
		lv_obj_set_style_border_width(pipe_top_cap[i], 0, LV_PART_MAIN);
		lv_obj_set_style_radius(pipe_top_cap[i], 3, LV_PART_MAIN);

		pipe_bot_body[i] = lv_obj_create(scr);
		lv_obj_set_style_bg_color(pipe_bot_body[i], lv_color_hex(C_PIPE),
					   LV_PART_MAIN);
		lv_obj_set_style_bg_opa(pipe_bot_body[i], LV_OPA_COVER, LV_PART_MAIN);
		lv_obj_set_style_border_width(pipe_bot_body[i], 0, LV_PART_MAIN);
		lv_obj_set_style_radius(pipe_bot_body[i], 0, LV_PART_MAIN);

		pipe_bot_cap[i] = lv_obj_create(scr);
		lv_obj_set_style_bg_color(pipe_bot_cap[i], lv_color_hex(C_PIPE_CAP),
					   LV_PART_MAIN);
		lv_obj_set_style_bg_opa(pipe_bot_cap[i], LV_OPA_COVER, LV_PART_MAIN);
		lv_obj_set_style_border_width(pipe_bot_cap[i], 0, LV_PART_MAIN);
		lv_obj_set_style_radius(pipe_bot_cap[i], 3, LV_PART_MAIN);
	}

	/* ── Player Sprite ───────────────────────────────────────────────── */
	player_obj = lv_image_create(scr);
	lv_image_set_src(player_obj, &microchip_logo_img);
	lv_obj_set_style_bg_opa(player_obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(player_obj, 0, 0);
	lv_obj_set_style_pad_all(player_obj, 0, 0);
	lv_image_set_pivot(player_obj, PLAYER_SZ / 2, PLAYER_SZ / 2);

	/* ── Score label ─────────────────────────────────────────────────── */
	score_lbl = lv_label_create(scr);
	lv_label_set_text(score_lbl, "0");
	lv_obj_set_style_text_font(score_lbl, &lv_font_montserrat_48, LV_PART_MAIN);
	lv_obj_set_style_text_color(score_lbl, lv_color_white(), LV_PART_MAIN);
	lv_obj_align(score_lbl, LV_ALIGN_TOP_MID, 0, 12);

	/* ── Status overlay ──────────────────────────────────────────────── */
	status_panel = lv_obj_create(scr);
	lv_obj_set_size(status_panel, 740, 150);
	lv_obj_align(status_panel, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_color(status_panel, lv_color_hex(C_OVERLAY), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(status_panel, LV_OPA_80, LV_PART_MAIN);
	lv_obj_set_style_border_color(status_panel, lv_color_hex(C_MC_RED),
				       LV_PART_MAIN);
	lv_obj_set_style_border_width(status_panel, 2, LV_PART_MAIN);
	lv_obj_set_style_radius(status_panel, 10, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(status_panel, LV_SCROLLBAR_MODE_OFF);

	status_lbl = lv_label_create(status_panel);
	lv_label_set_text(status_lbl, "");
	lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_32, LV_PART_MAIN);
	lv_obj_set_style_text_color(status_lbl, lv_color_white(), LV_PART_MAIN);
	lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, -22);

	sub_lbl = lv_label_create(status_panel);
	lv_label_set_text(sub_lbl, "");
	lv_obj_set_style_text_font(sub_lbl, &lv_font_montserrat_20, LV_PART_MAIN);
	lv_obj_set_style_text_color(sub_lbl, lv_color_hex(C_SUBTLE), LV_PART_MAIN);
	lv_obj_align(sub_lbl, LV_ALIGN_CENTER, 0, 20);

	/* ── Enter WAIT state ────────────────────────────────────────────── */
	enter_wait();

	/* ── Game timer: 33 ms physics loop ─────────────────────────────── */
	game_timer = lv_timer_create(game_tick_cb, 33, NULL);

	/* ── Cleanup hook ────────────────────────────────────────────────── */
	lv_obj_add_event_cb(scr, game_screen_delete_cb, LV_EVENT_DELETE, NULL);

	return scr;
}

void screen_game_update(void)
{
	/* Physics is driven by game_timer */
}

bool screen_game_is_done(void)
{
	if (game_done) {
		game_done = false;
		return true;
	}
	return false;
}
