# Flappy Chipmunk2D (PIC64GX)

This is a complete port of the Flappy Bird mini-game for the **PIC64GX Curiosity Kit** running **Zephyr RTOS**, replacing manual heuristic physics with the **Chipmunk2D rigid-body physics engine**.

## Features & Chipmunk2D Integration

- **Rigid-Body Physics**: The player is modeled as a dynamic `cpBody` with custom mass, moment of inertia, elasticity, and surface friction.
- **Kinematic Obstacles**: Pipes are modeled as kinematic bodies with upper and lower rectangular bounding box shapes moving in physical space.
- **Real-Time Collision Manifolds**: Collisions with pipes, ground, and ceiling are detected and handled via `cpCollisionHandler` callbacks in Chipmunk2D (`cpSpaceAddCollisionHandler`).
- **Dynamic Ragdoll / Collision Response**: When the player collides with an obstacle or pipe, Chipmunk calculates impulse, bounce restitution, and angular momentum, causing the sprite to naturally tumble and bounce across the ground.
- **Aerodynamic Tilt**: During active flight, the player sprite orientation smoothly tracks physical vertical velocity and angle.
- **Multi-Core SMP & Hardware FPU**: Runs on the 64-bit quad-core RISC-V SiFive U54 cluster with hardware FPU acceleration (`CONFIG_FPU_SHARING=y`, `CONFIG_SMP=y`).

## Controls

- **SW1** (`sw0` alias / Enter): Exit to the stats HUD / dashboard demo.
- **SW2** (`sw2` / Jump): Flap in flight, Start in title screen, Restart after Game Over.

## Building & Deploying

### 1. Build with West
```bash
west build -b pic64gx_curiosity_kit/pic64gx1000/u54/smp -p always -d build/flappy-chipmunk2d examples/flappy-chipmunk2d/source/demos/pic64_smp_hello
```

### 2. Fast JTAG RAM Deploy
```bash
# Terminal 1: OpenOCD
openocd -s "$ZEPHYR_SDK_INSTALL_DIR/hosttools/sysroots/x86_64-pokysdk-linux/usr/share/openocd/scripts" -f board/microchip/pic64gx-curiosity-kit.cfg

# Terminal 2: GDB Load
riscv64-zephyr-elf-gdb build/flappy-chipmunk2d/zephyr/zephyr.elf --batch \
  -ex "set pagination off" -ex "set confirm off" \
  -ex "target extended-remote localhost:3333" \
  -ex "thread 2" -ex "load" \
  -ex "thread 2" -ex "set \$pc = 0x80000000" \
  -ex "thread 3" -ex "set \$pc = 0x80000000" \
  -ex "thread 4" -ex "set \$pc = 0x80000000" \
  -ex "thread 5" -ex "set \$pc = 0x80000000" \
  -ex "thread 2" -ex "continue &"
```

### 3. Generate HSS Payload for SD Card
```bash
sed "s|zephyr\.elf|$(pwd)/build/flappy-chipmunk2d/zephyr/zephyr.elf|" \
  examples/flappy-chipmunk2d/source/demos/pic64_smp_hello/hss-payload.yaml > build/flappy-chipmunk2d/hss-payload.local.yaml

hss-payload-generator -c build/flappy-chipmunk2d/hss-payload.local.yaml build/flappy-chipmunk2d/payload.bin
```

