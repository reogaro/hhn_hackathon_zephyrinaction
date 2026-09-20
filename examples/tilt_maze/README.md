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
| 2    | game    | Pit/goal rules and banner state; asks physics to reset the ball |
| 3    | gy521   | 100 Hz I2C accelerometer polling |

Threads share only small snapshots (spinlock-protected ball position, atomic tilt, banner and reset flags).

## Wiring

| GY-521 Pin | Curiosity Kit Signal |
| ---------- | -------------------- |
| VCC        | 5V (or 3.3V)         |
| GND        | GND                  |
| SCL        | SCL                  |
| SDA        | SDA                  |
| AD0        | GND (or NC), address `0x68` |

## Build

From the repository root:

```bash
west build -b pic64gx_curiosity_kit/pic64gx1000/u54/smp -p always -d tilt_maze/build/game tilt_maze
```

## Orientation

Which way the ball rolls depends on how the GY-521 is mounted relative to the screen. If it rolls the wrong way, edit `TILT_INVERT_X`, `TILT_INVERT_Y` or `TILT_SWAP_XY` at the top of `src/gy521.c`. Set `GY521_DEBUG_PRINT` to `1` there to print the filtered tilt on the console (115200 8N1).

Without a sensor the game still starts, but the ball does not move; the console says `[gy521] no MPU-6050 found`.
