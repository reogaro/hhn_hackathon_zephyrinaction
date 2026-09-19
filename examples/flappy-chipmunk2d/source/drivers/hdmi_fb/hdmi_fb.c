/* SPDX-License-Identifier: Apache-2.0
 *
 * HDMI framebuffer display driver for PIC64GX.
 *
 * Implements Zephyr's display_driver_api.  LVGL renders RGB565 tiles into
 * its VDB; display_write() converts each tile to YUYV422 and writes to the
 * vdma_disp back buffer.  On the final tile of each frame it waits for vsync
 * and flips the double buffer — this naturally rate-limits LVGL to the
 * display frame rate.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <string.h>

#include "vdma_disp.h"

#define DT_DRV_COMPAT microchip_pic64gx_hdmi_fb

struct hdmi_fb_config {
	uint16_t width;
	uint16_t height;
};

/* ── RGB565 → YUYV422 (BT.601 full-range) ───────────────────────────── */

static inline void rgb565_pair_to_yuyv(uint16_t p0, uint16_t p1, uint8_t *out)
{
	int32_t r0 = (p0 >> 8) & 0xF8;
	int32_t g0 = (p0 >> 3) & 0xFC;
	int32_t b0 = (p0 << 3) & 0xF8;

	int32_t r1 = (p1 >> 8) & 0xF8;
	int32_t g1 = (p1 >> 3) & 0xFC;
	int32_t b1 = (p1 << 3) & 0xF8;

	out[0] = (uint8_t)(((66*r0 + 129*g0 +  25*b0 + 128) >> 8) + 16);
	out[1] = (uint8_t)(((-38*r0 - 74*g0 + 112*b0 + 128) >> 8) + 128);
	out[2] = (uint8_t)(((66*r1 + 129*g1 +  25*b1 + 128) >> 8) + 16);
	out[3] = (uint8_t)(((112*r0 - 94*g0 -  18*b0 + 128) >> 8) + 128);
}

/* ── display_driver_api ──────────────────────────────────────────────── */

static const struct device *vdma_dev = DEVICE_DT_GET(DT_NODELABEL(vdma_disp));

static int hdmi_fb_init(const struct device *dev)
{
	if (!device_is_ready(vdma_dev)) {
		return -ENODEV;
	}
	return 0;
}

static int hdmi_fb_write(const struct device *dev,
			 const uint16_t x, const uint16_t y,
			 const struct display_buffer_descriptor *desc,
			 const void *buf)
{
	const struct hdmi_fb_config *cfg     = dev->config;
	const uint16_t              *src_row = buf;
	const uint32_t               stride  = (uint32_t)cfg->width * 2u;

	/*
	 * Single-buffer mode: write directly to the front (displayed) buffer.
	 * The VDMA loops this buffer at 60 Hz; LVGL updates only dirty regions
	 * so changes appear on the next VDMA scan with no vsync wait required.
	 * Tearing on fast-moving content is possible but imperceptible for
	 * text that updates at ≤20 fps.
	 */
	uint8_t *dst_base = vdma_disp_front_buf(vdma_dev);

	for (uint16_t row = 0; row < desc->height; row++) {
		const uint16_t *src = src_row;
		uint8_t *dst = dst_base
			       + (uintptr_t)(y + row) * stride
			       + (uintptr_t)x * 2u;

		uint16_t col = 0;

		for (; col + 1 < desc->width; col += 2, src += 2, dst += 4) {
			rgb565_pair_to_yuyv(src[0], src[1], dst);
		}
		if (col < desc->width) {
			rgb565_pair_to_yuyv(src[0], src[0], dst);
		}

		src_row += desc->pitch;
	}

	return 0;
}

static void hdmi_fb_get_capabilities(const struct device *dev,
				     struct display_capabilities *caps)
{
	const struct hdmi_fb_config *cfg = dev->config;

	caps->x_resolution            = cfg->width;
	caps->y_resolution            = cfg->height;
	caps->supported_pixel_formats = PIXEL_FORMAT_RGB_565;
	caps->current_pixel_format    = PIXEL_FORMAT_RGB_565;
	caps->current_orientation     = DISPLAY_ORIENTATION_NORMAL;
	/*
	 * Force LVGL to flush full-width strips (x always 0, width always
	 * screen width).  YUYV422 packs pixel pairs so x must be even; this
	 * flag ensures that invariant without extra logic in the flush path.
	 */
	caps->screen_info             = SCREEN_INFO_X_ALIGNMENT_WIDTH;
}

static int hdmi_fb_blanking_on(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int hdmi_fb_blanking_off(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int hdmi_fb_set_pixel_format(const struct device *dev,
				    const enum display_pixel_format fmt)
{
	ARG_UNUSED(dev);
	return (fmt == PIXEL_FORMAT_RGB_565) ? 0 : -ENOTSUP;
}

static const struct display_driver_api hdmi_fb_api = {
	.blanking_on      = hdmi_fb_blanking_on,
	.blanking_off     = hdmi_fb_blanking_off,
	.write            = hdmi_fb_write,
	.get_capabilities = hdmi_fb_get_capabilities,
	.set_pixel_format = hdmi_fb_set_pixel_format,
};

#define HDMI_FB_DEFINE(n)						\
	static const struct hdmi_fb_config hdmi_fb_config_##n = {	\
		.width  = DT_INST_PROP(n, width),			\
		.height = DT_INST_PROP(n, height),			\
	};								\
	DEVICE_DT_INST_DEFINE(n, hdmi_fb_init, NULL,			\
			      NULL, &hdmi_fb_config_##n,		\
			      POST_KERNEL,				\
			      CONFIG_DISPLAY_INIT_PRIORITY,		\
			      &hdmi_fb_api);

DT_INST_FOREACH_STATUS_OKAY(HDMI_FB_DEFINE)
