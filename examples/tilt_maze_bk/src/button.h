/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SRC_BUTTON_H
#define SRC_BUTTON_H

/**
 * Start the mikroBUS INT push-button thread (pinned to MAZE_CORE_BUTTON).
 *
 * The button is polled and debounced. Once it has been held for
 * BUTTON_LONG_PRESS_MS, @p on_long_press is called once (from the button
 * thread); shorter presses are ignored. The callback fires again only after
 * the button has been released and held again.
 */
void button_start(void (*on_long_press)(void));

#endif /* SRC_BUTTON_H */
