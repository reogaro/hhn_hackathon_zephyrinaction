/* SPDX-License-Identifier: Apache-2.0
 *
 * Screen 0: Statistics HUD
 *
 * Layout
 *   Header (y 0..63)   board name · live uptime
 *   Left   (x 0..819)  live thread table
 *   Right  (x 827..)   system info · per-core load bars · memory
 */

#include "screen_hud.h"
#include "demo_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/version.h>
#include <lvgl.h>

/* ── Colour palette ───────────────────────────────────────────────────────── */
#define C_BG     0x0D1117u
#define C_CARD   0x161B22u
#define C_BORDER 0x30363Du
#define C_ACCENT 0x58A6FFu
#define C_GREEN  0x3FB950u
#define C_YELLOW 0xD29922u
#define C_RED    0xF85149u
#define C_WHITE  0xE6EDF3u
#define C_SUBTLE 0x8B949Eu

/* ── Layout ───────────────────────────────────────────────────────────────── */
#define SCR_W   1280
#define SCR_H    720
#define HDR_H     64
#define BODY_Y   (HDR_H + 2)
#define LEFT_W   820
#define R_X      (LEFT_W + 7)
#define R_W      (SCR_W - R_X)
#define PAD       10

/* ── Thread state bits ────────────────────────────────────────────────────── */
#define _TS_PENDING   BIT(1)
#define _TS_DEAD      BIT(3)
#define _TS_SUSPENDED BIT(4)
#define _TS_QUEUED    BIT(6)

/* ── Widget handles ───────────────────────────────────────────────────────── */
static lv_obj_t *uptime_lbl;
static lv_obj_t *fps_lbl;
static lv_obj_t *thr_table;
static lv_obj_t *heap_bar;
static lv_obj_t *heap_lbl;
static lv_obj_t *core_bar[4];
static lv_obj_t *core_lbl[4];

/* ── Thread collection ────────────────────────────────────────────────────── */
#define MAX_THR 18

struct trow { char name[16]; char pri[5]; char state[6]; char stack[20]; };
static struct trow trows[MAX_THR];
static int         trow_n;

static void collect_thread(const struct k_thread *t, void *arg)
{
	ARG_UNUSED(arg);
	if (trow_n >= MAX_THR) {
		return;
	}
	struct trow *r = &trows[trow_n++];
	const char *n = k_thread_name_get((k_tid_t)(uintptr_t)t);

	strncpy(r->name, (n && n[0]) ? n : "?", sizeof(r->name) - 1);
	r->name[sizeof(r->name) - 1] = '\0';

	snprintk(r->pri, sizeof(r->pri), "%d",
		 k_thread_priority_get((k_tid_t)(uintptr_t)t));

	uint8_t s = t->base.thread_state;
	const char *ss = (_TS_DEAD      & s) ? "dead"  :
			 (_TS_SUSPENDED & s) ? "susp"  :
			 (_TS_PENDING   & s) ? "wait"  :
			 (_TS_QUEUED    & s) ? "ready" : "run";

	strncpy(r->state, ss, sizeof(r->state) - 1);
	r->state[sizeof(r->state) - 1] = '\0';

	size_t unused = 0;
	k_thread_stack_space_get((k_tid_t)(uintptr_t)t, &unused);
	size_t total = t->stack_info.size;
	size_t used  = (total > unused) ? (total - unused) : 0;

	snprintk(r->stack, sizeof(r->stack), "%zu/%zuB", used, total);
}

/* ── Simulated per-core load (structured random walk) ────────────────────── */
static int32_t core_load[4] = { 45, 30, 60, 20 };

/* Simple xorshift per-core state for independent noise */
static uint32_t xr[4] = { 0xDEAD1u, 0xBEEF2u, 0xCAFE3u, 0xF00D4u };

static uint32_t xnext(int i)
{
	xr[i] ^= xr[i] << 13;
	xr[i] ^= xr[i] >> 17;
	xr[i] ^= xr[i] << 5;
	return xr[i];
}

