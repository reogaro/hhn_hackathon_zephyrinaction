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

/* Title screen white: three channels lit, so run it at half brightness to save current */
#define WHITE_PERCENT  50

#define RAINBOW_FRAME_MS 40  /* playing: time between rainbow steps */
#define RAINBOW_HUE_STEP 4   /* playing: how far the rainbow moves per step */
#define BLINK_MS         250 /* game over / victory: on and off time */

/* Print each ring update on the console, to debug the SPI transfer */
#define LED_RING_DEBUG 0

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[NUM_LEDS];

static K_THREAD_STACK_DEFINE(led_stack, LED_STACK);
static struct k_thread led_thread;

#define WHITE_LEVEL (BRIGHTNESS * WHITE_PERCENT / 100)

static const struct led_rgb OFF   = { .r = 0, .g = 0, .b = 0 };
static const struct led_rgb RED   = { .r = BRIGHTNESS, .g = 0, .b = 0 };
static const struct led_rgb WHITE = { .r = WHITE_LEVEL, .g = WHITE_LEVEL, .b = WHITE_LEVEL };

/* Colour wheel: 0-255 runs red -> green -> blue -> red, scaled by BRIGHTNESS. */
static struct led_rgb wheel(uint8_t pos)
{
	uint8_t r, g, b;

	if (pos < 85) {
		r = 255 - pos * 3;
		g = pos * 3;
		b = 0;
	} else if (pos < 170) {
		pos -= 85;
		r = 0;
		g = 255 - pos * 3;
		b = pos * 3;
	} else {
		pos -= 170;
		r = pos * 3;
		g = 0;
		b = 255 - pos * 3;
	}

	return (struct led_rgb){
		.r = r * BRIGHTNESS / 255,
		.g = g * BRIGHTNESS / 255,
		.b = b * BRIGHTNESS / 255,
	};
}

static void send(void)
{
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

static void show_all(struct led_rgb color)
{
	for (int i = 0; i < NUM_LEDS; i++) {
		pixels[i] = color;
	}
	send();
}

/* Rainbow spread over the whole ring, starting at colour @p hue */
static void show_rainbow(uint8_t hue)
{
	for (int i = 0; i < NUM_LEDS; i++) {
		pixels[i] = wheel(hue + i * 256 / NUM_LEDS);
	}
	send();
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

	int shown = -1;
	uint8_t hue = 0;
	unsigned int phase = 0;

	while (true) {
		int st = maze_game_get_state();

		if (st != shown) {
			shown = st;
			phase = 0;
			if (st == MAZE_STATE_START) {
				show_all(WHITE);
			}
		}

		switch (st) {
		case MAZE_STATE_PLAYING:
			show_rainbow(hue);
			hue += RAINBOW_HUE_STEP;
			wait_in_state(st, RAINBOW_FRAME_MS);
			break;
		case MAZE_STATE_GAME_OVER:
			show_all((phase++ & 1) ? OFF : RED);
			wait_in_state(st, BLINK_MS);
			break;
		case MAZE_STATE_VICTORY:
			if (phase++ & 1) {
				show_all(OFF);
			} else {
				show_rainbow(hue);
				hue += RAINBOW_HUE_STEP * 8;
			}
			wait_in_state(st, BLINK_MS);
			break;
		default: /* title screen: steady white, already shown */
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
