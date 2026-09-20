/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — Game Rules (own thread, own core)
 */

#ifndef SRC_MAZE_GAME_H
#define SRC_MAZE_GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "button.h"

typedef enum {
	MAZE_STATE_START = 0,
	MAZE_STATE_PLAYING,
	MAZE_STATE_GAME_OVER,
	MAZE_STATE_VICTORY,
} maze_state_t;

void maze_game_init(void);

/** Start the game thread (pinned to MAZE_CORE_GAME). */
void maze_game_start(void);

/** Current game state; safe to call from any thread. */
maze_state_t maze_game_get_state(void);

/** Button event handler; safe to call from any thread. */
void maze_game_on_button(button_event_t event);

/** Current run elapsed time in milliseconds; safe to call from any thread. */
uint32_t maze_game_get_time_ms(void);

/** Best escape time in milliseconds (0 if none yet); safe to call from any thread. */
uint32_t maze_game_get_best_time_ms(void);

/** Whether the latest victory was a new best record. */
bool maze_game_is_new_best(void);

#endif /* SRC_MAZE_GAME_H */
