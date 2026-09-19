/* SPDX-License-Identifier: Apache-2.0
 *
 * Screen 1: Vehicle Dashboard
 *
 * Shows 6 animated instruments: speed, RPM, coolant, fuel, boost, battery.
 * A scripted 12-second driving cycle drives smooth arc animations via lv_anim_t.
 */

#include "screen_dashboard.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
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
#define C_WHITE  0xE6EDF3u
#define C_SUBTLE 0x8B949Eu
#define C_GREY   0x484F58u

/* ── Layout ───────────────────────────────────────────────────────────────── */
#define SCR_W  1280
#define SCR_H   720
#define HDR_H    56
#define BODY_Y  (HDR_H + 4)
#define BODY_H  (SCR_H - BODY_Y - 4)
#define CELL_W  406   /* 3 columns */
#define CELL_H  (BODY_H / 2 - 4)
#define ARC_SZ  180   /* arc widget diameter */
#define BAR_W   340
#define BAR_H    18

/* ── Widget handles ───────────────────────────────────────────────────────── */
static lv_obj_t *speed_arc;
static lv_obj_t *speed_lbl;
static lv_obj_t *rpm_arc;
static lv_obj_t *rpm_lbl;
static lv_obj_t *coolant_arc;
static lv_obj_t *coolant_lbl;
static lv_obj_t *fuel_bar;
static lv_obj_t *fuel_lbl;
static lv_obj_t *boost_bar;
static lv_obj_t *boost_lbl;
static lv_obj_t *batt_bar;
static lv_obj_t *batt_lbl;
static lv_obj_t *gear_lbl;
static lv_obj_t *status_lbl;

/* ── Driving simulation state ─────────────────────────────────────────────── */
struct drive_state {
	int speed;    /* km/h */
	int rpm;      /* 1/min */
	int gear;     /* 0=N, 1-6 */
	int coolant;  /* °C */
	int fuel;     /* % */
	int boost;    /* 0-30 psi×10 */
	int battery;  /* % */
};

static struct drive_state ds = {
	.speed   = 0,
	.rpm     = 900,
	.gear    = 0,
	.coolant = 72,
	.fuel    = 85,
	.boost   = 0,
	.battery = 87,
};

/* Scripted driving sequence phases */
struct phase {
	int ticks;    /* seconds to hold this phase */
	int speed;
	int rpm;
	int gear;
	const char *status;
};

static const struct phase script[] = {
	{ 2, 0,   900,  0, "IDLE"      },
	{ 2, 30,  2800, 2, "PULL AWAY" },
	{ 2, 80,  2400, 3, "CRUISING"  },
	{ 2, 160, 4800, 4, "OVERTAKE"  },
	{ 2, 140, 3200, 5, "HIGHWAY"   },
	{ 2, 60,  1800, 3, "BRAKING"   },
};
#define N_PHASES ((int)ARRAY_SIZE(script))

static int phase_idx;
static int phase_tick;

/* ── Animate an arc to a new value ───────────────────────────────────────── */
static void arc_anim_to(lv_obj_t *arc, int32_t new_val, uint32_t dur_ms)
{
	lv_anim_t a;

	lv_anim_init(&a);
	lv_anim_set_var(&a, arc);
	lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_arc_set_value);
	lv_anim_set_values(&a, lv_arc_get_value(arc), new_val);
	lv_anim_set_duration(&a, dur_ms);
	lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
	lv_anim_start(&a);
}

