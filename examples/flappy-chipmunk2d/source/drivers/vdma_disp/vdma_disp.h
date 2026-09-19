/* SPDX-License-Identifier: Apache-2.0
 *
 * PIC64GX VDMA display pipeline public API.
 */

#ifndef SRC_DRIVERS_VDMA_DISP_H
#define SRC_DRIVERS_VDMA_DISP_H

#include <zephyr/device.h>
#include <zephyr/kernel.h>

/**
 * Wait for the next end-of-frame interrupt from the VDMA.
 * Returns 0 on success, -EAGAIN on timeout.
 */
int vdma_disp_wait_vsync(const struct device *dev, k_timeout_t timeout);

/**
 * Push the current back buffer to the VDMA (it becomes the new front)
 * and swap internal front/back tracking.  Call after rendering a frame.
 */
void vdma_disp_flip(const struct device *dev);

/**
 * Return the front (currently-displayed) buffer address.
 * Writing here updates what the VDMA is scanning — single-buffer mode.
 * Changes are immediately visible with no vsync wait needed.
 */
void *vdma_disp_front_buf(const struct device *dev);

/**
 * Return a pointer to the back (draw) buffer.  Always write here.
 * The pointer changes after each vdma_disp_flip() call.
 */
void *vdma_disp_back_buf(const struct device *dev);

/**
 * Atomically read and reset the VDMA EOF interrupt counter.
 * Call once per second to get the hardware output FPS.
 */
uint32_t vdma_disp_get_frame_count(const struct device *dev);

/**
 * Atomically read and reset the flip (render) counter.
 * Call once per second to get the LVGL render FPS.
 */
uint32_t vdma_disp_get_render_count(const struct device *dev);

#endif /* SRC_DRIVERS_VDMA_DISP_H */
