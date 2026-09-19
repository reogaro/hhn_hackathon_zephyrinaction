# Shared Display Drivers

These drivers implement the PIC64GX Curiosity Kit HDMI display pipeline. They are
shared across all demos in this repository — no demo-specific logic lives here.

## vdma_disp — VDMA + Display Controller

**Files:** `vdma_disp/vdma_disp.c`, `vdma_disp/vdma_disp.h`

Manages two hardware IP blocks:

| IP | Base address | Function |
|----|-------------|---------|
| VDMA read channel | `0x6000A000` | Reads YUYV422 frames from DDR, feeds Display Controller |
| Display Controller | `0x6000C000` | Generates CEA-861 720p60 HDMI sync + active video |

**Public API:**

```c
void  *vdma_disp_front_buf(const struct device *dev);   // address of displayed buffer
void  *vdma_disp_back_buf(const struct device *dev);    // address of off-screen buffer
void   vdma_disp_flip(const struct device *dev);        // push back buf → VDMA, swap
int    vdma_disp_wait_vsync(const struct device *dev, k_timeout_t t);
uint32_t vdma_disp_get_frame_count(const struct device *dev);  // VDMA EOF IRQ count/s
uint32_t vdma_disp_get_render_count(const struct device *dev); // flip count/s
```

**DT compatible:** `microchip,pic64gx-vdma-disp`
**Binding:** `../dts/bindings/microchip,pic64gx-vdma-disp.yaml`

**Critical register note:** `BUFF_ADDR_FIFO` is at offset `0x1C` (not `0x14`).
The FIFO value = `38-bit physical DDR address >> 6`.

---

## hdmi_fb — Zephyr Display Driver

**Files:** `hdmi_fb/hdmi_fb.c`

Implements Zephyr's `display_driver_api`. LVGL calls `display_write()` with RGB565 tiles;
the driver converts each tile to YUYV422 (BT.601) and writes it to the `vdma_disp` front
buffer (single-buffer mode — no vsync wait, no flip).

**Single-buffer rendering:** LVGL writes directly to the VDMA-displayed frame. The VDMA
loops the buffer at 60 Hz; LVGL partial updates appear on the next scan. Tearing is
imperceptible on text updating at ≤30 fps.

**YUYV422 alignment:** `caps->screen_info = SCREEN_INFO_X_ALIGNMENT_WIDTH` forces LVGL
to align all dirty regions to even x boundaries, satisfying the YUYV422 pixel-pair
requirement without extra logic in the flush path.

**DT compatible:** `microchip,pic64gx-hdmi-fb`
**Binding:** `../dts/bindings/microchip,pic64gx-hdmi-fb.yaml`

---

## Adding a new demo that uses these drivers

In the demo's `CMakeLists.txt`:

```cmake
list(APPEND DTS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")   # finds dts/bindings/

set(DRIVERS "${CMAKE_CURRENT_SOURCE_DIR}/../../drivers")

target_sources(app PRIVATE
    ...
    ${DRIVERS}/vdma_disp/vdma_disp.c
    ${DRIVERS}/hdmi_fb/hdmi_fb.c
)

target_include_directories(app PRIVATE
    ...
    ${DRIVERS}/vdma_disp
    ${DRIVERS}/hdmi_fb
)
```

In the demo's `app.overlay`, include the shared hardware nodes:

```dts
#include "../../dts/overlays/pic64gx_display.overlay"
/* then add your SRAM region and chosen { zephyr,sram = ...; } */
```
