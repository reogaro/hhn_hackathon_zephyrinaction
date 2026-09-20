/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — Game Rules (own thread, own core)
 */

#ifndef SRC_MAZE_GAME_H
#define SRC_MAZE_GAME_H

typedef enum {
	MAZE_BANNER_NONE = 0,
	MAZE_BANNER_HOLE,
	MAZE_BANNER_GOAL,
} maze_banner_t;

void maze_game_init(void);

/** Start the game thread (pinned to MAZE_CORE_GAME). */
void maze_game_start(void);

/** Banner the UI should currently show; safe to call from any thread. */
maze_banner_t maze_game_get_banner(void);

#endif /* SRC_MAZE_GAME_H */
