/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SRC_DEMO_MANAGER_H
#define SRC_DEMO_MANAGER_H

#include <lvgl.h>

/**
 * Screen descriptor — each demo screen registers one of these.
 */
typedef struct {
	const char  *name;
	lv_obj_t   *(*create)(void);  /* build all widgets, return screen obj */
	void        (*update)(void);  /* 1 Hz live-data refresh               */
} demo_screen_t;

/** Start the demo manager thread (pinned to core 2). */
void demo_manager_start(void);

/** Called by the FPS overlay — returns last measured render fps. */
uint32_t demo_get_render_fps(void);

/** Called by the FPS overlay — returns last measured output fps. */
uint32_t demo_get_output_fps(void);

/** Current screen index (0–3). */
int demo_get_screen_idx(void);

#endif /* SRC_DEMO_MANAGER_H */
