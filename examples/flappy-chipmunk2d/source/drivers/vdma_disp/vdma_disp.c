/* SPDX-License-Identifier: Apache-2.0
 *
 * PIC64GX VDMA display pipeline driver.
 *
 * Manages the VDMA read channel (reg "vdma") and Display Controller (reg "dc")
 * for 720p60 YUYV422 output.  The end-of-frame interrupt is routed through
 * the PLIC using the DT interrupt binding — DT_INST_IRQN(n) gives the correct
 * Zephyr multi-level encoded IRQ so Zephyr's PLIC driver handles all
 * priority/enable/context programming automatically.
 */

#include "vdma_disp.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>

#define DT_DRV_COMPAT microchip_pic64gx_vdma_disp

/* ── VDMA register offsets ────────────────────────────────────────────── */
#define VDMA_CTRL       0x04u
#define VDMA_GLBL_INT   0x08u
#define VDMA_INT_STATUS 0x0Cu
#define VDMA_INT_EN     0x10u
#define VDMA_BUFF_FIFO  0x1Cu

#define VDMA_IP_RESET  0x2u
#define VDMA_IP_ENABLE 0x1u
#define INT_EOF        BIT(0)

/* ── DC register offsets ──────────────────────────────────────────────── */
#define DC_CTRL        0x04u
#define DC_HRES        0x08u
#define DC_VRES        0x0Cu
#define DC_H_F_PORCH   0x10u
#define DC_H_B_PORCH   0x14u
#define DC_V_F_PORCH   0x18u
#define DC_V_B_PORCH   0x1Cu
#define DC_HSYNC_WIDTH 0x20u
#define DC_VSYNC_WIDTH 0x24u

/* CEA-861 720p60 timing */
#define DC_HRES_VAL 1280u
#define DC_VRES_VAL  720u
#define DC_HFP        110u
#define DC_HBP        220u
#define DC_HS          40u
#define DC_VFP          5u
#define DC_VBP         20u
#define DC_VS           5u

/* ── Driver structs ───────────────────────────────────────────────────── */

struct vdma_disp_config {
	uintptr_t  vdma_base;
	uintptr_t  dc_base;
	uintptr_t  fb[2];      /* [0]=front, [1]=back — CPU addresses */
	uint32_t   fifo[2];    /* VDMA FIFO values for fb[0] and fb[1] */
	void       (*irq_config)(void);
};

struct vdma_disp_data {
	struct k_sem vsync_sem;
	uint8_t      front_idx;    /* index into cfg->fb[] currently shown */
	atomic_t     frame_count;  /* incremented by ISR each VDMA EOF */
	atomic_t     render_count; /* incremented by vdma_disp_flip() */
};

/* ── ISR ──────────────────────────────────────────────────────────────── */

static void vdma_isr(const struct device *dev)
{
	const struct vdma_disp_config *cfg = dev->config;
	struct vdma_disp_data         *data = dev->data;

	sys_write32(0xFFu, cfg->vdma_base + VDMA_INT_STATUS);
	atomic_inc(&data->frame_count);
	k_sem_give(&data->vsync_sem);
}

/* ── Public API ───────────────────────────────────────────────────────── */

int vdma_disp_wait_vsync(const struct device *dev, k_timeout_t timeout)
{
	struct vdma_disp_data *data = dev->data;

	return k_sem_take(&data->vsync_sem, timeout);
}

void vdma_disp_flip(const struct device *dev)
{
	const struct vdma_disp_config *cfg  = dev->config;
	struct vdma_disp_data         *data = dev->data;
	uint8_t new_front = 1u - data->front_idx;

	/* Push new front buffer to VDMA; it will display on next frame */
	sys_write32(cfg->fifo[new_front], cfg->vdma_base + VDMA_BUFF_FIFO);
	data->front_idx = new_front;
	atomic_inc(&data->render_count);
}

uint32_t vdma_disp_get_frame_count(const struct device *dev)
{
	struct vdma_disp_data *data = dev->data;

	return (uint32_t)atomic_set(&data->frame_count, 0);
}

uint32_t vdma_disp_get_render_count(const struct device *dev)
{
	struct vdma_disp_data *data = dev->data;

	return (uint32_t)atomic_set(&data->render_count, 0);
}

void *vdma_disp_front_buf(const struct device *dev)
{
	const struct vdma_disp_config *cfg  = dev->config;
	const struct vdma_disp_data   *data = dev->data;

	return (void *)cfg->fb[data->front_idx];
}

