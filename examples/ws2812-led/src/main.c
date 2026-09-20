/*
 * WS2812 (NeoPixel) LED strip demo for the PIC64GX Curiosity Kit (Zephyr).
 *
 * DIN is on the mikroBUS MOSI pin (SPI1, see app.overlay). Runs a rainbow
 * that scrolls around the ring. The LED count is chain-length in app.overlay.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/sys/printk.h>

#define STRIP_NODE  DT_ALIAS(led_strip)
#define NUM_LEDS    DT_PROP(STRIP_NODE, chain_length)
#define BRIGHTNESS  32   /* 0-255; each LED draws up to ~60 mA at full white */
#define FRAME_MS    30
#define HUE_STEP    4

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[NUM_LEDS];

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

int main(void)
{
	if (!device_is_ready(strip)) {
		printk("LED strip not ready\n");
		return 0;
	}

	printk("WS2812 strip ready (%d LEDs)\n", NUM_LEDS);

	uint8_t hue = 0;

	while (1) {
		for (int i = 0; i < NUM_LEDS; i++) {
			pixels[i] = wheel(hue + i * 256 / NUM_LEDS);
		}

		int err = led_strip_update_rgb(strip, pixels, NUM_LEDS);

		if (err) {
			printk("led_strip_update_rgb failed: %d\n", err);
		}

		hue += HUE_STEP;
		k_msleep(FRAME_MS);
	}
}
