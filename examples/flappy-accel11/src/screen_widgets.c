/* SPDX-License-Identifier: Apache-2.0
 *
 * Screen 3: Widget Gallery
 *
 * 3×2 panel grid, all self-animating via lv_anim_t (infinite repeat).
 * No external data updates needed — update() is a no-op.
 *
 *  +-------------------+-------------------+
 *  | 3 Arc gauges      | 3 Sliders         |
 *  +-------------------+-------------------+
 *  | 6 LED indicators  | 4 Progress bars   |
 *  +-------------------+-------------------+
 *  | Roller wheel      | Switches/Checkbox |
 *  +-------------------+-------------------+
 */

#include "screen_widgets.h"

#include <zephyr/kernel.h>
#include <lvgl.h>

/* ── Colours ──────────────────────────────────────────────────────────────── */
#define C_BG     0x0D1117u
#define C_CARD   0x161B22u
#define C_BORDER 0x30363Du
#define C_ACCENT 0x58A6FFu
#define C_GREEN  0x3FB950u
#define C_YELLOW 0xD29922u
#define C_RED    0xF85149u
#define C_ORANGE 0xE06C2Bu
#define C_PURPLE 0xBC8CFFu
#define C_WHITE  0xE6EDF3u
#define C_SUBTLE 0x8B949Eu

/* ── Layout ───────────────────────────────────────────────────────────────── */
#define SCR_W   1280
#define SCR_H    720
#define HDR_H     50
#define BODY_Y   (HDR_H + 2)
#define BODY_H   (SCR_H - BODY_Y - 4)
#define NCOLS      3
#define NROWS      2
#define CELL_W   ((SCR_W - (NCOLS - 1) * 4) / NCOLS)       /* ~424 */
#define CELL_H   ((BODY_H - (NROWS - 1) * 4) / NROWS)      /* ~330 */
#define PAD        12

/* ── LED blink state (driven by lv_timer_t) ───────────────────────────────── */
static lv_obj_t  *leds[6];
static int        led_step;
static lv_timer_t *led_timer;
static lv_timer_t *roller_timer;

/*
 * Called when LVGL deletes the widgets screen (happens 400 ms after the fade
 * transition starts, because lv_screen_load_anim is called with delete_old=true).
 * We must stop both timers here — they hold raw pointers to child objects that
 * are about to be freed, and will fault on their next fire if not deleted.
 */
static void widgets_screen_delete_cb(lv_event_t *e)
{
	ARG_UNUSED(e);

	if (led_timer) {
		lv_timer_delete(led_timer);
		led_timer = NULL;
	}
	if (roller_timer) {
		lv_timer_delete(roller_timer);
		roller_timer = NULL;
	}
	for (int i = 0; i < 6; i++) {
		leds[i] = NULL;
	}
}

static void led_timer_cb(lv_timer_t *t)
{
	ARG_UNUSED(t);
	/* Ripple: light each LED in sequence */
	for (int i = 0; i < 6; i++) {
		if (leds[i]) {
			if (i == led_step) {
				lv_led_on(leds[i]);
			} else {
				lv_led_off(leds[i]);
			}
		}
	}
	led_step = (led_step + 1) % 6;
}

