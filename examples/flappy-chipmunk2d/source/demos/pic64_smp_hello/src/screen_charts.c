/* SPDX-License-Identifier: Apache-2.0
 *
 * Screen 2: Live Telemetry Charts
 *
 * Left:        scrolling line chart — 4 cores CPU load %
 * Right-top:   bar chart — simulated TX/RX throughput
 * Right-bot:   memory breakdown bars (live heap from lv_mem_monitor)
 */

#include "screen_charts.h"

#include <zephyr/kernel.h>
#include <lvgl.h>

/* ── Colours ──────────────────────────────────────────────────────────────── */
#define C_BG      0x0D1117u
#define C_CARD    0x161B22u
#define C_BORDER  0x30363Du
#define C_ACCENT  0x58A6FFu
#define C_GREEN   0x3FB950u
#define C_YELLOW  0xD29922u
#define C_RED     0xF85149u
#define C_ORANGE  0xE06C2Bu
#define C_WHITE   0xE6EDF3u
#define C_SUBTLE  0x8B949Eu
#define C_PURPLE  0xBC8CFFu

/* ── Layout ───────────────────────────────────────────────────────────────── */
#define SCR_W   1280
#define SCR_H    720
#define HDR_H     50
#define BODY_Y   (HDR_H + 2)
#define BODY_H   (SCR_H - BODY_Y - 4)
#define LCOL_W   780   /* left column width */
#define RCOL_X   (LCOL_W + 6)
#define RCOL_W   (SCR_W - RCOL_X)
#define PAD        8
#define CHART_PTS  60  /* 60 seconds of history */

/* ── Widget handles ───────────────────────────────────────────────────────── */
static lv_obj_t        *cpu_chart;
static lv_chart_series_t *cpu_ser[4];

static lv_obj_t        *io_chart;
static lv_chart_series_t *io_tx;
static lv_chart_series_t *io_rx;

static lv_obj_t *mem_bars[4];
static lv_obj_t *mem_lbls[4];

/* ── Per-core random walk state ───────────────────────────────────────────── */
static uint32_t rng[6] = { 0xA1B2u, 0xC3D4u, 0xE5F6u, 0x789Au,
			    0xBCDEu, 0xF012u };

static uint32_t xnext(int i)
{
	rng[i] ^= rng[i] << 13;
	rng[i] ^= rng[i] >> 17;
	rng[i] ^= rng[i] << 5;
	return rng[i];
}

static int32_t cpu_val[4] = { 45, 30, 65, 20 };

static int32_t next_cpu(int core)
{
	int32_t noise = (int32_t)(xnext(core) % 21) - 10;

	cpu_val[core] = CLAMP(cpu_val[core] + noise, 5, 95);
	return cpu_val[core];
}

/* ── Build helpers ────────────────────────────────────────────────────────── */
static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h,
		        const char *title)
{
	lv_obj_t *p = lv_obj_create(parent);

	lv_obj_set_pos(p, x, y);
	lv_obj_set_size(p, w, h);
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
	lv_obj_set_pos(t, 0, 0);
	return p;
}

