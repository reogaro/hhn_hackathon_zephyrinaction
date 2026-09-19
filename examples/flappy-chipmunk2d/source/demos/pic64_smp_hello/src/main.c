/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PIC64GX Curiosity Kit — SMP + display pipeline
 * Timestamps added to every print so Zephyr time can be compared to HSS time.
 */

#include "ui.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/version.h>
#include <zephyr/drivers/gpio.h>

#define TS() k_uptime_get()

/* ── LED blink ────────────────────────────────────────────────────────── */

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static K_THREAD_STACK_DEFINE(blink_stack, 512);
static struct k_thread blink_thread;

static void blink_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	if (!gpio_is_ready_dt(&led0)) {
		printk("[%lld] LED GPIO not ready\n", TS());
		return;
	}
	gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);

	while (true) {
		gpio_pin_toggle_dt(&led0);
		k_sleep(K_MSEC(500));
	}
}

/* ── Per-core workers ─────────────────────────────────────────────────── */

#define WORKER_STACK  1024
#define WORKER_PRIO   5

static K_THREAD_STACK_ARRAY_DEFINE(worker_stacks, 3, WORKER_STACK);
static struct k_thread worker_threads[3];

static void core_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
	printk("[%lld] Hello from U54 core %d (hart %d)\n",
	       TS(), arch_curr_cpu()->id, arch_curr_cpu()->id + 1);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
	printk("[%lld] *** PIC64GX SMP bring-up ***\n", TS());
	printk("[%lld] Zephyr %s  |  %d CPUs\n\n", TS(),
	       KERNEL_VERSION_STRING, arch_num_cpus());

	k_thread_create(&blink_thread, blink_stack,
			K_THREAD_STACK_SIZEOF(blink_stack),
			blink_fn, NULL, NULL, NULL, 10, 0, K_NO_WAIT);
	k_thread_name_set(&blink_thread, "blink");

	printk("[%lld] Hello from U54 core 0 (hart 1) [main]\n", TS());

	for (int i = 0; i < 3; i++) {
		k_tid_t tid = k_thread_create(&worker_threads[i],
					      worker_stacks[i], WORKER_STACK,
					      core_fn, NULL, NULL, NULL,
					      WORKER_PRIO, 0, K_NO_WAIT);
#ifdef CONFIG_SCHED_CPU_MASK
		k_thread_cpu_mask_clear(tid);
		k_thread_cpu_mask_enable(tid, i + 1);
#else
		ARG_UNUSED(tid);
#endif
	}

	printk("[%lld] Sleeping 200ms for workers...\n", TS());
	k_sleep(K_MSEC(200));
	printk("[%lld] Awake after 200ms sleep\n", TS());

	printk("[%lld] All cores reported. LED blinking.\n\n", TS());

	ui_start();
	return 0;
}
