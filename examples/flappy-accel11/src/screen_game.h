/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SRC_SCREEN_GAME_H
#define SRC_SCREEN_GAME_H

#include <lvgl.h>

/** Build the game screen and return it (does not load it). */
lv_obj_t *screen_game_create(void);

/** 1 Hz placeholder — game is self-updating via its own lv_timer_t. */
void screen_game_update(void);

/**
 * Returns true once: when the player has finished GAME_OVER (3 s hold or
 * SW1 press).  Resets the flag on return so it only fires once per session.
 * demo_manager polls this to know when to fade back to the HUD.
 */
bool screen_game_is_done(void);

#endif /* SRC_SCREEN_GAME_H */
