/* SPDX-License-Identifier: Apache-2.0
 *
 * Flappy Bird mini-game for PIC64GX Curiosity Kit.
 *
 * Controls
 *   SW1 (sw0 alias, gpio2 pin 28) — enter game / return to demo (handled by demo_manager)
 *   SW2 (sw2 node,  gpio2 pin 6 ) — flap / start
 *   Flick or twist the board (GY-521 IMU, see gy521.c) — same as SW2
 *
 * Player sprite: 44×44 Microchip red rounded rectangle with bold white "M" label.
 * Physics driven by a 33 ms lv_timer_t (runs within lv_timer_handler on the UI thread).
 * Timer is deleted via LV_EVENT_DELETE on the screen object.
 *
 * State machine: WAIT → PLAY → OVER → (done flag → demo_manager returns to HUD)
 */

#include "screen_game.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <lvgl.h>
#include "microchip_logo_img.h"
#include "gy521.h"

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
#define GRAVITY     0.50f
#define FLAP_VEL  -11.00f
#define VEL_MAX    14.00f
#define SCROLL_SPD  4.00f   /* initial scroll speed */
#define SPEED_STEP  0.35f   /* added per passed pipe */
#define SPEED_MAX  12.00f   /* speed cap */

/* ── Layout constants ─────────────────────────────────────────────────────── */
#define SCR_W        1280
#define SCR_H         720
#define PLAYER_X      160
#define PLAYER_SZ      44
#define PIPE_W         80
#define PIPE_CAP_H     18
#define GAP_H         280   /* larger gap = easier for kids */
#define PIPE_SPACING  430
#define N_PIPES         3
#define GROUND_Y      672
#define GROUND_H       48
#define GAP_MIN       (GAP_H / 2 + 50)
#define GAP_MAX       (GROUND_Y - GAP_H / 2 - 50)
#define N_STARS        20

/* A flick right after a crash is usually the player still moving, not asking
 * for a restart, so flicks are ignored for this long after GAME_OVER. */
#define OVER_FLICK_HOLDOFF_MS 1000

/* ── Game state ───────────────────────────────────────────────────────────── */
typedef enum { GAME_WAIT, GAME_PLAY, GAME_OVER } game_state_t;

static game_state_t state;
static float        py;         /* player Y (top of sprite) */
static float        vy;         /* player vertical velocity */
static float        pipe_x[N_PIPES];
static int          gap_cy[N_PIPES]; /* centre Y of pipe gap */
static int          score;
static int          best_score;
static float        scroll_spd;   /* current scroll speed, ramps up per pipe */
static bool         scored[N_PIPES]; /* has this pipe been passed */
static int64_t      over_time_ms;    /* when GAME_OVER was entered */
static bool         game_done;       /* signals demo_manager to return */

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
	/* Seed previous states to current (avoid spurious edge on first call) */
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

/* ── Random number (xorshift) ─────────────────────────────────────────────── */
static uint32_t rng_state = 0xDEADBEEFu;

static int rand_gap_cy(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return GAP_MIN + (int)(rng_state % (uint32_t)(GAP_MAX - GAP_MIN));
}

/* ── Pipe helpers ─────────────────────────────────────────────────────────── */
static void pipe_reset(int i, float x)
{
	pipe_x[i]   = x;
	gap_cy[i]   = rand_gap_cy();
	scored[i]   = false;
}

/* Find the pipe furthest to the right */
static float pipes_rightmost_x(void)
{
	float mx = pipe_x[0];

	for (int i = 1; i < N_PIPES; i++) {
		if (pipe_x[i] > mx) {
			mx = pipe_x[i];
		}
	}
	return mx;
}

static void pipe_update_objs(int i)
{
	int ix = (int)pipe_x[i];
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

	/* Top pipe cap (wider, at the bottom of the top pipe) */
	lv_obj_set_pos(pipe_top_cap[i], ix - 5, top_h - PIPE_CAP_H);
	lv_obj_set_size(pipe_top_cap[i], PIPE_W + 10,
			(top_h > 0) ? PIPE_CAP_H : 0);

	/* Bottom pipe body */
	lv_obj_set_pos(pipe_bot_body[i], ix, bot_y + PIPE_CAP_H);
	lv_obj_set_size(pipe_bot_body[i], PIPE_W,
			(bot_h > PIPE_CAP_H) ? bot_h - PIPE_CAP_H : 0);

	/* Bottom pipe cap (wider, at the top of the bottom pipe) */
	lv_obj_set_pos(pipe_bot_cap[i], ix - 5, bot_y);
	lv_obj_set_size(pipe_bot_cap[i], PIPE_W + 10,
			(bot_h > 0) ? PIPE_CAP_H : 0);
}

