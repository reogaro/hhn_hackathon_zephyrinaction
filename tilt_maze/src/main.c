#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define WIN_W 1280
#define WIN_H 720
#define GAME_W 800
#define GAME_H 600
#define GAME_OFFSET_X ((WIN_W - GAME_W) / 2)
#define GAME_OFFSET_Y ((WIN_H - GAME_H) / 2)

#define BALL_RADIUS 14.0f
#define BEVEL 3
#define PLATFORM_X 50
#define PLATFORM_Y 50
#define PLATFORM_W 700
#define PLATFORM_H 500

typedef struct { float x, y; } Vec2;
typedef struct { float x, y, w, h; } Rect;
typedef struct { float x, y, r; } Hole;
typedef struct { uint8_t r, g, b; } Col;

#define MAX_PTS 24
typedef struct {
    Vec2 pts[MAX_PTS];
    int n;
    float half_w;
} WallPath;

static void path_add(WallPath *p, float x, float y)
{
    if (p->n < MAX_PTS) { p->pts[p->n].x = x; p->pts[p->n].y = y; p->n++; }
}

static Rect perimeter[] = {
    { PLATFORM_X, PLATFORM_Y, PLATFORM_W, 20 },
    { PLATFORM_X, PLATFORM_Y + PLATFORM_H - 20, PLATFORM_W, 20 },
    { PLATFORM_X, PLATFORM_Y, 20, PLATFORM_H },
    { PLATFORM_X + PLATFORM_W - 20, PLATFORM_Y, 20, PLATFORM_H },
};
#define N_PERIM (sizeof(perimeter) / sizeof(perimeter[0]))

#define N_MAZE_WALLS 10
static WallPath maze_walls[N_MAZE_WALLS];

static void build_maze(void)
{
    float hw = 10;
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

    maze_walls[i].half_w = 9;
    path_add(&maze_walls[i], 490, 70);
    path_add(&maze_walls[i], 490, 105);
    i++;

    maze_walls[i].half_w = 9;
    path_add(&maze_walls[i], 570, 152);
    path_add(&maze_walls[i], 570, 117);
    i++;

    maze_walls[i].half_w = 9;
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
#define N_PITS (sizeof(pits) / sizeof(pits[0]))

static Hole goal = { 685, 490, 16 };
static const float START_X = 100, START_Y = 110;

static lv_obj_t *canvas;
static uint8_t cbuf[LV_CANVAS_BUF_SIZE(WIN_W, WIN_H, 16, LV_DRAW_BUF_STRIDE_ALIGN)];

static void set_px(int x, int y, Col c)
{
    x += GAME_OFFSET_X;
    y += GAME_OFFSET_Y;
    if (x < 0 || y < 0 || x >= WIN_W || y >= WIN_H) return;
    lv_canvas_set_px(canvas, x, y, lv_color_make(c.r, c.g, c.b), LV_OPA_COVER);
}

static void fill_circle_lv(int cx, int cy, int radius, Col col)
{
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = (int)sqrtf((float)(radius * radius - dy * dy));
        for (int px = cx - dx; px <= cx + dx; px++) set_px(px, cy + dy, col);
    }
}

static void draw_line_lv(int x0, int y0, int x1, int y1, Col col)
{
    float len = sqrtf((float)((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)));
    int steps = (int)len + 1;
    for (int s = 0; s <= steps; s++) {
        float t = (float)s / steps;
        set_px((int)(x0 + t * (x1 - x0)), (int)(y0 + t * (y1 - y0)), col);
    }
}

static void fill_triangle_lv(Vec2 a, Vec2 b, Vec2 c, Col col)
{
    Vec2 v[3] = { a, b, c };
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2 - i; j++)
            if (v[j].y > v[j + 1].y) { Vec2 t = v[j]; v[j] = v[j + 1]; v[j + 1] = t; }
    int y0 = (int)v[0].y, y2 = (int)v[2].y;
    for (int y = y0; y <= y2; y++) {
        float fy = (float)y;
        float xs[2]; int n = 0;
        if (fy >= v[0].y && fy <= v[1].y && v[1].y != v[0].y)
            xs[n++] = v[0].x + (fy - v[0].y) / (v[1].y - v[0].y) * (v[1].x - v[0].x);
        if (fy >= v[1].y && fy <= v[2].y && v[2].y != v[1].y)
            xs[n++] = v[1].x + (fy - v[1].y) / (v[2].y - v[1].y) * (v[2].x - v[1].x);
        if (n < 2 && fy >= v[0].y && fy <= v[2].y && v[2].y != v[0].y)
            xs[n++] = v[0].x + (fy - v[0].y) / (v[2].y - v[0].y) * (v[2].x - v[0].x);
        if (n == 2) {
            int x0 = (int)fminf(xs[0], xs[1]), x1 = (int)fmaxf(xs[0], xs[1]);
            for (int px = x0; px <= x1; px++) set_px(px, y, col);
        }
    }
}

