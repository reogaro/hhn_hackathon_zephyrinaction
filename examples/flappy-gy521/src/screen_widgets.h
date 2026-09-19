/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SRC_SCREEN_WIDGETS_H
#define SRC_SCREEN_WIDGETS_H

#include <lvgl.h>

lv_obj_t *screen_widgets_create(void);
void      screen_widgets_update(void);  /* no-op — all driven by lv_anim_t */

#endif /* SRC_SCREEN_WIDGETS_H */
