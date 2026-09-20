/* SPDX-License-Identifier: Apache-2.0
 *
 * Tilt Maze — 2D Marble Labyrinth for PIC64GX Curiosity Kit
 *
 * Controlled by a GY-521 (InvenSense MPU-6050) IMU over I2C.
 * Rendered using native LVGL v9 scene graph objects and Chipmunk2D physics.
 * UI, physics, game rules and sensor each run in their own thread on their
 * own core (see maze_cores.h).
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
#include "button.h"
#include "led_ring.h"

/* Long press on the button: put the ball back at the start */
static void on_long_press(void)
{
	maze_physics_request_reset();
	led_ring_flash(LED_RING_FLASH_RESET);
}

int main(void)
{
	printk("[tilt_maze] Booting Tilt Maze...\n");

	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display)) {
		printk("[tilt_maze] Error: Display device not ready!\n");
		return 0;
	}
	display_blanking_off(display);

	/* Build shared state single-threaded, then hand each module its own core */
	maze_map_init();
	maze_physics_init();
	maze_game_init();

	gy521_start();        /* core 3: sensor */
	maze_physics_start(); /* core 1: physics */
	maze_game_start();    /* core 2: game rules */
	maze_ui_start();      /* core 0: LVGL */
	led_ring_start();     /* core 2: LED ring */
	button_start(on_long_press); /* core 3: long press restarts the ball */

	printk("[tilt_maze] Initialization complete. UI/physics/game/sensor running on cores 0-3.\n");

	/* Everything runs in the pinned threads; main has nothing left to do. */
	return 0;
}
