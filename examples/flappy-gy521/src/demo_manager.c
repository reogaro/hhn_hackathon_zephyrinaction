/* SPDX-License-Identifier: Apache-2.0
 *
 * Demo manager — orchestrates 4-screen cycling demo on PIC64GX.
 *
 * Runs on core 2 (K_FOREVER + k_thread_start pattern).
 * Screens cycle every CYCLE_MS ms with a 400 ms fade transition.
 * A persistent overlay on lv_layer_top() shows render/output FPS
 * and screen indicator dots.
 *
 * SW1 (sw0 alias) — at any time: enter Flappy Bird game mode.
 * Game screen self-manages; demo_manager polls screen_game_is_done()
 * to know when to return to the normal cycle.
 */

#include "demo_manager.h"
#include "screen_hud.h"
#include "screen_dashboard.h"
#include "screen_charts.h"
#include "screen_widgets.h"
#include "screen_game.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include "vdma_disp.h"

/* ── Colour palette (shared) ──────────────────────────────────────────────── */
#define C_BG     0x0D1117u
#define C_GREEN  0x3FB950u
#define C_SUBTLE 0x8B949Eu
#define C_WHITE  0xE6EDF3u
#define C_ACCENT 0x58A6FFu
#define C_MC_RED 0xC0392Bu

/* ── Timing ───────────────────────────────────────────────────────────────── */
#define CYCLE_MS   5000   /* screen hold time in ms */
#define RENDER_MS    33   /* lv_timer_handler period (~30 fps) */
#define FADE_MS     400   /* transition fade duration */

/* ── Screen table ─────────────────────────────────────────────────────────── */
static const demo_screen_t screens[] = {
	{ "STATS HUD",      screen_hud_create,       screen_hud_update       },
	{ "DASHBOARD",      screen_dashboard_create, screen_dashboard_update  },
	{ "LIVE TELEMETRY", screen_charts_create,    screen_charts_update     },
	{ "WIDGET GALLERY", screen_widgets_create,   screen_widgets_update    },
};
#define N_SCREENS ((int)ARRAY_SIZE(screens))

static int      cur_screen = -1;   /* -1 = not yet initialised */
static lv_obj_t *active_scr;
static bool     in_game_mode;

/* ── Game mode entry button (SW1 = sw0 alias) ─────────────────────────────── */
static const struct gpio_dt_spec btn_game =
	GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static int prev_game_btn;
static bool game_btn_inited;

static void game_btn_init(void)
{
	if (gpio_is_ready_dt(&btn_game)) {
		gpio_pin_configure_dt(&btn_game, GPIO_INPUT);
		prev_game_btn = gpio_pin_get_dt(&btn_game);
	}
	game_btn_inited = true;
}

/* Returns true on rising edge of SW1 (pressed = pin goes high due to ACTIVE_LOW flag) */
static bool game_btn_edge(void)
{
	if (!game_btn_inited) {
		return false;
	}
	int cur = gpio_pin_get_dt(&btn_game);
	bool edge = (cur == 1) && (prev_game_btn == 0);

	prev_game_btn = cur;
	return edge;
}

/* ── FPS state ────────────────────────────────────────────────────────────── */
static uint32_t g_render_fps;
static uint32_t g_output_fps;

uint32_t demo_get_render_fps(void) { return g_render_fps; }
uint32_t demo_get_output_fps(void) { return g_output_fps; }
int      demo_get_screen_idx(void) { return cur_screen; }

/* ── Persistent overlay (lv_layer_top()) ─────────────────────────────────── */
static lv_obj_t *fps_badge;
static lv_obj_t *dot_bar;
static lv_obj_t *game_hint;

