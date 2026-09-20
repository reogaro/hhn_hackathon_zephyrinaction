/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SRC_LED_RING_H
#define SRC_LED_RING_H

/**
 * Start the WS2812 LED ring thread (pinned to MAZE_CORE_LED).
 *
 * The ring follows the game state (read from maze_game_get_state(), so the
 * game code does not know about the ring):
 *   title screen  steady white at half brightness (saves current)
 *   playing       moving rainbow
 *   game over     red blinks until the game is restarted
 *   victory       rainbow blinks until the game is restarted
 *
 * The ring is on the mikroBUS MOSI pin (SPI1). If it is not connected the
 * game is unaffected.
 */
void led_ring_start(void);

#endif /* SRC_LED_RING_H */
