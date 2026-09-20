/* SPDX-License-Identifier: Apache-2.0 */
/*
 * WS2812 (NeoPixel) LED ring for Tilt Maze.
 *
 * Uses Zephyr's WS2812-over-SPI led_strip driver on SPI1 (see app.overlay).
 * A thread owns the ring so that the slow polled SPI transfer never blocks
 * the game, physics or UI threads. Other threads only set an atomic state or
 * request a flash; the thread does the drawing.
 */

#include "led_ring.h"
#include "maze_cores.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#define STRIP_NODE DT_ALIAS(led_strip)
#define NUM_LEDS   DT_PROP(STRIP_NODE, chain_length)

#define LED_STACK  2048
#define LED_PRIO   5
#define POLL_MS    20

/* Keep dim: each LED draws up to ~60 mA at full white */
#define BRIGHTNESS 32

#define START_BLINKS   3
#define START_BLINK_MS 250

#define LOSS_BLINKS    3
#define LOSS_BLINK_MS  120
#define RESET_BLINKS   2
#define RESET_BLINK_MS 100

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[NUM_LEDS];

static K_THREAD_STACK_DEFINE(led_stack, LED_STACK);
static struct k_thread led_thread;

static const struct led_rgb OFF    = { .r = 0, .g = 0, .b = 0 };
static const struct led_rgb BLUE   = { .r = 0, .g = 0, .b = BRIGHTNESS };
static const struct led_rgb GREEN  = { .r = 0, .g = BRIGHTNESS, .b = 0 };
static const struct led_rgb RED    = { .r = BRIGHTNESS, .g = 0, .b = 0 };
static const struct led_rgb YELLOW = { .r = BRIGHTNESS, .g = BRIGHTNESS * 3 / 4, .b = 0 };

#define NO_FLASH (-1)

static atomic_t state = LED_RING_READY;
static atomic_t flash_req = NO_FLASH;

void led_ring_set_state(led_ring_state_t s)
{
	atomic_set(&state, s);
}

void led_ring_flash(led_ring_flash_t kind)
{
	atomic_set(&flash_req, kind);
}

static void show_all(struct led_rgb color)
{
	for (int i = 0; i < NUM_LEDS; i++) {
		pixels[i] = color;
	}

	printk("[dbg] led send t=%u\n", k_uptime_get_32()); /* TEMP-DEBUG */
	int err = led_strip_update_rgb(strip, pixels, NUM_LEDS);
	printk("[dbg] led done err=%d t=%u\n", err, k_uptime_get_32()); /* TEMP-DEBUG */

	if (err) {
		printk("[led_ring] update failed: %d\n", err);
	}
}

static void blink_all(struct led_rgb color, int times, int period_ms)
{
	for (int i = 0; i < times; i++) {
		show_all(color);
		k_msleep(period_ms);
		show_all(OFF);
		k_msleep(period_ms);
	}
}

static struct led_rgb state_color(led_ring_state_t s)
{
	return s == LED_RING_PLAYING ? GREEN : YELLOW;
}

static void led_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	if (!device_is_ready(strip)) {
		printk("[led_ring] LED strip not ready\n");
		return;
	}

	/* All LEDs blink together to show the game is starting */
	blink_all(BLUE, START_BLINKS, START_BLINK_MS);

	atomic_val_t shown = -1;

	while (true) {
		switch (atomic_set(&flash_req, NO_FLASH)) {
		case LED_RING_FLASH_LOSS:
			blink_all(RED, LOSS_BLINKS, LOSS_BLINK_MS);
			shown = -1; /* the ring is off now: redraw the steady colour */
			break;
		case LED_RING_FLASH_RESET:
			blink_all(YELLOW, RESET_BLINKS, RESET_BLINK_MS);
			shown = -1;
			break;
		default:
			break;
		}

		atomic_val_t now = atomic_get(&state);

		if (now != shown) {
			show_all(state_color(now));
			shown = now;
		}
		k_msleep(POLL_MS);
	}
}

void led_ring_start(void)
{
	maze_thread_spawn(&led_thread, led_stack, K_THREAD_STACK_SIZEOF(led_stack),
			  led_fn, LED_PRIO, MAZE_CORE_LED, "led_ring");
}