static void overlay_create(void)
{
	lv_obj_t *top = lv_layer_top();

	/* Top-right FPS badge */
	fps_badge = lv_label_create(top);
	lv_label_set_text(fps_badge, "R:-- D:--fps");
	lv_obj_set_style_text_font(fps_badge, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_style_text_color(fps_badge, lv_color_hex(C_GREEN), LV_PART_MAIN);
	lv_obj_align(fps_badge, LV_ALIGN_TOP_RIGHT, -12, 6);

	/* Bottom-center screen indicator dots */
	dot_bar = lv_label_create(top);
	lv_label_set_text(dot_bar, "\xe2\x97\x8f  \xe2\x97\x8b  \xe2\x97\x8b  \xe2\x97\x8b");
	lv_obj_set_style_text_font(dot_bar, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_style_text_color(dot_bar, lv_color_hex(C_WHITE), LV_PART_MAIN);
	lv_obj_align(dot_bar, LV_ALIGN_BOTTOM_MID, 0, -6);

	/* Top-left game hint */
	game_hint = lv_label_create(top);
	lv_label_set_text(game_hint, "SW1: GAME");
	lv_obj_set_style_text_font(game_hint, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_style_text_color(game_hint, lv_color_hex(C_MC_RED), LV_PART_MAIN);
	lv_obj_align(game_hint, LV_ALIGN_TOP_LEFT, 12, 6);
}

static void overlay_update(uint32_t render_fps, uint32_t output_fps, int idx)
{
	char buf[32];

	snprintk(buf, sizeof(buf), "R:%u D:%ufps", render_fps, output_fps);
	lv_label_set_text(fps_badge, buf);

	if (in_game_mode) {
		lv_label_set_text(dot_bar, "\xe2\x96\xb6 GAME");
		lv_obj_set_style_text_color(dot_bar, lv_color_hex(C_MC_RED),
					    LV_PART_MAIN);
		lv_label_set_text(game_hint, "SW1: EXIT");
		return;
	}

	lv_obj_set_style_text_color(dot_bar, lv_color_hex(C_WHITE), LV_PART_MAIN);
	lv_label_set_text(game_hint, "SW1: GAME");

	/* Build dot string: filled circle for current, empty for others */
	static const char *filled = "\xe2\x97\x8f";
	static const char *empty  = "\xe2\x97\x8b";
	char dots[64];
	int  pos = 0;

	for (int i = 0; i < N_SCREENS; i++) {
		const char *sym = (i == idx) ? filled : empty;

		while (*sym) {
			dots[pos++] = *sym++;
		}
		if (i < N_SCREENS - 1) {
			dots[pos++] = ' ';
			dots[pos++] = ' ';
		}
	}
	dots[pos] = '\0';
	lv_label_set_text(dot_bar, dots);
}

/* ── Screen switching ─────────────────────────────────────────────────────── */
static void load_screen(int idx)
{
	lv_obj_t *new_scr = screens[idx].create();

	if (cur_screen < 0) {
		/* First load — no animation, no old screen to delete */
		lv_screen_load(new_scr);
	} else {
		lv_screen_load_anim(new_scr, LV_SCR_LOAD_ANIM_FADE_ON,
				    FADE_MS, 0, true);
	}
	active_scr = new_scr;
	cur_screen = idx;
	printk("[demo] screen %d: %s\n", idx, screens[idx].name);
}

static void load_game_screen(void)
{
	lv_obj_t *game_scr = screen_game_create();

	lv_screen_load_anim(game_scr, LV_SCR_LOAD_ANIM_FADE_ON, FADE_MS, 0, true);
	active_scr = game_scr;
	printk("[demo] game mode\n");
}

/* ── Demo worker threads (make thread table interesting) ──────────────────── */
#define DW_STACK  512
#define DW_COUNT  4

static K_THREAD_STACK_ARRAY_DEFINE(dw_stacks, DW_COUNT, DW_STACK);
static struct k_thread dw_threads[DW_COUNT];

static void demo_worker_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2); ARG_UNUSED(p3);
	uint32_t phase = (uint32_t)(uintptr_t)p1;
	uint32_t x = 0xACE1u + phase * 0x1234u;

	while (true) {
		for (int i = 0; i < 50000; i++) {
			x ^= x << 13;
			x ^= x >> 17;
			x ^= x << 5;
		}
		k_sleep(K_MSEC(100 + (phase * 73) % 200));
	}
}

static void demo_workers_start(void)
{
	static const char *names[] = { "demo_w0", "demo_w1", "demo_w2", "demo_w3" };

	for (int i = 0; i < DW_COUNT; i++) {
		k_tid_t tid = k_thread_create(&dw_threads[i],
					      dw_stacks[i], DW_STACK,
					      demo_worker_fn,
					      (void *)(uintptr_t)i,
					      NULL, NULL,
					      8 + i, 0, K_FOREVER);
#ifdef CONFIG_SCHED_CPU_MASK
		k_thread_cpu_mask_clear(tid);
		k_thread_cpu_mask_enable(tid, 1 + (i % 3));
#endif
		k_thread_name_set(tid, names[i]);
		k_thread_start(tid);
	}
}

/* ── Main demo thread ─────────────────────────────────────────────────────── */
#define DM_STACK  16384
#define DM_PRIO   6
#define DM_CORE   2

static K_THREAD_STACK_DEFINE(dm_stack, DM_STACK);
static struct k_thread dm_thread;

static void demo_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	printk("[demo] starting on core %d\n", arch_curr_cpu()->id);

	game_btn_init();

	/* Build persistent overlay first (before any screen) */
	overlay_create();

	/* Start directly in Flappy Bird */
	in_game_mode = true;
	load_game_screen();

	int64_t next_stat   = k_uptime_get() + 1000;
	int64_t next_switch = k_uptime_get() + CYCLE_MS;
	uint32_t render_ticks = 0;

	while (true) {
		int64_t now = k_uptime_get();

		/* ── SW1 edge: enter or exit game mode ─────────────────────── */
		if (game_btn_edge()) {
			if (!in_game_mode) {
				in_game_mode = true;
				load_game_screen();
				/* Reset cycle so demo resumes cleanly on exit */
				next_switch = now + CYCLE_MS;
			}
			/* If in_game_mode, screen_game handles SW1 internally
			 * (it returns to demo via screen_game_is_done() below). */
		}

		/* ── Poll game-done flag → return to HUD ───────────────────── */
		/* Game restarts itself in place (screen_game GAME_OVER handles SW2).
		 * We never leave game mode, so no reload logic is needed here. */

		/* ── 1 Hz: update screen data + refresh overlay ────────────── */
		if (now >= next_stat) {
			if (!in_game_mode) {
				screens[cur_screen].update();
			} else {
				screen_game_update();
			}

			g_render_fps = render_ticks;
			g_output_fps = vdma_disp_get_frame_count(
				DEVICE_DT_GET(DT_NODELABEL(vdma_disp)));
			overlay_update(g_render_fps, g_output_fps, cur_screen);

			render_ticks = 0;
			next_stat = now + 1000;
		}

		/* ── 5 s: advance to next screen (suppressed in game mode) ─── */
		if (!in_game_mode && now >= next_switch) {
			int next = (cur_screen + 1) % N_SCREENS;

			load_screen(next);
			next_switch = now + CYCLE_MS;
		}

		lv_timer_handler();
		render_ticks++;
		k_sleep(K_MSEC(RENDER_MS));
	}
}

void demo_manager_start(void)
{
	demo_workers_start();

	k_tid_t tid = k_thread_create(&dm_thread, dm_stack, DM_STACK,
				      demo_thread_fn, NULL, NULL, NULL,
				      DM_PRIO, 0, K_FOREVER);
#ifdef CONFIG_SCHED_CPU_MASK
	k_thread_cpu_mask_clear(tid);
	k_thread_cpu_mask_enable(tid, DM_CORE);
#endif
	k_thread_name_set(tid, "demo_mgr");
	k_thread_start(tid);

	printk("[demo] manager thread started (core %d)\n", DM_CORE);
}
