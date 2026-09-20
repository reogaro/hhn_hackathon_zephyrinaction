/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — Game Rules
 *
 * Watches the ball position published by the physics thread, applies the pit
 * and goal rules and tells the UI which banner to show. It never touches
 * LVGL or Chipmunk directly.
 */

#include "maze_game.h"
#include "maze_map.h"
#include "maze_physics.h"
#include "maze_cores.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <math.h>

#if defined(CONFIG_SHELL)
#include <zephyr/shell/shell.h>
#endif

#define GAME_STACK          4096
#define GAME_PRIO           5
#define GAME_PERIOD_MS      16
#define DEBOUNCE_LOCKOUT_MS 400

static K_THREAD_STACK_DEFINE(game_stack, GAME_STACK);
static struct k_thread game_thread;

static atomic_t current_state = ATOMIC_INIT(MAZE_STATE_START);
static uint32_t last_state_transition_ms;

void maze_game_init(void)
{
	atomic_set(&current_state, MAZE_STATE_START);
	last_state_transition_ms = k_uptime_get_32();
	maze_physics_set_active(false);
	maze_physics_request_reset();
}

void maze_game_on_button(button_event_t event)
{
	uint32_t now = k_uptime_get_32();
	if ((uint32_t)(now - last_state_transition_ms) < DEBOUNCE_LOCKOUT_MS) {
		return;
	}

	maze_state_t st = (maze_state_t)atomic_get(&current_state);

	switch (st) {
	case MAZE_STATE_START:
		/* Short or long press on title screen starts the game */
		printk("[maze] Title screen button -> start playing\n");
		maze_physics_request_reset();
		maze_physics_set_active(true);
		last_state_transition_ms = now;
		atomic_set(&current_state, MAZE_STATE_PLAYING);
		break;

	case MAZE_STATE_PLAYING:
		/* During gameplay: short-press does nothing, long-press resets to title screen */
		if (event == BUTTON_EVENT_LONG) {
			printk("[maze] Long press during play -> reset to title screen\n");
			maze_physics_set_active(false);
			maze_physics_request_reset();
			last_state_transition_ms = now;
			atomic_set(&current_state, MAZE_STATE_START);
		} else {
			printk("[maze] Short press during play ignored\n");
		}
		break;

	case MAZE_STATE_GAME_OVER:
	case MAZE_STATE_VICTORY:
		/* For game over or victory, pressing returns to the title screen */
		printk("[maze] Game Over/Victory button -> return to title screen\n");
		maze_physics_set_active(false);
		maze_physics_request_reset();
		last_state_transition_ms = now;
		atomic_set(&current_state, MAZE_STATE_START);
		break;
	}
}

maze_state_t maze_game_get_state(void)
{
	return (maze_state_t)atomic_get(&current_state);
}

static void game_step(uint32_t now)
{
	maze_state_t st = (maze_state_t)atomic_get(&current_state);

	if (st != MAZE_STATE_PLAYING) {
		return;
	}

	/* Wait until ball is actually in place if reset was pending */
	if (maze_physics_reset_pending()) {
		return;
	}

	float ball_x, ball_y;
	maze_physics_get_ball_pos(&ball_x, &ball_y);

	/* Check pit collisions */
	size_t n_pits = 0;
	const maze_hole_t *pits = maze_map_get_pits(&n_pits);

	for (size_t i = 0; i < n_pits; i++) {
		float dx = ball_x - pits[i].x;
		float dy = ball_y - pits[i].y;

		if (sqrtf(dx * dx + dy * dy) < pits[i].r) {
			printk("[maze] Ball fell in hole %d — GAME OVER\n", (int)i);
			maze_physics_set_active(false);
			maze_physics_request_reset();
			last_state_transition_ms = now;
			atomic_set(&current_state, MAZE_STATE_GAME_OVER);
			return;
		}
	}

	/* Check goal condition */
	const maze_hole_t *goal = maze_map_get_goal();
	float gx = ball_x - goal->x;
	float gy = ball_y - goal->y;

	if (sqrtf(gx * gx + gy * gy) < goal->r) {
		printk("[maze] Goal reached — ESCAPED!\n");
		maze_physics_set_active(false);
		maze_physics_request_reset();
		last_state_transition_ms = now;
		atomic_set(&current_state, MAZE_STATE_VICTORY);
	}
}

static void game_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	int64_t next_ms = k_uptime_get();

	while (true) {
		game_step(k_uptime_get_32());

		next_ms += GAME_PERIOD_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next_ms));
	}
}

void maze_game_start(void)
{
	maze_thread_spawn(&game_thread, game_stack,
			  K_THREAD_STACK_SIZEOF(game_stack),
			  game_fn, GAME_PRIO, MAZE_CORE_GAME, "game");
}

#if defined(CONFIG_SHELL)
static int cmd_maze_button(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	shell_print(sh, "[maze] Triggering button short press via shell");
	maze_game_on_button(BUTTON_EVENT_SHORT);
	return 0;
}

static int cmd_maze_long_button(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	shell_print(sh, "[maze] Triggering button long press via shell");
	maze_game_on_button(BUTTON_EVENT_LONG);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(maze_cmds,
	SHELL_CMD(button, NULL, "Trigger button short press", cmd_maze_button),
	SHELL_CMD(long_button, NULL, "Trigger button long press", cmd_maze_long_button),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(maze, &maze_cmds, "Tilt Maze commands", NULL);
#endif
