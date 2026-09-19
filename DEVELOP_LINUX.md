# Linux Development & Quickstart

## 1. Environment Setup

```bash
source ~/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=${ZEPHYR_BASE:-~/zephyrproject/zephyr}
export ZEPHYR_SDK_INSTALL_DIR=${ZEPHYR_SDK_INSTALL_DIR:-/opt/zephyr-sdk}
export PATH="$ZEPHYR_SDK_INSTALL_DIR/gnu/riscv64-zephyr-elf/bin:$HOME/hss-payload-generator:$PATH"
```

## 2. Build

From the root of the repository:

```bash
# Example: GY-521 IMU
west build -b pic64gx_curiosity_kit/pic64gx1000/u54/smp -p always -d build/i2c-gy521 examples/i2c-gy521

# Example: Flappy Microchip
west build -b pic64gx_curiosity_kit/pic64gx1000/u54/smp -p always -d build/flappy-microchip examples/flappy-microchip/source/demos/pic64_smp_hello
```

## 3. Deploy to RAM via JTAG (Fast Iteration)

### Start OpenOCD Server (Terminal 1)
```bash
openocd \
  -s "$ZEPHYR_SDK_INSTALL_DIR/hosttools/sysroots/x86_64-pokysdk-linux/usr/share/openocd/scripts" \
  -f board/microchip/pic64gx-curiosity-kit.cfg
```

### Load & Run with GDB (Terminal 2)

You might need to turn this into an one-liner, GDB and multi-line commands sometimes don't play well with eachother

```bash
riscv64-zephyr-elf-gdb build/i2c-gy521/zephyr/zephyr.elf --batch \
  -ex "set pagination off" -ex "set confirm off" \
  -ex "target extended-remote localhost:3333" \
  -ex "thread 2" -ex "load" \
  -ex "thread 2" -ex "set \$pc = 0x80000000" \
  -ex "thread 3" -ex "set \$pc = 0x80000000" \
  -ex "thread 4" -ex "set \$pc = 0x80000000" \
  -ex "thread 5" -ex "set \$pc = 0x80000000" \
  -ex "thread 2" -ex "continue &"
```

## 4. Deploy to SD Card (Permanent)

```bash
# Generate HSS payload
sed "s|zephyr\.elf|$(pwd)/build/i2c-gy521/zephyr/zephyr.elf|" \
  examples/i2c-gy521/hss-payload.yaml > build/i2c-gy521/hss-payload.local.yaml

hss-payload-generator -c build/i2c-gy521/hss-payload.local.yaml build/i2c-gy521/payload.bin

# Write to SD card (replace /dev/mmcblk0 with your card)
sudo dd if=build/i2c-gy521/payload.bin of=/dev/mmcblk0 status=progress conv=fsync
```

## 5. Serial Console

```bash
picocom -b 115200 /dev/ttyUSB2
```
