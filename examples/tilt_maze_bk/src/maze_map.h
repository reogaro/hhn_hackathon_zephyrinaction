/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — Geometry & Map Definitions
 */

#ifndef SRC_MAZE_MAP_H
#define SRC_MAZE_MAP_H

#include <stddef.h>
#include <stdint.h>

#define MAZE_WIN_W         1280
#define MAZE_WIN_H          720
#define MAZE_GAME_W         800
#define MAZE_GAME_H         600
#define MAZE_OFFSET_X       ((MAZE_WIN_W - MAZE_GAME_W) / 2)
#define MAZE_OFFSET_Y       ((MAZE_WIN_H - MAZE_GAME_H) / 2)

#define MAZE_PLATFORM_X     50
#define MAZE_PLATFORM_Y     50
#define MAZE_PLATFORM_W    700
#define MAZE_PLATFORM_H    500

#define MAZE_BALL_RADIUS    14.0f
#define MAZE_START_X       100.0f
#define MAZE_START_Y       110.0f

#define MAZE_MAX_WALL_PTS   24

typedef struct {
	float x, y;
} maze_vec2_t;

typedef struct {
	float x, y, w, h;
} maze_rect_t;

typedef struct {
	float x, y, r;
} maze_hole_t;

typedef struct {
	maze_vec2_t pts[MAZE_MAX_WALL_PTS];
	int n;
	float half_w;
} maze_wall_path_t;

void maze_map_init(void);

const maze_rect_t *maze_map_get_perimeter(size_t *count);
const maze_wall_path_t *maze_map_get_walls(size_t *count);
const maze_hole_t *maze_map_get_pits(size_t *count);
const maze_hole_t *maze_map_get_goal(void);

#endif /* SRC_MAZE_MAP_H */
