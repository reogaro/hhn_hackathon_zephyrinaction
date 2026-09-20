/*
 * mikroBUS push button demo for the PIC64GX Curiosity Kit (Zephyr).
 *
 * The button is on the mikroBUS INT pin (gpio1 pin 0, see app.overlay).
 * Only long presses are reported; short presses are ignored.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define POLL_MS          10
#define DEBOUNCE_SAMPLES 3     /* equal reads in a row before a change counts */
#define LONG_PRESS_MS    1000  /* shorter presses are ignored */

static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(mikrobus_btn), gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
	if (!gpio_is_ready_dt(&btn) || !gpio_is_ready_dt(&led)) {
		printk("GPIO not ready\n");
		return 0;
	}

	gpio_pin_configure_dt(&btn, GPIO_INPUT);
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	int pressed = gpio_pin_get_dt(&btn);
	int differing = 0;
	int held_ms = 0;
	bool long_press = false;

	printk("mikroBUS button ready (long press = %d ms)\n", LONG_PRESS_MS);

	while (1) {
		int level = gpio_pin_get_dt(&btn);

		if (level == pressed) {
			differing = 0;
		} else if (++differing >= DEBOUNCE_SAMPLES) {
			pressed = level;
			differing = 0;
			held_ms = 0;
			if (!pressed && long_press) {
				printk("Long press released\n");
				gpio_pin_set_dt(&led, 0);
			}
			long_press = false;
		}

		if (pressed && !long_press) {
			held_ms += POLL_MS;
			if (held_ms >= LONG_PRESS_MS) {
				long_press = true;
				printk("Long press detected\n");
				gpio_pin_set_dt(&led, 1);
			}
		}
		k_msleep(POLL_MS);
	}
}
