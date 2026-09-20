/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SRC_LED_RING_H
#define SRC_LED_RING_H

/** Steady colour shown between events. */
typedef enum {
	LED_RING_READY,   /* yellow: ball at the start, waiting to begin */
	LED_RING_PLAYING, /* green: ball is rolling */
} led_ring_state_t;

/** Brief blink shown on top of the steady colour, then it resumes. */
typedef enum {
	LED_RING_FLASH_LOSS,  /* red: ball fell in a pit */
	LED_RING_FLASH_RESET, /* yellow: game reset by the button */
} led_ring_flash_t;

/**
 * Start the WS2812 LED ring thread (pinned to MAZE_CORE_LED).
 *
 * On start the ring blinks all its LEDs to signal that the game is starting.
 * The ring is on the mikroBUS MOSI pin (SPI1); if it is not connected the
 * game is unaffected and the calls below do nothing.
 */
void led_ring_start(void);

/** Set the steady colour. Cheap; safe to call every frame from any thread. */
void led_ring_set_state(led_ring_state_t state);

/** Request a brief blink. Returns immediately; safe from any thread. */
void led_ring_flash(led_ring_flash_t kind);

#endif /* SRC_LED_RING_H */