static Col lerp_col(Col a, Col b, float t)
{
    Col c;
    c.r = (uint8_t)(a.r + (b.r - a.r) * t);
    c.g = (uint8_t)(a.g + (b.g - a.g) * t);
    c.b = (uint8_t)(a.b + (b.b - a.b) * t);
    return c;
}

static void draw_vgradient_lv(Rect area, Col top, Col bottom)
{
    for (int row = 0; row < (int)area.h; row++) {
        float t = (float)row / ((float)area.h - 1);
        Col c = lerp_col(top, bottom, t);
        for (int px = (int)area.x; px < (int)(area.x + area.w); px++)
            set_px(px, (int)area.y + row, c);
    }
}

static void draw_rect_bevel_lv(Rect w)
{
    for (int yy = (int)w.y; yy < (int)(w.y + w.h); yy++)
        for (int xx = (int)w.x; xx < (int)(w.x + w.w); xx++)
            set_px(xx, yy, (Col){ 30, 40, 80 });
    for (int yy = (int)w.y; yy < (int)(w.y + w.h - BEVEL); yy++)
        for (int xx = (int)w.x; xx < (int)(w.x + w.w - BEVEL); xx++)
            set_px(xx, yy, (Col){ 140, 175, 225 });
    for (int yy = (int)(w.y + BEVEL); yy < (int)(w.y + w.h - BEVEL); yy++)
        for (int xx = (int)(w.x + BEVEL); xx < (int)(w.x + w.w - BEVEL); xx++)
            set_px(xx, yy, (Col){ 65, 95, 165 });
}

static void draw_path_lv(WallPath *p)
{
    for (int i = 0; i < p->n - 1; i++) {
        Vec2 A = p->pts[i], B = p->pts[i + 1];
        float len = sqrtf((B.x - A.x) * (B.x - A.x) + (B.y - A.y) * (B.y - A.y));
        int steps = (int)(len / 3.0f) + 1;
        for (int s = 0; s <= steps; s++) {
            float t = (float)s / steps;
            float px = A.x + t * (B.x - A.x), py = A.y + t * (B.y - A.y);
            fill_circle_lv((int)px, (int)py, (int)p->half_w, (Col){ 30, 40, 80 });
            fill_circle_lv((int)px, (int)py, (int)(p->half_w - BEVEL), (Col){ 65, 95, 165 });
            int hi_r = (int)(p->half_w - BEVEL - 2);
            if (hi_r < 1) hi_r = 1;
            fill_circle_lv((int)(px - 1), (int)(py - 1), hi_r, (Col){ 140, 175, 225 });
        }
    }
}

static void draw_pit_lv(Hole h)
{
    fill_circle_lv((int)h.x, (int)h.y, (int)h.r + 3, (Col){ 200, 60, 60 });
    fill_circle_lv((int)h.x, (int)h.y, (int)h.r, (Col){ 15, 15, 18 });
}