/* ── Build helpers ────────────────────────────────────────────────────────── */
static lv_obj_t *panel(lv_obj_t *parent, int col, int row)
{
	int x = col * (CELL_W + 1);
	int y = BODY_Y + row * (CELL_H + 4);
	lv_obj_t *p = lv_obj_create(parent);

	lv_obj_set_pos(p, x, y);
	lv_obj_set_size(p, CELL_W, CELL_H);
	lv_obj_set_style_bg_color(p, lv_color_hex(C_CARD), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(p, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_border_color(p, lv_color_hex(C_BORDER), LV_PART_MAIN);
	lv_obj_set_style_border_width(p, 1, LV_PART_MAIN);
	lv_obj_set_style_radius(p, 8, LV_PART_MAIN);
	lv_obj_set_style_pad_all(p, 8, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_OFF);
	return p;
}

static lv_obj_t *make_gauge_arc(lv_obj_t *parent,
				 int min, int max,
				 uint32_t ind_col)
{
	lv_obj_t *a = lv_arc_create(parent);

	lv_arc_set_range(a, min, max);
	lv_arc_set_value(a, min);
	lv_arc_set_bg_angles(a, 135, 45);
	lv_arc_set_mode(a, LV_ARC_MODE_NORMAL);
	lv_obj_set_size(a, ARC_SZ, ARC_SZ);
	lv_obj_align(a, LV_ALIGN_CENTER, 0, -10);

	lv_obj_set_style_arc_color(a, lv_color_hex(C_BORDER), LV_PART_MAIN);
	lv_obj_set_style_arc_width(a, 14, LV_PART_MAIN);
	lv_obj_set_style_arc_color(a, lv_color_hex(ind_col), LV_PART_INDICATOR);
	lv_obj_set_style_arc_width(a, 14, LV_PART_INDICATOR);

	/* Hide the knob */
	lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
	lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);

	return a;
}

static lv_obj_t *gauge_label(lv_obj_t *parent, const char *text,
			      uint32_t col, const lv_font_t *font)
{
	lv_obj_t *l = lv_label_create(parent);

	lv_label_set_text(l, text);
	lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
	lv_obj_set_style_text_color(l, lv_color_hex(col), LV_PART_MAIN);
	lv_obj_align(l, LV_ALIGN_CENTER, 0, 28);
	return l;
}

static lv_obj_t *title_label(lv_obj_t *parent, const char *text)
{
	lv_obj_t *l = lv_label_create(parent);

	lv_label_set_text(l, text);
	lv_obj_set_style_text_font(l, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_style_text_color(l, lv_color_hex(C_SUBTLE), LV_PART_MAIN);
	lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 0);
	return l;
}

static lv_obj_t *horiz_bar(lv_obj_t *parent, int min, int max, uint32_t col)
{
	lv_obj_t *b = lv_bar_create(parent);

	lv_bar_set_range(b, min, max);
	lv_bar_set_value(b, min, LV_ANIM_OFF);
	lv_obj_set_size(b, BAR_W, BAR_H);
	lv_obj_align(b, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_color(b, lv_color_hex(C_BORDER), LV_PART_MAIN);
	lv_obj_set_style_bg_color(b, lv_color_hex(col),      LV_PART_INDICATOR);
	lv_obj_set_style_radius(b, 4, LV_PART_MAIN);
	lv_obj_set_style_radius(b, 4, LV_PART_INDICATOR);
	return b;
}

/* ── Screen construction ──────────────────────────────────────────────────── */
lv_obj_t *screen_dashboard_create(void)
{
	/* Reset state */
	phase_idx  = 0;
	phase_tick = 0;

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

	lv_label_set_text(ht, "VEHICLE TELEMETRY");
	lv_obj_set_style_text_font(ht, &lv_font_montserrat_32, LV_PART_MAIN);
	lv_obj_set_style_text_color(ht, lv_color_white(), LV_PART_MAIN);
	lv_obj_align(ht, LV_ALIGN_LEFT_MID, 16, 0);

	/* Gear indicator — large centred digit */
	gear_lbl = lv_label_create(hdr);
	lv_label_set_text(gear_lbl, "N");
	lv_obj_set_style_text_font(gear_lbl, &lv_font_montserrat_48, LV_PART_MAIN);
	lv_obj_set_style_text_color(gear_lbl, lv_color_hex(C_GREY), LV_PART_MAIN);
	lv_obj_align(gear_lbl, LV_ALIGN_RIGHT_MID, -80, 0);

	lv_obj_t *gear_ttl = lv_label_create(hdr);

	lv_label_set_text(gear_ttl, "GEAR");
	lv_obj_set_style_text_font(gear_ttl, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_style_text_color(gear_ttl, lv_color_hex(C_SUBTLE), LV_PART_MAIN);
	lv_obj_align(gear_ttl, LV_ALIGN_RIGHT_MID, -130, -14);

	status_lbl = lv_label_create(hdr);
	lv_label_set_text(status_lbl, "IDLE");
	lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_20, LV_PART_MAIN);
	lv_obj_set_style_text_color(status_lbl, lv_color_hex(C_YELLOW), LV_PART_MAIN);
	lv_obj_align(status_lbl, LV_ALIGN_RIGHT_MID, -200, 0);

	/* Accent rule */
	lv_obj_t *rule = lv_obj_create(scr);

	lv_obj_set_pos(rule, 0, HDR_H);
	lv_obj_set_size(rule, SCR_W, 4);
	lv_obj_set_style_bg_color(rule, lv_color_hex(C_ACCENT), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_border_width(rule, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(rule, 0, LV_PART_MAIN);

	/* ── Row 0, Col 0: SPEED ─────────────────────────────────────────── */
	lv_obj_t *p_speed = panel(scr, 0, 0);

	title_label(p_speed, "SPEED  km/h");
	speed_arc = make_gauge_arc(p_speed, 0, 220, C_ACCENT);
	speed_lbl = lv_label_create(p_speed);
	lv_label_set_text(speed_lbl, "0");
	lv_obj_set_style_text_font(speed_lbl, &lv_font_montserrat_48, LV_PART_MAIN);
	lv_obj_set_style_text_color(speed_lbl, lv_color_white(), LV_PART_MAIN);
	lv_obj_align(speed_lbl, LV_ALIGN_CENTER, 0, -10);

	/* ── Row 0, Col 1: RPM ───────────────────────────────────────────── */
	lv_obj_t *p_rpm = panel(scr, 1, 0);

	title_label(p_rpm, "ENGINE RPM");
	rpm_arc = make_gauge_arc(p_rpm, 0, 8000, C_ORANGE);
	rpm_lbl = gauge_label(p_rpm, "900", C_WHITE, &lv_font_montserrat_32);
	lv_obj_align(rpm_lbl, LV_ALIGN_CENTER, 0, -10);

	/* ── Row 0, Col 2: BOOST ─────────────────────────────────────────── */
	lv_obj_t *p_boost = panel(scr, 2, 0);

	title_label(p_boost, "BOOST  psi");

	lv_obj_t *bl = lv_label_create(p_boost);

	lv_label_set_text(bl, "0.0");
	lv_obj_set_style_text_font(bl, &lv_font_montserrat_48, LV_PART_MAIN);
	lv_obj_set_style_text_color(bl, lv_color_hex(C_GREEN), LV_PART_MAIN);
	lv_obj_align(bl, LV_ALIGN_CENTER, 0, -20);
	boost_lbl = bl;

	boost_bar = horiz_bar(p_boost, 0, 300, C_GREEN);
	lv_obj_align(boost_bar, LV_ALIGN_CENTER, 0, 30);
	{
		lv_obj_t *u = lv_label_create(p_boost);

		lv_label_set_text(u, "max 30 psi");
		lv_obj_set_style_text_font(u, &lv_font_montserrat_14, LV_PART_MAIN);
		lv_obj_set_style_text_color(u, lv_color_hex(C_SUBTLE), LV_PART_MAIN);
		lv_obj_align(u, LV_ALIGN_BOTTOM_MID, 0, -4);
	}

	/* ── Row 1, Col 0: COOLANT ───────────────────────────────────────── */
	lv_obj_t *p_cool = panel(scr, 0, 1);

	title_label(p_cool, "COOLANT  \xc2\xb0" "C");
	coolant_arc = make_gauge_arc(p_cool, 40, 120, C_GREEN);
	lv_arc_set_value(coolant_arc, 72);
	coolant_lbl = gauge_label(p_cool, "72\xc2\xb0", C_WHITE, &lv_font_montserrat_32);
	lv_obj_align(coolant_lbl, LV_ALIGN_CENTER, 0, -10);

	/* ── Row 1, Col 1: FUEL ──────────────────────────────────────────── */
	lv_obj_t *p_fuel = panel(scr, 1, 1);

	title_label(p_fuel, "FUEL LEVEL");

	lv_obj_t *fl = lv_label_create(p_fuel);

	lv_label_set_text(fl, "85%");
	lv_obj_set_style_text_font(fl, &lv_font_montserrat_48, LV_PART_MAIN);
	lv_obj_set_style_text_color(fl, lv_color_hex(C_GREEN), LV_PART_MAIN);
	lv_obj_align(fl, LV_ALIGN_CENTER, 0, -20);
	fuel_lbl = fl;

	fuel_bar = horiz_bar(p_fuel, 0, 100, C_GREEN);
	lv_bar_set_value(fuel_bar, 85, LV_ANIM_OFF);
	lv_obj_align(fuel_bar, LV_ALIGN_CENTER, 0, 30);

	/* ── Row 1, Col 2: BATTERY ───────────────────────────────────────── */
	lv_obj_t *p_batt = panel(scr, 2, 1);

	title_label(p_batt, "12V BATTERY");

	lv_obj_t *bt = lv_label_create(p_batt);

	lv_label_set_text(bt, "87%");
	lv_obj_set_style_text_font(bt, &lv_font_montserrat_48, LV_PART_MAIN);
	lv_obj_set_style_text_color(bt, lv_color_hex(C_ACCENT), LV_PART_MAIN);
	lv_obj_align(bt, LV_ALIGN_CENTER, 0, -20);
	batt_lbl = bt;

	batt_bar = horiz_bar(p_batt, 0, 100, C_ACCENT);
	lv_bar_set_value(batt_bar, 87, LV_ANIM_OFF);
	lv_obj_align(batt_bar, LV_ALIGN_CENTER, 0, 30);

	/* Kick off first arc animation */
	arc_anim_to(speed_arc, ds.speed, 500);
	arc_anim_to(rpm_arc, ds.rpm, 500);
	arc_anim_to(coolant_arc, ds.coolant, 500);

	return scr;
}

/* ── 1 Hz update ──────────────────────────────────────────────────────────── */
void screen_dashboard_update(void)
{
	if (!speed_arc) {
		return;
	}

	/* Advance driving script */
	const struct phase *ph = &script[phase_idx];

	phase_tick++;
	if (phase_tick >= ph->ticks) {
		phase_idx  = (phase_idx + 1) % N_PHASES;
		phase_tick = 0;
		ph = &script[phase_idx];
	}

	ds.speed  = ph->speed;
	ds.rpm    = ph->rpm;
	ds.gear   = ph->gear;
	ds.boost  = (ds.rpm > 2000) ? (ds.rpm - 2000) / 100 : 0;

	/* Coolant slowly rises to 92, then holds */
	if (ds.coolant < 92) {
		ds.coolant++;
	}

	/* Fuel drops slowly */
	if (ds.fuel > 5 && (phase_idx % 3 == 0)) {
		ds.fuel--;
	}

	/* Animate arcs */
	arc_anim_to(speed_arc, ds.speed, 900);
	arc_anim_to(rpm_arc, ds.rpm, 900);
	arc_anim_to(coolant_arc, ds.coolant, 1500);

	/* Bars */
	lv_bar_set_value(fuel_bar, ds.fuel, LV_ANIM_ON);
	lv_bar_set_value(boost_bar, ds.boost * 10, LV_ANIM_ON);
	lv_bar_set_value(batt_bar, ds.battery, LV_ANIM_OFF);

	/* Coolant indicator colour: green < 90, yellow < 100, red ≥ 100 */
	uint32_t cc = (ds.coolant >= 100) ? C_RED :
		      (ds.coolant >= 90)  ? C_YELLOW : C_GREEN;

	lv_obj_set_style_arc_color(coolant_arc, lv_color_hex(cc), LV_PART_INDICATOR);

	/* Labels */
	char buf[16];

	snprintk(buf, sizeof(buf), "%d", ds.speed);
	lv_label_set_text(speed_lbl, buf);

	snprintk(buf, sizeof(buf), "%d", ds.rpm);
	lv_label_set_text(rpm_lbl, buf);

	snprintk(buf, sizeof(buf), "%d\xc2\xb0", ds.coolant);
	lv_label_set_text(coolant_lbl, buf);

	snprintk(buf, sizeof(buf), "%d%%", ds.fuel);
	lv_label_set_text(fuel_lbl, buf);

	snprintk(buf, sizeof(buf), "%.1f", (double)ds.boost / 10.0);
	lv_label_set_text(boost_lbl, buf);

	snprintk(buf, sizeof(buf), "%d%%", ds.battery);
	lv_label_set_text(batt_lbl, buf);

	/* Gear display */
	if (ds.gear == 0) {
		lv_label_set_text(gear_lbl, "N");
		lv_obj_set_style_text_color(gear_lbl, lv_color_hex(C_GREY),
					    LV_PART_MAIN);
	} else {
		snprintk(buf, sizeof(buf), "%d", ds.gear);
		lv_label_set_text(gear_lbl, buf);
		uint32_t gc = (ds.gear <= 2) ? C_GREEN :
			      (ds.gear <= 4) ? C_YELLOW : C_ORANGE;

		lv_obj_set_style_text_color(gear_lbl, lv_color_hex(gc), LV_PART_MAIN);
	}

	lv_label_set_text(status_lbl, ph->status);
}
