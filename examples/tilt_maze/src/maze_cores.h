/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — CPU core assignment & pinned-thread helper
 *
 * Each module runs in its own thread, pinned to its own U54 hart.
 * LVGL and Chipmunk2D are not thread-safe, so each is only ever touched by
 * the one thread that owns it; modules exchange data via small snapshots.
 */

#ifndef SRC_MAZE_CORES_H
#define SRC_MAZE_CORES_H

#include <zephyr/kernel.h>

#define MAZE_CORE_UI       0  /* LVGL scene graph + rendering */
#define MAZE_CORE_PHYSICS  1  /* Chipmunk2D simulation */
#define MAZE_CORE_GAME     2  /* pit / goal rules, banner state */
#define MAZE_CORE_SENSOR   3  /* GY-521 I2C polling */
#define MAZE_CORE_BUTTON   3  /* mikroBUS button polling (light, shares the sensor core) */

/* Create a thread that is only allowed to run on @p core. The mask has to be
 * set while the thread is not runnable, hence K_FOREVER + k_thread_start().
 */
static inline void maze_thread_spawn(struct k_thread *thread,
				     k_thread_stack_t *stack, size_t stack_size,
				     k_thread_entry_t entry, int prio,
				     int core, const char *name)
{
	k_tid_t tid = k_thread_create(thread, stack, stack_size, entry,
				      NULL, NULL, NULL, prio, 0, K_FOREVER);

	k_thread_name_set(tid, name);
	k_thread_cpu_mask_clear(tid);
	k_thread_cpu_mask_enable(tid, core);
	k_thread_start(tid);
}

#endif /* SRC_MAZE_CORES_H */