/* ── Collision (AABB) ─────────────────────────────────────────────────────── */
static bool rects_overlap(int ax1, int ay1, int ax2, int ay2,
			   int bx1, int by1, int bx2, int by2)
{
	return ax1 < bx2 && ax2 > bx1 && ay1 < by2 && ay2 > by1;
}

static bool check_collision(void)
{
	int px1 = PLAYER_X + 4;  /* shrink hitbox slightly for fairness */
	int py1 = (int)py + 4;
	int px2 = PLAYER_X + PLAYER_SZ - 4;
	int py2 = (int)py + PLAYER_SZ - 4;

	/* Ground / ceiling */
	if (py2 >= GROUND_Y || py1 <= 0) {
		return true;
	}

	for (int i = 0; i < N_PIPES; i++) {
		int ix = (int)pipe_x[i];
		int top_h = gap_cy[i] - GAP_H / 2;
		int bot_y = gap_cy[i] + GAP_H / 2;

		/* Top pipe */
		if (rects_overlap(px1, py1, px2, py2,
				  ix, 0, ix + PIPE_W, top_h)) {
			return true;
		}
		/* Bottom pipe */
		if (rects_overlap(px1, py1, px2, py2,
				  ix, bot_y, ix + PIPE_W, GROUND_Y)) {
			return true;
		}
	}
	return false;
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

/* ── Game state transitions ───────────────────────────────────────────────── */
static void game_reset(void)
{
	py    = 300.0f;
	vy    = 0.0f;
	score = 0;
	scroll_spd = SCROLL_SPD;

	for (int i = 0; i < N_PIPES; i++) {
		pipe_reset(i, (float)(SCR_W + i * PIPE_SPACING));
		pipe_update_objs(i);
	}

	score_update();
	lv_obj_set_pos(player_obj, PLAYER_X, (int)py);
}

static void enter_wait(void)
{
	state = GAME_WAIT;
	game_reset();
	status_show("FLAPPY MICROCHIP",
		    "Flick the board or press SW2 to start");
	lv_obj_set_style_text_color(status_lbl, lv_color_hex(C_MC_RED),
				    LV_PART_MAIN);
}

static void enter_play(void)
{
	state = GAME_PLAY;
	status_hide();
}

static void enter_over(void)
{
	char buf[64];

	state = GAME_OVER;
	over_time_ms = k_uptime_get();

	if (score > best_score) {
		best_score = score;
	}

	snprintk(buf, sizeof(buf), "GAME OVER    %d pts", score);
	if (score > 0 && score == best_score) {
		lv_label_set_text(sub_lbl, "New best!");
		lv_obj_set_style_text_color(sub_lbl, lv_color_hex(C_YELLOW),
					    LV_PART_MAIN);
	} else {
		char sub[32];

		snprintk(sub, sizeof(sub), "Best: %d   flick to retry", best_score);
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

	/* Always consume the flick so a stale one never fires later. */
	bool flick = gy521_take_flap();
	bool flap  = flap_edge();
	bool enter = enter_edge();

	if (state == GAME_OVER &&
	    k_uptime_get() - over_time_ms < OVER_FLICK_HOLDOFF_MS) {
		flick = false;
	}
	flap = flap || flick;

	switch (state) {
	case GAME_WAIT:
		if (flap) {
			enter_play();
		}
		/* Gentle hover animation while waiting */
		vy += 0.1f;
		py += vy;
		if (py > 320.0f || py < 280.0f) {
			vy = -vy * 0.8f;
			py = CLAMP(py, 280.0f, 320.0f);
		}
		lv_obj_set_pos(player_obj, PLAYER_X, (int)py);
		break;

	case GAME_PLAY:
		/* Physics */
		vy += GRAVITY;
		if (vy > VEL_MAX) {
			vy = VEL_MAX;
		}
		if (flap) {
			vy = FLAP_VEL;
		}
		py += vy;

		lv_obj_set_pos(player_obj, PLAYER_X, (int)py);

		/* Scroll pipes */
		for (int i = 0; i < N_PIPES; i++) {
			pipe_x[i] -= scroll_spd;

			/* Score: player passed pipe centre */
			int centre = (int)pipe_x[i] + PIPE_W / 2;

			if (!scored[i] && centre < PLAYER_X) {
				scored[i] = true;
				score++;
				score_update();
				/* Speed up a little after each pipe, up to the cap */
				scroll_spd += SPEED_STEP;
				if (scroll_spd > SPEED_MAX) {
					scroll_spd = SPEED_MAX;
				}
			}

			/* Respawn pipe that has exited left */
			if (pipe_x[i] + PIPE_W < 0) {
				pipe_reset(i, pipes_rightmost_x() + PIPE_SPACING);
			}

			pipe_update_objs(i);
		}

		/* Collision */
		if (check_collision()) {
			enter_over();
		}
		break;

	case GAME_OVER:
		/* SW2 (flap) restarts the game IN PLACE — same screen, same
		 * timer. No screen reload, so no timer-clobbering race. */
		if (flap) {
			enter_wait();
		}
		break;
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
	/* Null all widget handles so stale pointers are never dereferenced */
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

	lv_obj_t *scr = lv_obj_create(NULL);

	lv_obj_set_style_bg_color(scr, lv_color_hex(C_SKY), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

	/* ── Stars (static decorative dots) ─────────────────────────────── */
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

	/* ── Pipes (pre-created, positioned in game_reset) ───────────────── */
	for (int i = 0; i < N_PIPES; i++) {
		/* Top body */
		pipe_top_body[i] = lv_obj_create(scr);
		lv_obj_set_style_bg_color(pipe_top_body[i], lv_color_hex(C_PIPE),
					   LV_PART_MAIN);
		lv_obj_set_style_bg_opa(pipe_top_body[i], LV_OPA_COVER, LV_PART_MAIN);
		lv_obj_set_style_border_width(pipe_top_body[i], 0, LV_PART_MAIN);
		lv_obj_set_style_radius(pipe_top_body[i], 0, LV_PART_MAIN);

		/* Top cap */
		pipe_top_cap[i] = lv_obj_create(scr);
		lv_obj_set_style_bg_color(pipe_top_cap[i], lv_color_hex(C_PIPE_CAP),
					   LV_PART_MAIN);
		lv_obj_set_style_bg_opa(pipe_top_cap[i], LV_OPA_COVER, LV_PART_MAIN);
		lv_obj_set_style_border_width(pipe_top_cap[i], 0, LV_PART_MAIN);
		lv_obj_set_style_radius(pipe_top_cap[i], 3, LV_PART_MAIN);

		/* Bottom body */
		pipe_bot_body[i] = lv_obj_create(scr);
		lv_obj_set_style_bg_color(pipe_bot_body[i], lv_color_hex(C_PIPE),
					   LV_PART_MAIN);
		lv_obj_set_style_bg_opa(pipe_bot_body[i], LV_OPA_COVER, LV_PART_MAIN);
		lv_obj_set_style_border_width(pipe_bot_body[i], 0, LV_PART_MAIN);
		lv_obj_set_style_radius(pipe_bot_body[i], 0, LV_PART_MAIN);

		/* Bottom cap */
		pipe_bot_cap[i] = lv_obj_create(scr);
		lv_obj_set_style_bg_color(pipe_bot_cap[i], lv_color_hex(C_PIPE_CAP),
					   LV_PART_MAIN);
		lv_obj_set_style_bg_opa(pipe_bot_cap[i], LV_OPA_COVER, LV_PART_MAIN);
		lv_obj_set_style_border_width(pipe_bot_cap[i], 0, LV_PART_MAIN);
		lv_obj_set_style_radius(pipe_bot_cap[i], 3, LV_PART_MAIN);
	}

	/* ── Player: Microchip logo (red rounded rect + white M) ─────────── */
	/* ── Player: Microchip logo (RGB565, rounded corners) ───────────── */
	player_obj = lv_image_create(scr);
	lv_image_set_src(player_obj, &microchip_logo_img);
	lv_obj_set_style_bg_opa(player_obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(player_obj, 0, 0);
	lv_obj_set_style_pad_all(player_obj, 0, 0);

	/* ── Score label ─────────────────────────────────────────────────── */
	score_lbl = lv_label_create(scr);
	lv_label_set_text(score_lbl, "0");
	lv_obj_set_style_text_font(score_lbl, &lv_font_montserrat_48, LV_PART_MAIN);
	lv_obj_set_style_text_color(score_lbl, lv_color_white(), LV_PART_MAIN);
	lv_obj_align(score_lbl, LV_ALIGN_TOP_MID, 0, 12);

	/* ── Status overlay (hidden during play) ─────────────────────────── */
	status_panel = lv_obj_create(scr);
	lv_obj_set_size(status_panel, 700, 150);
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

	/* ── Initialise game to WAIT state ───────────────────────────────── */
	enter_wait();

	/* ── Game timer: 33 ms physics loop ─────────────────────────────── */
	game_timer = lv_timer_create(game_tick_cb, 33, NULL);

	/* ── Cleanup hook: delete timer when screen is freed ─────────────── */
	lv_obj_add_event_cb(scr, game_screen_delete_cb, LV_EVENT_DELETE, NULL);

	return scr;
}

void screen_game_update(void)
{
	/* Physics is driven by game_timer; nothing to do at 1 Hz. */
}

bool screen_game_is_done(void)
{
	if (game_done) {
		game_done = false;
		return true;
	}
	return false;
}
