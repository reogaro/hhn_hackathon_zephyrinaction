# Tilt Maze (GY-521)

Roll the ball through the maze to the goal by tilting a **GY-521** (InvenSense MPU-6050) board. Falling into a pit resets the ball to the start.

The tilt comes from the accelerometer's X/Y axes (gravity vector), read over I2C in `src/gy521.c`. Detection and init follow `examples/i2c-gy521`: both `i2c0` and `i2c1` are probed at `0x68` and `0x69`, because the FPGA design decides which controller reaches the header.

The VDMA/HDMI display drivers and devicetree bindings are reused from `examples/flappy-microchip`.

## Threads and cores

Each module runs in its own thread, pinned to its own U54 hart (`src/maze_cores.h`):

| Core | Thread  | Job |
| ---- | ------- | --- |
| 0    | ui      | LVGL scene graph and rendering; the only thread that touches LVGL |
| 1    | physics | Chipmunk2D at ~60 Hz; the only thread that touches Chipmunk; reads tilt, publishes ball position |
| 2    | game    | Pit/goal rules and state machine (START, PLAYING, GAME OVER, VICTORY) |
| 3    | gy521   | 100 Hz I2C accelerometer polling |
| 2    | led_ring | WS2812 ring (SPI1, polled): follows the game state (shares core 2) |
| 3    | button  | mikroBUS button polling (light, shares core 3); starts/restarts game |

Threads share only small snapshots (spinlock-protected ball position, atomic tilt, game state flags).

## Wiring

| GY-521 Pin | Curiosity Kit Signal |
| ---------- | -------------------- |
| VCC        | 5V (or 3.3V)         |
| GND        | GND                  |
| SCL        | SCL                  |
| SDA        | SDA                  |
| AD0        | GND (or NC), address `0x68` |

## Red button
 
A push button on the mikroBUS INT pin (`gpio1` pin 0, see `app.overlay`) controls the game flow:
- **Title screen**: Press the button to start the game.
- **Gameplay**: Short presses are ignored; long press (hold for >= 1 second) resets to the title screen.
- **Game Over / Victory**: Press the button to return to the title screen.

Wire one leg to INT and the diagonal leg to GND, with a 10 kΩ pull-up from INT to 3P3V.

## LED ring

A 24-LED WS2812 ring shows the game state. Wire DIN to the mikroBUS MOSI pin, 5V and GND to the ring, with a common ground. No pull-up is needed. The LED count is `chain-length` in `app.overlay`; brightness is `BRIGHTNESS` in `src/led_ring.c`. Without a ring connected the game runs normally.

| Situation | Ring |
| --------- | ---- |
| Title screen (until the game is started) | steady white at half brightness (saves current) |
| Playing | moving rainbow |
| Game over | red blinks until the game is restarted |
| Victory | rainbow blinks until the game is restarted |

## Build

From the repository root:

```bash
west build -b pic64gx_curiosity_kit/pic64gx1000/u54/smp -p always -d tilt_maze/build/game tilt_maze
```

## Orientation

Which way the ball rolls depends on how the GY-521 is mounted relative to the screen. If it rolls the wrong way, edit `TILT_INVERT_X`, `TILT_INVERT_Y` or `TILT_SWAP_XY` at the top of `src/gy521.c`. Set `GY521_DEBUG_PRINT` to `1` there to print the filtered tilt on the console (115200 8N1).

Without a sensor the game still starts, but the ball does not move; the console says `[gy521] no MPU-6050 found`.
