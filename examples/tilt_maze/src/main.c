/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — 2D Marble Labyrinth for PIC64GX Curiosity Kit
 *
 * Controlled by a GY-521 (InvenSense MPU-6050) IMU over I2C.
 * Rendered using native LVGL v9 scene graph objects and Chipmunk2D physics.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>
#include <lvgl.h>

#include "maze_map.h"
#include "maze_physics.h"
#include "maze_ui.h"
#include "maze_game.h"
#include "gy521.h"

int main(void)
{
	printk("[tilt_maze] Booting Tilt Maze...\n");

	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display)) {
		printk("[tilt_maze] Error: Display device not ready!\n");
		return 0;
	}
	display_blanking_off(display);

	/* Initialize modular subsystems */
	maze_map_init();
	maze_physics_init();
	maze_ui_init();
	maze_game_init();

	/* Start sensor reader thread */
	gy521_start();

	printk("[tilt_maze] Initialization complete. Entering LVGL dispatch loop.\n");

	while (1) {
		uint32_t sleep_ms = lv_timer_handler();
		if (sleep_ms > 16) {
			sleep_ms = 16;
		} else if (sleep_ms == 0) {
			sleep_ms = 1;
		}
		k_msleep(sleep_ms);
	}
}
