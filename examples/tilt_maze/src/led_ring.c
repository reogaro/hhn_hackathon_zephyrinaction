/* SPDX-License-Identifier: Apache-2.0 */
/*
 * WS2812 (NeoPixel) LED ring for Tilt Maze.
 *
 * Uses Zephyr's WS2812-over-SPI led_strip driver on SPI1 (see app.overlay).
 * A thread owns the ring so that the slow polled SPI transfer never blocks
 * the game, physics or UI threads. It only reads the game state; it never
 * changes it.
 */

#include "led_ring.h"
#include "maze_game.h"
#include "maze_cores.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/sys/printk.h>

#define STRIP_NODE DT_ALIAS(led_strip)
#define NUM_LEDS   DT_PROP(STRIP_NODE, chain_length)

#define LED_STACK  2048
#define LED_PRIO   5
#define POLL_MS    20

/* Keep dim: each LED draws up to ~60 mA at full white */
#define BRIGHTNESS 32

#define CYCLE_MS       400  /* title screen: time per colour */
#define BLINK_MS       250  /* game over / victory: on and off time */

/* Print each ring update on the console, to debug the SPI transfer */
#define LED_RING_DEBUG 0

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[NUM_LEDS];

static K_THREAD_STACK_DEFINE(led_stack, LED_STACK);
static struct k_thread led_thread;

static const struct led_rgb OFF    = { .r = 0, .g = 0, .b = 0 };
static const struct led_rgb BLUE   = { .r = 0, .g = 0, .b = BRIGHTNESS };
static const struct led_rgb GREEN  = { .r = 0, .g = BRIGHTNESS, .b = 0 };
static const struct led_rgb RED    = { .r = BRIGHTNESS, .g = 0, .b = 0 };
static const struct led_rgb YELLOW = { .r = BRIGHTNESS, .g = BRIGHTNESS * 3 / 4, .b = 0 };

static void show_all(struct led_rgb color)
{
	for (int i = 0; i < NUM_LEDS; i++) {
		pixels[i] = color;
	}

#if LED_RING_DEBUG
	printk("[led_ring] send t=%u\n", k_uptime_get_32());
#endif
	int err = led_strip_update_rgb(strip, pixels, NUM_LEDS);

#if LED_RING_DEBUG
	printk("[led_ring] done err=%d t=%u\n", err, k_uptime_get_32());
#endif
	if (err) {
		printk("[led_ring] update failed: %d\n", err);
	}
}

/* Sleep up to @p ms, but return early once the game state is no longer @p st */
static void wait_in_state(int st, int ms)
{
	for (int t = 0; t < ms && maze_game_get_state() == st; t += POLL_MS) {
		k_msleep(POLL_MS);
	}
}

static void led_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	if (!device_is_ready(strip)) {
		printk("[led_ring] LED strip not ready\n");
		return;
	}

	const struct led_rgb *cycle[] = { &BLUE, &YELLOW, &GREEN, &RED };
	int shown = -1;
	int phase = 0;

	while (true) {
		int st = maze_game_get_state();

		if (st != shown) {
			shown = st;
			phase = 0;
			if (st == MAZE_STATE_PLAYING) {
				show_all(GREEN);
			}
		}

		switch (st) {
		case MAZE_STATE_START:
			show_all(*cycle[phase++ % ARRAY_SIZE(cycle)]);
			wait_in_state(st, CYCLE_MS);
			break;
		case MAZE_STATE_GAME_OVER:
			show_all((phase++ & 1) ? OFF : RED);
			wait_in_state(st, BLINK_MS);
			break;
		case MAZE_STATE_VICTORY:
			show_all((phase++ & 1) ? OFF : GREEN);
			wait_in_state(st, BLINK_MS);
			break;
		default:
			k_msleep(POLL_MS);
			break;
		}
	}
}

void led_ring_start(void)
{
	maze_thread_spawn(&led_thread, led_stack, K_THREAD_STACK_SIZEOF(led_stack),
			  led_fn, LED_PRIO, MAZE_CORE_LED, "led_ring");
}
