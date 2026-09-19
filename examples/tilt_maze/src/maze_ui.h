/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — LVGL Presentation & Visual Widgets
 */

#ifndef SRC_MAZE_UI_H
#define SRC_MAZE_UI_H

void maze_ui_init(void);
void maze_ui_set_ball_pos(float x, float y);
void maze_ui_show_banner(const char *msg);
void maze_ui_hide_banner(void);

#endif /* SRC_MAZE_UI_H */