static void draw_kite_goal_lv(Hole h)
{
    fill_circle_lv((int)h.x, (int)h.y, (int)h.r + 6, (Col){ 60, 200, 100 });

    float r = h.r;
    Vec2 T = { h.x + 0.00f * r, h.y - 1.60f * r };
    Vec2 R = { h.x + 1.10f * r, h.y - 0.35f * r };
    Vec2 L = { h.x - 1.00f * r, h.y - 0.15f * r };
    Vec2 B = { h.x + 0.15f * r, h.y + 1.75f * r };
    Vec2 C = { h.x + 0.05f * r, h.y - 0.15f * r };

    Col cyan = { 195, 230, 242 }, blue = { 55, 140, 210 };
    Col midblu = { 80, 95, 185 }, purple = { 90, 45, 150 };

    fill_triangle_lv(T, C, L, cyan);
    fill_triangle_lv(T, R, C, blue);
    fill_triangle_lv(C, R, B, midblu);
    fill_triangle_lv(L, C, B, purple);

    Col outline = { 15, 20, 40 };
    draw_line_lv((int)T.x, (int)T.y, (int)R.x, (int)R.y, outline);
    draw_line_lv((int)R.x, (int)R.y, (int)B.x, (int)B.y, outline);
    draw_line_lv((int)B.x, (int)B.y, (int)L.x, (int)L.y, outline);
    draw_line_lv((int)L.x, (int)L.y, (int)T.x, (int)T.y, outline);
    draw_line_lv((int)T.x, (int)T.y, (int)C.x, (int)C.y, outline);
    draw_line_lv((int)C.x, (int)C.y, (int)B.x, (int)B.y, outline);
    draw_line_lv((int)L.x, (int)L.y, (int)C.x, (int)C.y, outline);
    draw_line_lv((int)C.x, (int)C.y, (int)R.x, (int)R.y, outline);
}

static void draw_static_maze(void)
{
    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);
    Col ztop = { 33, 150, 216 }, zbot = { 90, 45, 150 };
    Rect platform = { PLATFORM_X, PLATFORM_Y, PLATFORM_W, PLATFORM_H };
    draw_vgradient_lv(platform, ztop, zbot);
    for (int i = 0; i < N_PERIM; i++) draw_rect_bevel_lv(perimeter[i]);
    for (int i = 0; i < N_MAZE_WALLS; i++) draw_path_lv(&maze_walls[i]);
    for (int i = 0; i < N_PITS; i++) draw_pit_lv(pits[i]);
    draw_kite_goal_lv(goal);
}

#define BMA456_CHIP_ID_REG 0x00
#define BMA456_CHIP_ID_VAL 0x16
#define BMA456_ACC_X_LSB   0x12

static const struct device *i2c_dev;
static uint8_t bma_addr;

struct tilt_sample { float x, y; };
K_MSGQ_DEFINE(tilt_msgq, sizeof(struct tilt_sample), 4, 4);

static float filtered_x = 0, filtered_y = 0;
const float filter_alpha = 0.15f;

static bool bma456_find_addr(void)
{
    uint8_t candidates[] = { 0x18, 0x19 };
    uint8_t id;
    for (int i = 0; i < 2; i++) {
        if (i2c_reg_read_byte(i2c_dev, candidates[i], BMA456_CHIP_ID_REG, &id) == 0
            && id == BMA456_CHIP_ID_VAL) {
            bma_addr = candidates[i];
            return true;
        }
    }
    return false;
}

static void sampling_timer_handler(struct k_timer *t)
{
    uint8_t buf[6];
    if (i2c_burst_read(i2c_dev, bma_addr, BMA456_ACC_X_LSB, buf, 6) == 0) {
        int16_t rx = (int16_t)((buf[1] << 8) | buf[0]);
        int16_t ry = (int16_t)((buf[3] << 8) | buf[2]);
        float x = rx / 16384.0f, y = ry / 16384.0f;
        filtered_x += filter_alpha * (x - filtered_x);
        filtered_y += filter_alpha * (y - filtered_y);
        struct tilt_sample s = { .x = filtered_x, .y = filtered_y };
        k_msgq_put(&tilt_msgq, &s, K_NO_WAIT);
    }
}
K_TIMER_DEFINE(sampling_timer, sampling_timer_handler, NULL);

static void resolve_bounce(float dx, float dy, float dist, float *vx, float *vy)
{
    float nx = dx / dist, ny = dy / dist;
    float vn = (*vx) * nx + (*vy) * ny;
    if (vn < 0) {
        const float restitution = 0.55f;
        *vx -= (1.0f + restitution) * vn * nx;
        *vy -= (1.0f + restitution) * vn * ny;
    }
}

