/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — Game Rules (own thread, own core)
 */

#ifndef SRC_MAZE_GAME_H
#define SRC_MAZE_GAME_H

#include <stdbool.h>
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

#endif /* SRC_MAZE_GAME_H */
