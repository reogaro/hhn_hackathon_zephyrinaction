/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SRC_BUTTON_H
#define SRC_BUTTON_H

typedef enum {
	BUTTON_EVENT_SHORT,
	BUTTON_EVENT_LONG,
} button_event_t;

/**
 * Start the mikroBUS INT push-button thread (pinned to MAZE_CORE_BUTTON).
 *
 * The button is polled and debounced. Short presses fire BUTTON_EVENT_SHORT
 * upon release. Long presses fire BUTTON_EVENT_LONG when held for >= 1 second.
 */
void button_start(void (*on_event)(button_event_t event));

#endif /* SRC_BUTTON_H */