static void collide_path(WallPath *p, float *x, float *y, float *vx, float *vy)
{
    for (int i = 0; i < p->n - 1; i++) {
        Vec2 A = p->pts[i], B = p->pts[i + 1];
        float abx = B.x - A.x, aby = B.y - A.y;
        float ab_len2 = abx * abx + aby * aby;
        float t = ab_len2 > 0 ? ((*x - A.x) * abx + (*y - A.y) * aby) / ab_len2 : 0;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        float cx = A.x + t * abx, cy = A.y + t * aby;
        float dx = *x - cx, dy = *y - cy;
        float dist2 = dx * dx + dy * dy;
        float min_dist = BALL_RADIUS + p->half_w;
        if (dist2 < min_dist * min_dist) {
            float dist = dist2 > 0 ? sqrtf(dist2) : 0.01f;
            float push = min_dist - dist;
            *x += (dx / dist) * push;
            *y += (dy / dist) * push;
            resolve_bounce(dx, dy, dist, vx, vy);
        }
    }
}

int main(void)
{
    build_maze();

    i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
    if (!device_is_ready(i2c_dev)) { printk("I2C not ready\n"); return 0; }
    if (!bma456_find_addr()) { printk("BMA456 not found\n"); return 0; }
    printk("BMA456 found at 0x%02X\n", bma_addr);

    const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    display_blanking_off(display);

    canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(canvas, cbuf, WIN_W, WIN_H, LV_COLOR_FORMAT_RGB565);
    draw_static_maze();

    lv_obj_t *ball = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ball, BALL_RADIUS * 2, BALL_RADIUS * 2);
    lv_obj_set_style_radius(ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ball, lv_color_make(240, 200, 60), 0);

    float x = START_X, y = START_Y, vx = 0, vy = 0;
    const float accel_scale = 900.0f, rolling_friction = 220.0f, max_speed = 500.0f;

    k_timer_start(&sampling_timer, K_MSEC(16), K_MSEC(16));
    uint32_t last = k_uptime_get_32();
    struct tilt_sample tilt = { 0, 0 };

    while (1) {
        struct tilt_sample latest;
        while (k_msgq_get(&tilt_msgq, &latest, K_NO_WAIT) == 0) tilt = latest;

        uint32_t now = k_uptime_get_32();
        float dt = (now - last) / 1000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;

        vx += tilt.x * accel_scale * dt;
        vy += tilt.y * accel_scale * dt;

        float speed = sqrtf(vx * vx + vy * vy);
        if (speed > 0) {
            float dec = fminf(rolling_friction * dt, speed);
            vx -= (vx / speed) * dec;
            vy -= (vy / speed) * dec;
        }
        speed = sqrtf(vx * vx + vy * vy);
        if (speed > max_speed) {
            vx = (vx / speed) * max_speed;
            vy = (vy / speed) * max_speed;
        }

        x += vx * dt;
        y += vy * dt;

        for (int i = 0; i < N_PERIM; i++) {
            Rect w = perimeter[i];
            float cx = fmaxf(w.x, fminf(x, w.x + w.w));
            float cy = fmaxf(w.y, fminf(y, w.y + w.h));
            float dx = x - cx, dy = y - cy;
            float dist2 = dx * dx + dy * dy;
            if (dist2 < BALL_RADIUS * BALL_RADIUS) {
                float dist = dist2 > 0 ? sqrtf(dist2) : 0.01f;
                float push = BALL_RADIUS - dist;
                x += (dx / dist) * push;
                y += (dy / dist) * push;
                resolve_bounce(dx, dy, dist, &vx, &vy);
            }
        }
        for (int i = 0; i < N_MAZE_WALLS; i++) {
            collide_path(&maze_walls[i], &x, &y, &vx, &vy);
        }

        for (int i = 0; i < N_PITS; i++) {
            float dx = x - pits[i].x, dy = y - pits[i].y;
            if (sqrtf(dx * dx + dy * dy) < pits[i].r) {
                printk("Fell in a hole - resetting\n");
                x = START_X; y = START_Y; vx = 0; vy = 0;
            }
        }
        {
            float dx = x - goal.x, dy = y - goal.y;
            if (sqrtf(dx * dx + dy * dy) < goal.r) {
                printk("Reached the goal!\n");
                x = START_X; y = START_Y; vx = 0; vy = 0;
            }
        }

        lv_obj_set_pos(ball, (int)(x - BALL_RADIUS) + GAME_OFFSET_X,
                             (int)(y - BALL_RADIUS) + GAME_OFFSET_Y);
        lv_task_handler();
        k_msleep(10);
    }
}