static void update_core_loads(void)
{
	/* Each core drifts ±8 % per second with an independent triangular wave */
	static const int32_t t_offset[4] = { 0, 50, 100, 150 };
	static int32_t tick;

	tick++;

	for (int i = 0; i < 4; i++) {
		int32_t noise = (int32_t)(xnext(i) % 17) - 8;
		/* Triangular wave: period=200 ticks, amplitude ±15 */
		int32_t phase = (tick + t_offset[i]) % 200;
		int32_t wave = (phase < 100) ? phase : (200 - phase);

		wave = wave * 15 / 100 - 7;
		core_load[i] = CLAMP(50 + wave + noise, 5, 95);
	}
}

static lv_color_t load_color(int32_t pct)
{
	if (pct >= 80) {
		return lv_color_hex(C_RED);
	}
	if (pct >= 60) {
		return lv_color_hex(C_YELLOW);
	}
	return lv_color_hex(C_GREEN);
}

/* ── Build helpers ────────────────────────────────────────────────────────── */
static lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h)
{
	lv_obj_t *o = lv_obj_create(parent);

	lv_obj_set_pos(o, x, y);
	lv_obj_set_size(o, w, h);
	lv_obj_set_style_bg_color(o,     lv_color_hex(C_CARD),   LV_PART_MAIN);
	lv_obj_set_style_bg_opa(o,       LV_OPA_COVER,            LV_PART_MAIN);
	lv_obj_set_style_border_color(o, lv_color_hex(C_BORDER),  LV_PART_MAIN);
	lv_obj_set_style_border_width(o, 1,                        LV_PART_MAIN);
	lv_obj_set_style_radius(o,       6,                        LV_PART_MAIN);
	lv_obj_set_style_pad_all(o,      PAD,                      LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
	return o;
}

static lv_obj_t *row_label(lv_obj_t *p, int x, int y,
			    const char *text, uint32_t col)
{
	lv_obj_t *l = lv_label_create(p);

	lv_label_set_text(l, text);
	lv_obj_set_style_text_font(l,  &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_style_text_color(l, lv_color_hex(col),       LV_PART_MAIN);
	lv_obj_set_pos(l, x, y);
	return l;
}

static lv_obj_t *section_title(lv_obj_t *parent, const char *text)
{
	lv_obj_t *l = lv_label_create(parent);

	lv_label_set_text(l, text);
	lv_obj_set_style_text_font(l,  &lv_font_montserrat_20, LV_PART_MAIN);
	lv_obj_set_style_text_color(l, lv_color_hex(C_ACCENT),  LV_PART_MAIN);
	lv_obj_set_pos(l, 0, 0);
	return l;
}

/* ── Screen construction ──────────────────────────────────────────────────── */
lv_obj_t *screen_hud_create(void)
{
	/* Reset all widget handles */
	uptime_lbl = NULL; fps_lbl = NULL; thr_table = NULL;
	heap_bar = NULL; heap_lbl = NULL;
	for (int i = 0; i < 4; i++) {
		core_bar[i] = NULL; core_lbl[i] = NULL;
	}

	lv_obj_t *scr = lv_obj_create(NULL);

	lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

	/* ── Header ─────────────────────────────────────────────────────── */
	lv_obj_t *hdr = lv_obj_create(scr);

	lv_obj_set_pos(hdr, 0, 0);
	lv_obj_set_size(hdr, SCR_W, HDR_H);
	lv_obj_set_style_bg_color(hdr,    lv_color_hex(C_CARD), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(hdr,      LV_OPA_COVER,          LV_PART_MAIN);
	lv_obj_set_style_border_width(hdr, 0,                     LV_PART_MAIN);
	lv_obj_set_style_radius(hdr,       0,                     LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(hdr, LV_SCROLLBAR_MODE_OFF);

	lv_obj_t *title = lv_label_create(hdr);

	lv_label_set_text(title, "PIC64GX Curiosity Kit");
	lv_obj_set_style_text_font(title,  &lv_font_montserrat_32, LV_PART_MAIN);
	lv_obj_set_style_text_color(title, lv_color_white(),        LV_PART_MAIN);
	lv_obj_set_pos(title, 14, 6);

	lv_obj_t *sub = lv_label_create(hdr);

	lv_label_set_text(sub,
		"Zephyr v" KERNEL_VERSION_STRING
		"  \xe2\x80\x83"
		"LVGL 9.5"
		"  \xe2\x80\x83"
		"4\xc3\x97 SiFive U54 SMP @ 600 MHz"
		"  \xe2\x80\x83"
		"rv64imafdc");
	lv_obj_set_style_text_font(sub,  &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_style_text_color(sub, lv_color_hex(C_SUBTLE),  LV_PART_MAIN);
	lv_obj_set_pos(sub, 16, 44);

	uptime_lbl = lv_label_create(hdr);
	lv_label_set_text(uptime_lbl, "00h 00m 00s");
	lv_obj_set_style_text_font(uptime_lbl,  &lv_font_montserrat_20, LV_PART_MAIN);
	lv_obj_set_style_text_color(uptime_lbl, lv_color_hex(C_GREEN),   LV_PART_MAIN);
	lv_obj_align(uptime_lbl, LV_ALIGN_RIGHT_MID, -14, 0);

	/* Accent rule */
	lv_obj_t *rule = lv_obj_create(scr);

	lv_obj_set_pos(rule, 0, HDR_H);
	lv_obj_set_size(rule, SCR_W, 2);
	lv_obj_set_style_bg_color(rule, lv_color_hex(C_ACCENT), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_border_width(rule, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(rule, 0, LV_PART_MAIN);

	/* ── Left: thread table ──────────────────────────────────────────── */
	lv_obj_t *left = card(scr, 0, BODY_Y, LEFT_W, SCR_H - BODY_Y);

	lv_obj_set_style_radius(left, 0, LV_PART_MAIN);
	lv_obj_set_style_border_width(left, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(left, 8, LV_PART_MAIN);
	section_title(left, "THREADS");

	thr_table = lv_table_create(left);
	lv_obj_set_pos(thr_table, 0, 28);
	lv_obj_set_size(thr_table, LEFT_W - 16, SCR_H - BODY_Y - 40);
	lv_obj_set_scrollbar_mode(thr_table, LV_SCROLLBAR_MODE_OFF);

	lv_table_set_column_count(thr_table, 4);
	lv_table_set_column_width(thr_table, 0, 198);
	lv_table_set_column_width(thr_table, 1,  56);
	lv_table_set_column_width(thr_table, 2,  86);
	lv_table_set_column_width(thr_table, 3, 454);

	lv_obj_set_style_border_width(thr_table, 0, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(thr_table, LV_OPA_TRANSP, LV_PART_MAIN);
	lv_obj_set_style_pad_all(thr_table, 0, LV_PART_MAIN);
	lv_obj_set_style_text_font(thr_table,    &lv_font_montserrat_14, LV_PART_ITEMS);
	lv_obj_set_style_text_color(thr_table,   lv_color_hex(C_WHITE),  LV_PART_ITEMS);
	lv_obj_set_style_bg_color(thr_table,     lv_color_hex(C_BG),     LV_PART_ITEMS);
	lv_obj_set_style_bg_opa(thr_table,       LV_OPA_COVER,           LV_PART_ITEMS);
	lv_obj_set_style_border_color(thr_table, lv_color_hex(C_BORDER), LV_PART_ITEMS);
	lv_obj_set_style_border_width(thr_table, 1,                       LV_PART_ITEMS);
	lv_obj_set_style_pad_top(thr_table,    3, LV_PART_ITEMS);
	lv_obj_set_style_pad_bottom(thr_table, 3, LV_PART_ITEMS);
	lv_obj_set_style_pad_left(thr_table,   6, LV_PART_ITEMS);
	lv_obj_set_style_pad_right(thr_table,  6, LV_PART_ITEMS);

	lv_table_set_cell_value(thr_table, 0, 0, "NAME");
	lv_table_set_cell_value(thr_table, 0, 1, "PRI");
	lv_table_set_cell_value(thr_table, 0, 2, "STATE");
	lv_table_set_cell_value(thr_table, 0, 3, "STACK USED / TOTAL");

	lv_table_set_row_count(thr_table, MAX_THR + 1);
	for (int i = 0; i < MAX_THR; i++) {
		for (int c = 0; c < 4; c++) {
			lv_table_set_cell_value(thr_table, (uint32_t)(i + 1), c, "");
		}
	}

	/* ── Right: system info ──────────────────────────────────────────── */
	const int sys_h = 160;
	lv_obj_t *sys = card(scr, R_X, BODY_Y, R_W, sys_h);

	section_title(sys, "SYSTEM INFO");

	static const struct { const char *k; const char *v; uint32_t vc; }
	si[] = {
		{ "Zephyr",  KERNEL_VERSION_STRING,              C_GREEN  },
		{ "LVGL",    "9.5.0",                             C_GREEN  },
		{ "Arch",    "rv64imafdc",                        C_WHITE  },
		{ "SMP",     "4\xc3\x97 SiFive U54 @ 600 MHz",   C_WHITE  },
		{ "Shell",   "UART0 115200 8N1",                  C_YELLOW },
	};
	int sy = 28;

	for (int i = 0; i < (int)ARRAY_SIZE(si); i++) {
		row_label(sys,   0, sy, si[i].k, C_SUBTLE);
		row_label(sys, 100, sy, si[i].v, si[i].vc);
		sy += 22;
	}

	/* ── Right: per-core load bars ───────────────────────────────────── */
	const int load_y = BODY_Y + sys_h + 6;
	const int load_h = 180;
	const int bar_w  = R_W - 2 * PAD;
	lv_obj_t *load_card = card(scr, R_X, load_y, R_W, load_h);

	section_title(load_card, "CORE LOAD");

	static const char *core_names[] = { "CORE 0", "CORE 1", "CORE 2", "CORE 3" };

	for (int i = 0; i < 4; i++) {
		int ry = 28 + i * 36;

		row_label(load_card, 0, ry, core_names[i], C_SUBTLE);
		core_bar[i] = lv_bar_create(load_card);
		lv_bar_set_range(core_bar[i], 0, 100);
		lv_bar_set_value(core_bar[i], core_load[i], LV_ANIM_OFF);
		lv_obj_set_size(core_bar[i], bar_w - 70, 10);
		lv_obj_set_pos(core_bar[i], 70, ry + 2);
		lv_obj_set_style_bg_color(core_bar[i], lv_color_hex(C_BORDER),
					  LV_PART_MAIN);
		lv_obj_set_style_bg_color(core_bar[i], load_color(core_load[i]),
					  LV_PART_INDICATOR);

		core_lbl[i] = lv_label_create(load_card);
		char buf[8];

		snprintk(buf, sizeof(buf), "%d%%", (int)core_load[i]);
		lv_label_set_text(core_lbl[i], buf);
		lv_obj_set_style_text_font(core_lbl[i], &lv_font_montserrat_14,
					   LV_PART_MAIN);
		lv_obj_set_style_text_color(core_lbl[i], load_color(core_load[i]),
					    LV_PART_MAIN);
		lv_obj_set_pos(core_lbl[i], bar_w - 36, ry);
	}

	/* ── Right: memory card ──────────────────────────────────────────── */
	const int mem_y = load_y + load_h + 6;
	const int mem_h = SCR_H - mem_y;
	lv_obj_t *mem = card(scr, R_X, mem_y, R_W, mem_h);

	section_title(mem, "MEMORY");

	row_label(mem, 0, 28, "SRAM image", C_SUBTLE);
	lv_obj_t *sbar = lv_bar_create(mem);

	lv_bar_set_range(sbar, 0, 100);
	lv_bar_set_value(sbar, 75, LV_ANIM_OFF);
	lv_obj_set_size(sbar, bar_w, 10);
	lv_obj_set_pos(sbar, 0, 48);
	lv_obj_set_style_bg_color(sbar, lv_color_hex(C_BORDER), LV_PART_MAIN);
	lv_obj_set_style_bg_color(sbar, lv_color_hex(C_ACCENT),  LV_PART_INDICATOR);
	fps_lbl = row_label(mem, 0, 62, "~848/1024 KB (83%)", C_WHITE);

	row_label(mem, 0, 86, "LVGL Heap", C_SUBTLE);
	heap_bar = lv_bar_create(mem);
	lv_bar_set_range(heap_bar, 0, 100);
	lv_bar_set_value(heap_bar, 0, LV_ANIM_OFF);
	lv_obj_set_size(heap_bar, bar_w, 10);
	lv_obj_set_pos(heap_bar, 0, 106);
	lv_obj_set_style_bg_color(heap_bar, lv_color_hex(C_BORDER), LV_PART_MAIN);
	lv_obj_set_style_bg_color(heap_bar, lv_color_hex(C_GREEN),  LV_PART_INDICATOR);
	heap_lbl = row_label(mem, 0, 120, "-- / -- B", C_WHITE);

	return scr;
}

/* ── 1 Hz update ──────────────────────────────────────────────────────────── */
void screen_hud_update(void)
{
	if (!uptime_lbl) {
		return;
	}

	/* Uptime */
	char buf[48];
	int64_t ms = k_uptime_get();
	int h = (int)(ms / 3600000LL);
	int m = (int)((ms % 3600000LL) / 60000LL);
	int s = (int)((ms % 60000LL) / 1000LL);

	snprintk(buf, sizeof(buf), "%02dh %02dm %02ds", h, m, s);
	lv_label_set_text(uptime_lbl, buf);

	/* FPS label (reusing fps_lbl widget in SRAM row) */
	snprintk(buf, sizeof(buf), "RENDER %u  OUT %u fps",
		 demo_get_render_fps(), demo_get_output_fps());
	lv_label_set_text(fps_lbl, buf);

	/* Thread table */
	trow_n = 0;
	k_thread_foreach(collect_thread, NULL);
	for (int i = 0; i < MAX_THR; i++) {
		uint32_t row = (uint32_t)(i + 1);

		if (i < trow_n) {
			lv_table_set_cell_value(thr_table, row, 0, trows[i].name);
			lv_table_set_cell_value(thr_table, row, 1, trows[i].pri);
			lv_table_set_cell_value(thr_table, row, 2, trows[i].state);
			lv_table_set_cell_value(thr_table, row, 3, trows[i].stack);
		} else {
			lv_table_set_cell_value(thr_table, row, 0, "");
			lv_table_set_cell_value(thr_table, row, 1, "");
			lv_table_set_cell_value(thr_table, row, 2, "");
			lv_table_set_cell_value(thr_table, row, 3, "");
		}
	}

	/* Core load bars */
	update_core_loads();
	for (int i = 0; i < 4; i++) {
		lv_bar_set_value(core_bar[i], core_load[i], LV_ANIM_ON);
		lv_obj_set_style_bg_color(core_bar[i], load_color(core_load[i]),
					  LV_PART_INDICATOR);
		lv_obj_set_style_text_color(core_lbl[i], load_color(core_load[i]),
					    LV_PART_MAIN);
		snprintk(buf, sizeof(buf), "%d%%", (int)core_load[i]);
		lv_label_set_text(core_lbl[i], buf);
	}

	/* LVGL heap */
	lv_mem_monitor_t mon;

	lv_mem_monitor(&mon);
	lv_bar_set_value(heap_bar, (int32_t)mon.used_pct, LV_ANIM_OFF);
	snprintk(buf, sizeof(buf), "%zu/%zuB (%u%%)",
		 mon.total_size - mon.free_size, mon.total_size,
		 (unsigned)mon.used_pct);
	lv_label_set_text(heap_lbl, buf);
}
