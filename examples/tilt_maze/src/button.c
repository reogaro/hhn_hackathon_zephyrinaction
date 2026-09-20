/* SPDX-License-Identifier: Apache-2.0 */
/*
 * mikroBUS INT push button for Tilt Maze (gpio1 pin 0, see app.overlay).
 *
 * A polling thread debounces the button and invokes the callback on press.
 */

#include "button.h"
#include "maze_cores.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define BUTTON_STACK         2048
#define BUTTON_PRIO          5
#define POLL_MS              10
#define DEBOUNCE_SAMPLES     3     /* equal reads in a row before a change counts */
#define BUTTON_LONG_PRESS_MS 1000  /* held >= 1s is long press */

static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(mikrobus_btn), gpios);

static K_THREAD_STACK_DEFINE(button_stack, BUTTON_STACK);
static struct k_thread button_thread;

static void button_fn(void *p1, void *p2, void *p3)
{
	void (*on_event)(button_event_t event) = p1;

	ARG_UNUSED(p2); ARG_UNUSED(p3);

	if (!gpio_is_ready_dt(&btn) || gpio_pin_configure_dt(&btn, GPIO_INPUT) != 0) {
		printk("[button] GPIO not ready\n");
		return;
	}

	int pressed = gpio_pin_get_dt(&btn);
	int differing = 0;
	int held_ms = 0;
	bool long_pressed = false;

	printk("[button] mikroBUS button ready (long press = %d ms)\n", BUTTON_LONG_PRESS_MS);

	while (true) {
		int level = gpio_pin_get_dt(&btn);

		if (level == pressed) {
			differing = 0;
		} else if (++differing >= DEBOUNCE_SAMPLES) {
			pressed = level;
			differing = 0;

			if (!pressed) {
				/* Button released */
				if (!long_pressed && held_ms > 0) {
					printk("[button] Short press\n");
					if (on_event) {
						on_event(BUTTON_EVENT_SHORT);
					}
				}
				held_ms = 0;
				long_pressed = false;
			} else {
				/* Button pressed down */
				held_ms = 0;
				long_pressed = false;
			}
		}

		if (pressed && !long_pressed) {
			held_ms += POLL_MS;
			if (held_ms >= BUTTON_LONG_PRESS_MS) {
				long_pressed = true;
				printk("[button] Long press\n");
				if (on_event) {
					on_event(BUTTON_EVENT_LONG);
				}
			}
		}

		k_msleep(POLL_MS);
	}
}

void button_start(void (*on_event)(button_event_t event))
{
	k_tid_t tid = k_thread_create(&button_thread, button_stack,
				      K_THREAD_STACK_SIZEOF(button_stack), button_fn,
				      on_event, NULL, NULL, BUTTON_PRIO, 0, K_FOREVER);

	k_thread_name_set(tid, "button");
	k_thread_cpu_mask_clear(tid);
	k_thread_cpu_mask_enable(tid, MAZE_CORE_BUTTON);
	k_thread_start(tid);
}