void *vdma_disp_back_buf(const struct device *dev)
{
	const struct vdma_disp_config *cfg  = dev->config;
	const struct vdma_disp_data   *data = dev->data;

	return (void *)cfg->fb[1u - data->front_idx];
}

/* ── Init ─────────────────────────────────────────────────────────────── */

static int vdma_disp_init(const struct device *dev)
{
	const struct vdma_disp_config *cfg  = dev->config;
	struct vdma_disp_data         *data = dev->data;

	k_sem_init(&data->vsync_sem, 0, 1);
	data->front_idx = 0u;

	/* Clear both framebuffers to black (YUYV: Y=16, U=128, V=128) */
	const uint32_t black = 0x80108010u;

	for (int b = 0; b < 2; b++) {
		uint32_t *p = (uint32_t *)cfg->fb[b];
		uint32_t  n = (DC_HRES_VAL * DC_VRES_VAL * 2u) / 4u;

		for (uint32_t i = 0; i < n; i++) {
			p[i] = black;
		}
	}

	/* Reset both IPs */
	sys_write32(VDMA_IP_RESET, cfg->vdma_base + VDMA_CTRL);
	sys_write32(VDMA_IP_RESET, cfg->dc_base   + DC_CTRL);

	/* DC 720p60 timing — written before enable */
	sys_write32(DC_HRES_VAL, cfg->dc_base + DC_HRES);
	sys_write32(DC_VRES_VAL, cfg->dc_base + DC_VRES);
	sys_write32(DC_HFP,      cfg->dc_base + DC_H_F_PORCH);
	sys_write32(DC_HBP,      cfg->dc_base + DC_H_B_PORCH);
	sys_write32(DC_HS,       cfg->dc_base + DC_HSYNC_WIDTH);
	sys_write32(DC_VFP,      cfg->dc_base + DC_V_F_PORCH);
	sys_write32(DC_VBP,      cfg->dc_base + DC_V_B_PORCH);
	sys_write32(DC_VS,       cfg->dc_base + DC_VSYNC_WIDTH);

	/* Enable DC then VDMA */
	sys_write32(VDMA_IP_ENABLE, cfg->dc_base   + DC_CTRL);
	sys_write32(VDMA_IP_ENABLE, cfg->vdma_base + VDMA_CTRL);

	/*
	 * Enable interrupts AFTER IP_ENABLE — registers may be reset while
	 * CTRL bit1 (reset) is asserted, so configure them after release.
	 */
	sys_write32(1u,     cfg->vdma_base + VDMA_GLBL_INT);
	sys_write32(INT_EOF, cfg->vdma_base + VDMA_INT_EN);
	sys_write32(0xFFu,  cfg->vdma_base + VDMA_INT_STATUS);

	/* Push front buffer address so VDMA starts outputting (black screen) */
	sys_write32(cfg->fifo[0u], cfg->vdma_base + VDMA_BUFF_FIFO);

	/* Register ISR via DT-computed multi-level encoded IRQ */
	cfg->irq_config();

	return 0;
}

/* ── Device instantiation ─────────────────────────────────────────────── */

#define VDMA_DISP_IRQ_FUNC(n)						\
	static void vdma_disp_irq_config_##n(void)			\
	{								\
		IRQ_CONNECT(DT_INST_IRQN(n),				\
			    DT_INST_IRQ(n, priority),			\
			    vdma_isr,					\
			    DEVICE_DT_INST_GET(n), 0);			\
		irq_enable(DT_INST_IRQN(n));				\
	}

#define VDMA_DISP_DEFINE(n)						\
	VDMA_DISP_IRQ_FUNC(n)						\
	static struct vdma_disp_data vdma_disp_data_##n;		\
	static const struct vdma_disp_config vdma_disp_cfg_##n = {	\
		.vdma_base  = DT_INST_REG_ADDR_BY_NAME(n, vdma),	\
		.dc_base    = DT_INST_REG_ADDR_BY_NAME(n, dc),		\
		.fb         = { DT_INST_PROP(n, fb_front_addr),	\
				DT_INST_PROP(n, fb_back_addr) },	\
		.fifo       = { DT_INST_PROP(n, fifo_front_val),	\
				DT_INST_PROP(n, fifo_back_val) },	\
		.irq_config = vdma_disp_irq_config_##n,			\
	};								\
	DEVICE_DT_INST_DEFINE(n, vdma_disp_init, NULL,			\
			      &vdma_disp_data_##n,			\
			      &vdma_disp_cfg_##n,			\
			      POST_KERNEL,				\
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,	\
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(VDMA_DISP_DEFINE)