/* ── Screen construction ──────────────────────────────────────────────────── */
lv_obj_t *screen_charts_create(void)
{
	/* Reset widget handles */
	cpu_chart = NULL; io_chart = NULL;
	for (int i = 0; i < 4; i++) {
		cpu_ser[i] = NULL; mem_bars[i] = NULL; mem_lbls[i] = NULL;
	}
	io_tx = NULL; io_rx = NULL;

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

	lv_label_set_text(ht, "LIVE TELEMETRY");
	lv_obj_set_style_text_font(ht, &lv_font_montserrat_32, LV_PART_MAIN);
	lv_obj_set_style_text_color(ht, lv_color_white(), LV_PART_MAIN);
	lv_obj_align(ht, LV_ALIGN_LEFT_MID, 16, 0);

	lv_obj_t *sub = lv_label_create(hdr);

	lv_label_set_text(sub, "60s rolling window  \xe2\x80\x83  updated 1 Hz");
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

	/* ── Left: CPU load line chart ───────────────────────────────────── */
	lv_obj_t *lp = panel(scr, 0, BODY_Y, LCOL_W, BODY_H,
			      "CPU CORE LOAD (%)");

	cpu_chart = lv_chart_create(lp);
	lv_obj_set_pos(cpu_chart, 0, 22);
	lv_obj_set_size(cpu_chart, LCOL_W - 2 * PAD, BODY_H - 2 * PAD - 22);
	lv_chart_set_type(cpu_chart, LV_CHART_TYPE_LINE);
	lv_chart_set_update_mode(cpu_chart, LV_CHART_UPDATE_MODE_SHIFT);
	lv_chart_set_point_count(cpu_chart, CHART_PTS);
	lv_chart_set_range(cpu_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
	lv_obj_set_style_bg_color(cpu_chart, lv_color_hex(C_BG), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(cpu_chart, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_border_width(cpu_chart, 0, LV_PART_MAIN);
	lv_chart_set_div_line_count(cpu_chart, 5, 0);
	lv_obj_set_style_line_color(cpu_chart, lv_color_hex(C_BORDER),
				    LV_PART_MAIN);

	static const uint32_t core_cols[] = { C_ACCENT, C_GREEN, C_YELLOW, C_PURPLE };
	static const char    *core_names[] = { "C0", "C1", "C2", "C3" };

	for (int i = 0; i < 4; i++) {
		cpu_ser[i] = lv_chart_add_series(cpu_chart, lv_color_hex(core_cols[i]),
						  LV_CHART_AXIS_PRIMARY_Y);
		/* Pre-fill with initial values */
		for (int j = 0; j < CHART_PTS; j++) {
			lv_chart_set_next_value(cpu_chart, cpu_ser[i], next_cpu(i));
		}
	}

	/* Series legend */
	for (int i = 0; i < 4; i++) {
		lv_obj_t *leg = lv_label_create(lp);
		char buf[8];

		snprintk(buf, sizeof(buf), "\xe2\x97\x8f %s", core_names[i]);
		lv_label_set_text(leg, buf);
		lv_obj_set_style_text_font(leg, &lv_font_montserrat_14, LV_PART_MAIN);
		lv_obj_set_style_text_color(leg, lv_color_hex(core_cols[i]),
					    LV_PART_MAIN);
		lv_obj_set_pos(leg, i * 70, BODY_H - 2 * PAD - 16);
	}

	/* ── Right-top: TX/RX bar chart ──────────────────────────────────── */
	int rh = (BODY_H - 6) / 2;
	lv_obj_t *rtp = panel(scr, RCOL_X, BODY_Y, RCOL_W, rh,
			       "NETWORK I/O  KB/s");

	io_chart = lv_chart_create(rtp);
	lv_obj_set_pos(io_chart, 0, 22);
	lv_obj_set_size(io_chart, RCOL_W - 2 * PAD, rh - 2 * PAD - 22);
	lv_chart_set_type(io_chart, LV_CHART_TYPE_BAR);
	lv_chart_set_update_mode(io_chart, LV_CHART_UPDATE_MODE_SHIFT);
	lv_chart_set_point_count(io_chart, 12);
	lv_chart_set_range(io_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
	lv_obj_set_style_bg_color(io_chart, lv_color_hex(C_BG), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(io_chart, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_border_width(io_chart, 0, LV_PART_MAIN);

	io_tx = lv_chart_add_series(io_chart, lv_color_hex(C_ACCENT),
				     LV_CHART_AXIS_PRIMARY_Y);
	io_rx = lv_chart_add_series(io_chart, lv_color_hex(C_ORANGE),
				     LV_CHART_AXIS_PRIMARY_Y);

	/* TX/RX legend */
	{
		lv_obj_t *tl = lv_label_create(rtp);

		lv_label_set_text(tl, "\xe2\x96\xa0 TX    \xe2\x96\xa0 RX");
		lv_obj_set_style_text_font(tl, &lv_font_montserrat_14, LV_PART_MAIN);
		lv_obj_set_style_text_color(tl, lv_color_hex(C_WHITE), LV_PART_MAIN);
		lv_obj_align(tl, LV_ALIGN_TOP_RIGHT, 0, 0);
	}

	/* ── Right-bot: memory breakdown bars ───────────────────────────── */
	int rby = BODY_Y + rh + 6;
	int rbh = BODY_H - rh - 6;
	lv_obj_t *rbp = panel(scr, RCOL_X, rby, RCOL_W, rbh, "MEMORY");

	static const char    *mem_names[] = { "SRAM",  "Heap",  "Stack", "Free" };
	static const uint32_t mem_cols[]  = { C_ACCENT, C_GREEN, C_YELLOW, C_SUBTLE };
	const int bar_w = RCOL_W - 2 * PAD;

	for (int i = 0; i < 4; i++) {
		int my = 22 + i * 40;

		lv_obj_t *nl = lv_label_create(rbp);

		lv_label_set_text(nl, mem_names[i]);
		lv_obj_set_style_text_font(nl, &lv_font_montserrat_14, LV_PART_MAIN);
		lv_obj_set_style_text_color(nl, lv_color_hex(C_SUBTLE), LV_PART_MAIN);
		lv_obj_set_pos(nl, 0, my);

		mem_bars[i] = lv_bar_create(rbp);
		lv_bar_set_range(mem_bars[i], 0, 100);
		lv_bar_set_value(mem_bars[i], 50, LV_ANIM_OFF);
		lv_obj_set_size(mem_bars[i], bar_w - 60, 12);
		lv_obj_set_pos(mem_bars[i], 60, my + 2);
		lv_obj_set_style_bg_color(mem_bars[i], lv_color_hex(C_BORDER),
					  LV_PART_MAIN);
		lv_obj_set_style_bg_color(mem_bars[i], lv_color_hex(mem_cols[i]),
					  LV_PART_INDICATOR);

		mem_lbls[i] = lv_label_create(rbp);
		lv_label_set_text(mem_lbls[i], "--");
		lv_obj_set_style_text_font(mem_lbls[i], &lv_font_montserrat_14,
					   LV_PART_MAIN);
		lv_obj_set_style_text_color(mem_lbls[i], lv_color_hex(mem_cols[i]),
					    LV_PART_MAIN);
		lv_obj_set_pos(mem_lbls[i], bar_w - 56, my);
	}

	return scr;
}

/* ── 1 Hz update ──────────────────────────────────────────────────────────── */
void screen_charts_update(void)
{
	if (!cpu_chart) {
		return;
	}

	/* CPU line chart — shift and add new values */
	for (int i = 0; i < 4; i++) {
		lv_chart_set_next_value(cpu_chart, cpu_ser[i], next_cpu(i));
	}
	lv_chart_refresh(cpu_chart);

	/* I/O bar chart */
	int32_t tx = (int32_t)(xnext(4) % 800 + 50);
	int32_t rx = (int32_t)(xnext(5) % 600 + 30);

	/* Occasional burst */
	if ((xnext(4) & 0xFF) < 30) {
		tx = (int32_t)(xnext(4) % 400 + 600);
	}
	lv_chart_set_next_value(io_chart, io_tx, tx);
	lv_chart_set_next_value(io_chart, io_rx, rx);
	lv_chart_refresh(io_chart);

	/* Memory bars */
	lv_mem_monitor_t mon;

	lv_mem_monitor(&mon);

	/* SRAM: static image ~83% */
	lv_bar_set_value(mem_bars[0], 83, LV_ANIM_OFF);

	/* Heap: live */
	lv_bar_set_value(mem_bars[1], (int32_t)mon.used_pct, LV_ANIM_ON);

	/* Stack: rough estimate (workers + UI thread) */
	lv_bar_set_value(mem_bars[2], 45, LV_ANIM_OFF);

	/* Free SRAM (100 - 83) */
	lv_bar_set_value(mem_bars[3], 17, LV_ANIM_OFF);

	char buf[20];

	lv_label_set_text(mem_lbls[0], "~848KB");
	snprintk(buf, sizeof(buf), "%u%%", (unsigned)mon.used_pct);
	lv_label_set_text(mem_lbls[1], buf);
	lv_label_set_text(mem_lbls[2], "~45%");
	lv_label_set_text(mem_lbls[3], "~17%");
}
