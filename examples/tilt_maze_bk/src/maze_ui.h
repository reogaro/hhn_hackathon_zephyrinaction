/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — LVGL Presentation & Visual Widgets (own thread, own core)
 */

#ifndef SRC_MAZE_UI_H
#define SRC_MAZE_UI_H

/**
 * Start the UI thread (pinned to MAZE_CORE_UI). It builds the scene, then runs
 * the LVGL dispatch loop and mirrors the physics/game state onto the widgets.
 */
void maze_ui_start(void);

#endif /* SRC_MAZE_UI_H */