/* ── Build helpers ────────────────────────────────────────────────────────── */
static lv_obj_t *cell(lv_obj_t *parent, int col, int row, const char *title)
{
	int x = col * (CELL_W + 4);
	int y = BODY_Y + row * (CELL_H + 4);
	lv_obj_t *p = lv_obj_create(parent);

	lv_obj_set_pos(p, x, y);
	lv_obj_set_size(p, CELL_W, CELL_H);
	lv_obj_set_style_bg_color(p, lv_color_hex(C_CARD), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(p, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_border_color(p, lv_color_hex(C_BORDER), LV_PART_MAIN);
	lv_obj_set_style_border_width(p, 1, LV_PART_MAIN);
	lv_obj_set_style_radius(p, 6, LV_PART_MAIN);
	lv_obj_set_style_pad_all(p, PAD, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_OFF);

	lv_obj_t *t = lv_label_create(p);

	lv_label_set_text(t, title);
	lv_obj_set_style_text_font(t, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_style_text_color(t, lv_color_hex(C_ACCENT), LV_PART_MAIN);
	lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);
	return p;
}

static void arc_anim_exec(void *obj, int32_t val)
{
	lv_arc_set_value((lv_obj_t *)obj, val);
}

static void add_arc_anim(lv_obj_t *arc, int min, int max,
			  uint32_t period_ms, uint32_t delay_ms)
{
	lv_anim_t a;

	lv_anim_init(&a);
	lv_anim_set_var(&a, arc);
	lv_anim_set_exec_cb(&a, arc_anim_exec);
	lv_anim_set_values(&a, min, max);
	lv_anim_set_duration(&a, period_ms);
	lv_anim_set_playback_duration(&a, period_ms);
	lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
	lv_anim_set_delay(&a, delay_ms);
	lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
	lv_anim_start(&a);
}

static void bar_anim_exec(void *obj, int32_t val)
{
	lv_bar_set_value((lv_obj_t *)obj, val, LV_ANIM_OFF);
}

static void add_bar_anim(lv_obj_t *bar, int min, int max,
			  uint32_t period_ms, uint32_t delay_ms)
{
	lv_anim_t a;

	lv_anim_init(&a);
	lv_anim_set_var(&a, bar);
	lv_anim_set_exec_cb(&a, bar_anim_exec);
	lv_anim_set_values(&a, min, max);
	lv_anim_set_duration(&a, period_ms);
	lv_anim_set_playback_duration(&a, period_ms);
	lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
	lv_anim_set_delay(&a, delay_ms);
	lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
	lv_anim_start(&a);
}

static void slider_anim_exec(void *obj, int32_t val)
{
	lv_slider_set_value((lv_obj_t *)obj, val, LV_ANIM_OFF);
}

static void add_slider_anim(lv_obj_t *slider, uint32_t period_ms,
			     uint32_t delay_ms)
{
	lv_anim_t a;

	lv_anim_init(&a);
	lv_anim_set_var(&a, slider);
	lv_anim_set_exec_cb(&a, slider_anim_exec);
	lv_anim_set_values(&a, 0, 100);
	lv_anim_set_duration(&a, period_ms);
	lv_anim_set_playback_duration(&a, period_ms);
	lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
	lv_anim_set_delay(&a, delay_ms);
	lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
	lv_anim_start(&a);
}

static void roller_timer_cb(lv_timer_t *t)
{
	lv_obj_t *r = (lv_obj_t *)lv_timer_get_user_data(t);
	uint32_t sel = lv_roller_get_selected(r);

	lv_roller_set_selected(r, sel + 1, LV_ANIM_ON);
}

/* ── Screen construction ──────────────────────────────────────────────────── */
lv_obj_t *screen_widgets_create(void)
{
	/* Reset LED state */
	for (int i = 0; i < 6; i++) {
		leds[i] = NULL;
	}
	led_step  = 0;
	led_timer = NULL;

	lv_obj_t *scr = lv_obj_create(NULL);

	lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

	/* ── Header ─────────────────────────────────────────────────────── */
	lv_obj_t *hdr = lv_obj_create(scr);

	lv_obj_set_pos(hdr, 0, 0);
	lv_obj_set_size(hdr, SCR_W, HDR_H);
	lv_obj_set_style_bg_color(hdr, lv_color_hex(C_CARD), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(hdr, 0, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(hdr, LV_SCROLLBAR_MODE_OFF);

	lv_obj_t *ht = lv_label_create(hdr);

	lv_label_set_text(ht, "LVGL WIDGET GALLERY");
	lv_obj_set_style_text_font(ht, &lv_font_montserrat_32, LV_PART_MAIN);
	lv_obj_set_style_text_color(ht, lv_color_white(), LV_PART_MAIN);
	lv_obj_align(ht, LV_ALIGN_LEFT_MID, 16, 0);

	lv_obj_t *sub = lv_label_create(hdr);

	lv_label_set_text(sub, "LVGL 9.5 on PIC64GX  \xe2\x80\x83  all animations native");
	lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_style_text_color(sub, lv_color_hex(C_SUBTLE), LV_PART_MAIN);
	lv_obj_align(sub, LV_ALIGN_RIGHT_MID, -16, 0);

	/* Accent rule */
	lv_obj_t *rule = lv_obj_create(scr);

	lv_obj_set_pos(rule, 0, HDR_H);
	lv_obj_set_size(rule, SCR_W, 2);
	lv_obj_set_style_bg_color(rule, lv_color_hex(C_ACCENT), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_border_width(rule, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(rule, 0, LV_PART_MAIN);

	/* ── Cell (0,0): Arc gauges ──────────────────────────────────────── */
	lv_obj_t *c00 = cell(scr, 0, 0, "ARC GAUGES");

	static const uint32_t arc_cols[] = { C_ACCENT, C_GREEN, C_PURPLE };
	static const uint32_t arc_periods[] = { 2000, 2700, 1600 };

	for (int i = 0; i < 3; i++) {
		lv_obj_t *a = lv_arc_create(c00);

		lv_arc_set_range(a, 0, 100);
		lv_arc_set_bg_angles(a, 135, 45);
		lv_arc_set_mode(a, LV_ARC_MODE_NORMAL);
		lv_obj_set_size(a, 90, 90);
		lv_obj_set_pos(a, 10 + i * 130, 28);
		lv_obj_set_style_arc_color(a, lv_color_hex(C_BORDER), LV_PART_MAIN);
		lv_obj_set_style_arc_width(a, 10, LV_PART_MAIN);
		lv_obj_set_style_arc_color(a, lv_color_hex(arc_cols[i]),
					    LV_PART_INDICATOR);
		lv_obj_set_style_arc_width(a, 10, LV_PART_INDICATOR);
		lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
		lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);

		add_arc_anim(a, 0, 100, arc_periods[i], i * 400);
	}

	/* Labels */
	static const char *arc_labels[] = { "LOAD", "TEMP", "VOLT" };
	static const uint32_t lbl_cols[] = { C_ACCENT, C_GREEN, C_PURPLE };

	for (int i = 0; i < 3; i++) {
		lv_obj_t *l = lv_label_create(c00);

		lv_label_set_text(l, arc_labels[i]);
		lv_obj_set_style_text_font(l, &lv_font_montserrat_14, LV_PART_MAIN);
		lv_obj_set_style_text_color(l, lv_color_hex(lbl_cols[i]), LV_PART_MAIN);
		lv_obj_set_pos(l, 30 + i * 130, CELL_H - 2 * PAD - 44);
	}

	/* Description */
	lv_obj_t *dl = lv_label_create(c00);

	lv_label_set_text(dl, "270\xc2\xb0 sweep\nease-in-out\nindependent phase");
	lv_obj_set_style_text_font(dl, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_style_text_color(dl, lv_color_hex(C_SUBTLE), LV_PART_MAIN);
	lv_obj_set_pos(dl, 0, CELL_H - 2 * PAD - 64);

	/* ── Cell (1,0): Sliders ─────────────────────────────────────────── */
	lv_obj_t *c10 = cell(scr, 1, 0, "SLIDERS");

	static const uint32_t sl_cols[]    = { C_ACCENT, C_GREEN, C_YELLOW };
	static const uint32_t sl_periods[] = { 1800, 2400, 1400 };
	static const char    *sl_names[]   = { "FREQUENCY", "AMPLITUDE", "PHASE" };

	for (int i = 0; i < 3; i++) {
		lv_obj_t *nl = lv_label_create(c10);

		lv_label_set_text(nl, sl_names[i]);
		lv_obj_set_style_text_font(nl, &lv_font_montserrat_14, LV_PART_MAIN);
		lv_obj_set_style_text_color(nl, lv_color_hex(C_SUBTLE), LV_PART_MAIN);
		lv_obj_set_pos(nl, 0, 26 + i * 72);

		lv_obj_t *sl = lv_slider_create(c10);

		lv_slider_set_range(sl, 0, 100);
		lv_obj_set_size(sl, CELL_W - 2 * PAD - 10, 14);
		lv_obj_set_pos(sl, 0, 44 + i * 72);
		lv_obj_set_style_bg_color(sl, lv_color_hex(C_BORDER), LV_PART_MAIN);
		lv_obj_set_style_bg_color(sl, lv_color_hex(sl_cols[i]),
					    LV_PART_INDICATOR);
		lv_obj_set_style_bg_color(sl, lv_color_hex(sl_cols[i]),
					    LV_PART_KNOB);
		lv_obj_remove_flag(sl, LV_OBJ_FLAG_CLICKABLE);

		add_slider_anim(sl, sl_periods[i], i * 600);
	}

	/* ── Cell (0,1): LED indicators ──────────────────────────────────── */
	lv_obj_t *c01 = cell(scr, 0, 1, "LED INDICATORS");

	static const uint32_t led_cols[] = {
		C_GREEN, C_ACCENT, C_YELLOW, C_RED, C_PURPLE, C_ORANGE
	};
	static const char *led_names[] = {
		"PWR", "SYS", "NET", "ERR", "DBG", "I/O"
	};

	for (int i = 0; i < 6; i++) {
		lv_obj_t *led = lv_led_create(c01);

		lv_led_set_color(led, lv_color_hex(led_cols[i]));
		lv_led_off(led);
		lv_obj_set_size(led, 28, 28);
		lv_obj_set_pos(led, 10 + (i % 3) * 120, 32 + (i / 3) * 80);
		leds[i] = led;

		lv_obj_t *nl = lv_label_create(c01);

		lv_label_set_text(nl, led_names[i]);
		lv_obj_set_style_text_font(nl, &lv_font_montserrat_14, LV_PART_MAIN);
		lv_obj_set_style_text_color(nl, lv_color_hex(led_cols[i]),
					    LV_PART_MAIN);
		lv_obj_set_pos(nl, 10 + (i % 3) * 120, 64 + (i / 3) * 80);
	}

	/* LED ripple driven by LVGL timer (runs inside lv_timer_handler) */
	led_timer = lv_timer_create(led_timer_cb, 200, NULL);

	lv_obj_t *dl2 = lv_label_create(c01);

	lv_label_set_text(dl2, "ripple @ 200 ms");
	lv_obj_set_style_text_font(dl2, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_style_text_color(dl2, lv_color_hex(C_SUBTLE), LV_PART_MAIN);
	lv_obj_align(dl2, LV_ALIGN_BOTTOM_MID, 0, -4);

	/* ── Cell (1,1): Progress bars ───────────────────────────────────── */
	lv_obj_t *c11 = cell(scr, 1, 1, "PROGRESS BARS");

	static const char    *bar_names[]  = { "SIGNAL", "POWER", "SPEED", "UTIL" };
	static const uint32_t bar_cols[]   = { C_GREEN, C_ACCENT, C_YELLOW, C_ORANGE };
	static const uint32_t bar_periods[]= { 2200, 1800, 1400, 2600 };

	for (int i = 0; i < 4; i++) {
		lv_obj_t *nl = lv_label_create(c11);

		lv_label_set_text(nl, bar_names[i]);
		lv_obj_set_style_text_font(nl, &lv_font_montserrat_14, LV_PART_MAIN);
		lv_obj_set_style_text_color(nl, lv_color_hex(C_SUBTLE), LV_PART_MAIN);
		lv_obj_set_pos(nl, 0, 26 + i * 56);

		lv_obj_t *b = lv_bar_create(c11);

		lv_bar_set_range(b, 0, 100);
		lv_obj_set_size(b, CELL_W - 2 * PAD - 10, 16);
		lv_obj_set_pos(b, 0, 44 + i * 56);
		lv_obj_set_style_bg_color(b, lv_color_hex(C_BORDER), LV_PART_MAIN);
		lv_obj_set_style_bg_color(b, lv_color_hex(bar_cols[i]),
					    LV_PART_INDICATOR);
		lv_obj_set_style_radius(b, 4, LV_PART_MAIN);
		lv_obj_set_style_radius(b, 4, LV_PART_INDICATOR);

		add_bar_anim(b, 5, 95, bar_periods[i], i * 500);
	}

	/* ── Cell (2,0): Roller ──────────────────────────────────────────── */
	lv_obj_t *c20 = cell(scr, 2, 0, "ROLLER");

	lv_obj_t *roller = lv_roller_create(c20);

	lv_roller_set_options(roller,
			      "SiFive U54 Core 0\n"
			      "SiFive U54 Core 1\n"
			      "SiFive U54 Core 2\n"
			      "SiFive U54 Core 3\n"
			      "HSS Bootloader\n"
			      "Zephyr RTOS\n"
			      "LVGL 9.5\n"
			      "PIC64GX SoC",
			      LV_ROLLER_MODE_INFINITE);
	lv_roller_set_visible_row_count(roller, 4);
	lv_obj_set_width(roller, CELL_W - 2 * PAD - 10);
	lv_obj_align(roller, LV_ALIGN_CENTER, 0, 10);
	lv_obj_set_style_text_font(roller, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_style_text_color(roller, lv_color_hex(C_WHITE), LV_PART_MAIN);
	lv_obj_set_style_bg_color(roller, lv_color_hex(C_BG), LV_PART_MAIN);
	lv_obj_set_style_bg_color(roller, lv_color_hex(C_BORDER),
				   LV_PART_SELECTED);
	lv_obj_set_style_border_color(roller, lv_color_hex(C_BORDER),
				       LV_PART_MAIN);
	lv_obj_remove_flag(roller, LV_OBJ_FLAG_CLICKABLE);

	/* Auto-scroll via LVGL timer — stored so we can delete it on screen unload */
	roller_timer = lv_timer_create(roller_timer_cb, 1200, roller);

	/* ── Cell (2,1): Switches & Checkboxes ───────────────────────────── */
	lv_obj_t *c21 = cell(scr, 2, 1, "SWITCHES  &  CHECKBOXES");

	static const char *sw_names[]  = { "SMP ENABLE", "DISPLAY ON" };
	static const char *chk_names[] = { "FPU Sharing", "LVGL Workq", "DynIRQ" };

	for (int i = 0; i < 2; i++) {
		lv_obj_t *sw = lv_switch_create(c21);

		lv_obj_align(sw, LV_ALIGN_TOP_LEFT, 0, 28 + i * 50);
		lv_obj_set_style_bg_color(sw, lv_color_hex(C_GREEN),
					   LV_PART_INDICATOR);
		lv_obj_remove_flag(sw, LV_OBJ_FLAG_CLICKABLE);
		if (i == 0) {
			lv_obj_add_state(sw, LV_STATE_CHECKED);
		}

		lv_obj_t *nl = lv_label_create(c21);

		lv_label_set_text(nl, sw_names[i]);
		lv_obj_set_style_text_font(nl, &lv_font_montserrat_14, LV_PART_MAIN);
		lv_obj_set_style_text_color(nl, lv_color_hex(C_WHITE), LV_PART_MAIN);
		lv_obj_set_pos(nl, 66, 30 + i * 50);
	}

	for (int i = 0; i < 3; i++) {
		lv_obj_t *cb = lv_checkbox_create(c21);

		lv_checkbox_set_text(cb, chk_names[i]);
		lv_obj_set_pos(cb, 0, 140 + i * 40);
		lv_obj_set_style_text_font(cb, &lv_font_montserrat_14, LV_PART_MAIN);
		lv_obj_set_style_text_color(cb, lv_color_hex(C_WHITE), LV_PART_MAIN);
		lv_obj_set_style_border_color(cb, lv_color_hex(C_ACCENT),
					       LV_PART_INDICATOR);
		lv_obj_set_style_bg_color(cb, lv_color_hex(C_ACCENT),
					   LV_PART_INDICATOR);
		lv_obj_remove_flag(cb, LV_OBJ_FLAG_CLICKABLE);
		if (i == 0 || i == 2) {
			lv_obj_add_state(cb, LV_STATE_CHECKED);
		}
	}

	/* Register delete callback so timers are stopped before screen objects free */
	lv_obj_add_event_cb(scr, widgets_screen_delete_cb, LV_EVENT_DELETE, NULL);

	return scr;
}

void screen_widgets_update(void)
{
	/* Intentionally empty — all animation driven by lv_anim_t / lv_timer_t */
}
