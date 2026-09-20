/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — Declarative Maze Layout & Geometries
 */

#include "maze_map.h"
#include <zephyr/sys/util.h>

static const maze_rect_t perimeter_walls[] = {
	{ MAZE_PLATFORM_X, MAZE_PLATFORM_Y, MAZE_PLATFORM_W, 20 },
	{ MAZE_PLATFORM_X, MAZE_PLATFORM_Y + MAZE_PLATFORM_H - 20, MAZE_PLATFORM_W, 20 },
	{ MAZE_PLATFORM_X, MAZE_PLATFORM_Y, 20, MAZE_PLATFORM_H },
	{ MAZE_PLATFORM_X + MAZE_PLATFORM_W - 20, MAZE_PLATFORM_Y, 20, MAZE_PLATFORM_H },
};

#define N_MAZE_WALLS 10
static maze_wall_path_t internal_walls[N_MAZE_WALLS];

static const maze_hole_t trap_pits[] = {
	{ 214, 91,  17 },
	{ 337, 125, 17 },
	{ 359, 215, 17 },
	{ 110, 253, 17 },
	{ 210, 302, 17 },
	{ 535, 264, 17 },
	{ 643, 273, 17 },
	{ 702, 348, 17 },
	{ 121, 440, 17 },
	{ 263, 377, 17 },
};

static const maze_hole_t exit_goal = { 685, 490, 20 };

static void path_add(maze_wall_path_t *p, float x, float y)
{
	if (p->n < MAZE_MAX_WALL_PTS) {
		p->pts[p->n].x = x;
		p->pts[p->n].y = y;
		p->n++;
	}
}

void maze_map_init(void)
{
	float hw = 10.0f;
	int i = 0;

	internal_walls[i].n = 0;
	internal_walls[i].half_w = hw;
	path_add(&internal_walls[i], 70, 162);
	path_add(&internal_walls[i], 640, 162);
	i++;

	internal_walls[i].n = 0;
	internal_walls[i].half_w = hw;
	path_add(&internal_walls[i], 730, 220);
	path_add(&internal_walls[i], 480, 220);
	path_add(&internal_walls[i], 480, 270);
	path_add(&internal_walls[i], 250, 270);
	path_add(&internal_walls[i], 250, 220);
	path_add(&internal_walls[i], 170, 220);
	i++;

	internal_walls[i].n = 0;
	internal_walls[i].half_w = hw;
	path_add(&internal_walls[i], 70, 346);
	path_add(&internal_walls[i], 640, 346);
	i++;

	internal_walls[i].n = 0;
	internal_walls[i].half_w = hw;
	path_add(&internal_walls[i], 190, 438);
	path_add(&internal_walls[i], 730, 438);
	i++;

	internal_walls[i].n = 0;
	internal_walls[i].half_w = hw;
	path_add(&internal_walls[i], 430, 132);
	path_add(&internal_walls[i], 430, 162);
	i++;

	internal_walls[i].n = 0;
	internal_walls[i].half_w = hw;
	path_add(&internal_walls[i], 300, 438);
	path_add(&internal_walls[i], 300, 478);
	i++;

	internal_walls[i].n = 0;
	internal_walls[i].half_w = hw;
	path_add(&internal_walls[i], 550, 438);
	path_add(&internal_walls[i], 550, 478);
	i++;

	internal_walls[i].n = 0;
	internal_walls[i].half_w = 9.0f;
	path_add(&internal_walls[i], 490, 70);
	path_add(&internal_walls[i], 490, 105);
	i++;

	internal_walls[i].n = 0;
	internal_walls[i].half_w = 9.0f;
	path_add(&internal_walls[i], 570, 152);
	path_add(&internal_walls[i], 570, 117);
	i++;

	internal_walls[i].n = 0;
	internal_walls[i].half_w = 9.0f;
	path_add(&internal_walls[i], 650, 70);
	path_add(&internal_walls[i], 650, 105);
	i++;
}

const maze_rect_t *maze_map_get_perimeter(size_t *count)
{
	if (count) {
		*count = ARRAY_SIZE(perimeter_walls);
	}
	return perimeter_walls;
}

const maze_wall_path_t *maze_map_get_walls(size_t *count)
{
	if (count) {
		*count = ARRAY_SIZE(internal_walls);
	}
	return internal_walls;
}

const maze_hole_t *maze_map_get_pits(size_t *count)
{
	if (count) {
		*count = ARRAY_SIZE(trap_pits);
	}
	return trap_pits;
}

const maze_hole_t *maze_map_get_goal(void)
{
	return &exit_goal;
}
