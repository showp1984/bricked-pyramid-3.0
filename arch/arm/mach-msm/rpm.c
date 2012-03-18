/* Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bug.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#ifdef CONFIG_ARCH_MSM8960
#include <linux/slab.h>
#endif
#include <linux/platform_device.h>
#include <asm/hardware/gic.h>
#include <mach/msm_iomap.h>
#include <mach/rpm.h>
#include <mach/board_htc.h>


/******************************************************************************
 * Data type and structure definitions
 *****************************************************************************/

struct msm_rpm_request {
	struct msm_rpm_iv_pair *req;
	int count;
	uint32_t *ctx_mask_ack;
	uint32_t *sel_masks_ack;
	struct completion *done;
};

struct msm_rpm_notif_config {
	struct msm_rpm_iv_pair iv[MSM_RPM_SEL_MASK_SIZE * 2];
};

#define configured_iv(notif_cfg) ((notif_cfg)->iv)
#define registered_iv(notif_cfg) ((notif_cfg)->iv + MSM_RPM_SEL_MASK_SIZE)

#ifdef CONFIG_ARCH_MSM8960
static int rpm_debug_enable = 0;
#endif
static struct msm_rpm_platform_data *msm_rpm_platform;
static uint32_t msm_rpm_map[MSM_RPM_ID_LAST + 1];
static stats_blob *msm_rpm_stat_data;

static DEFINE_MUTEX(msm_rpm_mutex);
static DEFINE_SPINLOCK(msm_rpm_lock);
static DEFINE_SPINLOCK(msm_rpm_irq_lock);

static struct msm_rpm_request *msm_rpm_request;
static struct msm_rpm_request msm_rpm_request_irq_mode;
static struct msm_rpm_request msm_rpm_request_poll_mode;

static LIST_HEAD(msm_rpm_notifications);
static struct msm_rpm_notif_config msm_rpm_notif_cfgs[MSM_RPM_CTX_SET_COUNT];
static bool msm_rpm_init_notif_done;

/******************************************************************************
 * Internal functions
 *****************************************************************************/

static inline uint32_t msm_rpm_read(unsigned int page, unsigned int reg)
{
	return __raw_readl(msm_rpm_platform->reg_base_addrs[page] + reg * 4);
}

static inline void msm_rpm_write(
	unsigned int page, unsigned int reg, uint32_t value)
{
	__raw_writel(value, msm_rpm_platform->reg_base_addrs[page] + reg * 4);
}

static inline void msm_rpm_read_contiguous(
	unsigned int page, unsigned int reg, uint32_t *values, int count)
{
	int i;

	for (i = 0; i < count; i++)
		values[i] = msm_rpm_read(page, reg + i);
}

static inline void msm_rpm_write_contiguous(
	unsigned int page, unsigned int reg, uint32_t *values, int count)
{
	int i;

	for (i = 0; i < count; i++)
		msm_rpm_write(page, reg + i, values[i]);
}

static inline void msm_rpm_write_contiguous_zeros(
	unsigned int page, unsigned int reg, int count)
{
	int i;

	for (i = 0; i < count; i++)
		msm_rpm_write(page, reg + i, 0);
}

static inline uint32_t msm_rpm_map_id_to_sel(uint32_t id)
{
	return (id > MSM_RPM_ID_LAST) ? MSM_RPM_SEL_LAST + 1 : msm_rpm_map[id];
}

/*
 * Note: the function does not clear the masks before filling them.
 *
 * Return value:
 *   0: success
 *   -EINVAL: invalid id in <req> array
 */
static int msm_rpm_fill_sel_masks(
	uint32_t *sel_masks, struct msm_rpm_iv_pair *req, int count)
{
	uint32_t sel;
	int i;

	for (i = 0; i < count; i++) {
		sel = msm_rpm_map_id_to_sel(req[i].id);

		if (sel > MSM_RPM_SEL_LAST)
			return -EINVAL;

		sel_masks[msm_rpm_get_sel_mask_reg(sel)] |=
			msm_rpm_get_sel_mask(sel);
	}

	return 0;
}

static inline void msm_rpm_send_req_interrupt(void)
{
	__raw_writel(msm_rpm_platform->msm_apps_ipc_rpm_val,
			msm_rpm_platform->msm_apps_ipc_rpm_reg);
}

/*
 * Note: assumes caller has acquired <msm_rpm_irq_lock>.
 *
 * Return value:
 *   0: request acknowledgement
 *   1: notification
 *   2: spurious interrupt
 */
static int msm_rpm_process_ack_interrupt(void)
{
	uint32_t ctx_mask_ack;
	uint32_t sel_masks_ack[MSM_RPM_SEL_MASK_SIZE];

	ctx_mask_ack = msm_rpm_read(MSM_RPM_PAGE_CTRL, MSM_RPM_CTRL_ACK_CTX_0);
	msm_rpm_read_contiguous(MSM_RPM_PAGE_CTRL,
		MSM_RPM_CTRL_ACK_SEL_0, sel_masks_ack, MSM_RPM_SEL_MASK_SIZE);

	if (ctx_mask_ack & msm_rpm_get_ctx_mask(MSM_RPM_CTX_NOTIFICATION)) {
		struct msm_rpm_notification *n;
		int i;

		list_for_each_entry(n, &msm_rpm_notifications, list)
			for (i = 0; i < MSM_RPM_SEL_MASK_SIZE; i++)
				if (sel_masks_ack[i] & n->sel_masks[i]) {
					up(&n->sem);
					break;
				}

		msm_rpm_write_contiguous_zeros(MSM_RPM_PAGE_CTRL,
			MSM_RPM_CTRL_ACK_SEL_0, MSM_RPM_SEL_MASK_SIZE);
		msm_rpm_write(MSM_RPM_PAGE_CTRL, MSM_RPM_CTRL_ACK_CTX_0, 0);
		/* Ensure the write is complete before return */
		mb();

		return 1;
	}

	if (msm_rpm_request) {
		int i;

		*(msm_rpm_request->ctx_mask_ack) = ctx_mask_ack;
		memcpy(msm_rpm_request->sel_masks_ack, sel_masks_ack,
			sizeof(sel_masks_ack));

		for (i = 0; i < msm_rpm_request->count; i++)
			msm_rpm_request->req[i].value =
				msm_rpm_read(MSM_RPM_PAGE_ACK,
						msm_rpm_request->req[i].id);

		msm_rpm_write_contiguous_zeros(MSM_RPM_PAGE_CTRL,
			MSM_RPM_CTRL_ACK_SEL_0, MSM_RPM_SEL_MASK_SIZE);
		msm_rpm_write(MSM_RPM_PAGE_CTRL, MSM_RPM_CTRL_ACK_CTX_0, 0);
		/* Ensure the write is complete before return */
		mb();

		if (msm_rpm_request->done)
			complete_all(msm_rpm_request->done);

		msm_rpm_request = NULL;
		return 0;
	}

	return 2;
}

static irqreturn_t msm_rpm_ack_interrupt(int irq, void *dev_id)
{
	unsigned long flags;
	int rc;

	if (dev_id != &msm_rpm_ack_interrupt)
		return IRQ_NONE;

	spin_lock_irqsave(&msm_rpm_irq_lock, flags);
	rc = msm_rpm_process_ack_interrupt();
	spin_unlock_irqrestore(&msm_rpm_irq_lock, flags);

	return IRQ_HANDLED;
}

/*
 * Note: assumes caller has acquired <msm_rpm_irq_lock>.
 */
static void msm_rpm_busy_wait_for_request_completion(
	bool allow_async_completion)
{
	int rc;

	do {
		while (!gic_is_spi_pending(msm_rpm_platform->irq_ack) &&
				msm_rpm_request) {
			if (allow_async_completion)
				spin_unlock(&msm_rpm_irq_lock);
			udelay(1);
			if (allow_async_completion)
				spin_lock(&msm_rpm_irq_lock);
		}

		if (!msm_rpm_request)
			break;

		rc = msm_rpm_process_ack_interrupt();
		gic_clear_spi_pending(msm_rpm_platform->irq_ack);
	} while (rc);
}

/* Upon return, the <req> array will contain values from the ack page.
 *
 * Note: assumes caller has acquired <msm_rpm_mutex>.
 *
 * Return value:
 *   0: success
 *   -ENOSPC: request rejected
 */
static int msm_rpm_set_exclusive(int ctx,
	uint32_t *sel_masks, struct msm_rpm_iv_pair *req, int count)
{
	DECLARE_COMPLETION_ONSTACK(ack);
	unsigned long flags;
	uint32_t ctx_mask = msm_rpm_get_ctx_mask(ctx);
	uint32_t ctx_mask_ack = 0;
	uint32_t sel_masks_ack[MSM_RPM_SEL_MASK_SIZE];
	int i;

	msm_rpm_request_irq_mode.req = req;
	msm_rpm_request_irq_mode.count = count;
	msm_rpm_request_irq_mode.ctx_mask_ack = &ctx_mask_ack;
	msm_rpm_request_irq_mode.sel_masks_ack = sel_masks_ack;
	msm_rpm_request_irq_mode.done = &ack;

	spin_lock_irqsave(&msm_rpm_lock, flags);
	spin_lock(&msm_rpm_irq_lock);

	BUG_ON(msm_rpm_request);
	msm_rpm_request = &msm_rpm_request_irq_mode;

	for (i = 0; i < count; i++) {
		BUG_ON(req[i].id > MSM_RPM_ID_LAST);
		msm_rpm_write(MSM_RPM_PAGE_REQ, req[i].id, req[i].value);
	}

	msm_rpm_write_contiguous(MSM_RPM_PAGE_CTRL,
		MSM_RPM_CTRL_REQ_SEL_0, sel_masks, MSM_RPM_SEL_MASK_SIZE);
	msm_rpm_write(MSM_RPM_PAGE_CTRL, MSM_RPM_CTRL_REQ_CTX_0, ctx_mask);

	/* Ensure RPM data is written before sending the interrupt */
	mb();
	msm_rpm_send_req_interrupt();

	spin_unlock(&msm_rpm_irq_lock);
	spin_unlock_irqrestore(&msm_rpm_lock, flags);

	wait_for_completion(&ack);

	BUG_ON((ctx_mask_ack & ~(msm_rpm_get_ctx_mask(MSM_RPM_CTX_REJECTED)))
		!= ctx_mask);
	BUG_ON(memcmp(sel_masks, sel_masks_ack, sizeof(sel_masks_ack)));

	return (ctx_mask_ack & msm_rpm_get_ctx_mask(MSM_RPM_CTX_REJECTED))
		? -ENOSPC : 0;
}

/* Upon return, the <req> array will contain values from the ack page.
 *
 * Note: assumes caller has acquired <msm_rpm_lock>.
 *
 * Return value:
 *   0: success
 *   -ENOSPC: request rejected
 */
static int msm_rpm_set_exclusive_noirq(int ctx,
	uint32_t *sel_masks, struct msm_rpm_iv_pair *req, int count)
{
	unsigned int irq = msm_rpm_platform->irq_ack;
	unsigned long flags;
	uint32_t ctx_mask = msm_rpm_get_ctx_mask(ctx);
	uint32_t ctx_mask_ack = 0;
	uint32_t sel_masks_ack[MSM_RPM_SEL_MASK_SIZE];
	struct irq_chip *irq_chip = NULL;
	int i;

	msm_rpm_request_poll_mode.req = req;
	msm_rpm_request_poll_mode.count = count;
	msm_rpm_request_poll_mode.ctx_mask_ack = &ctx_mask_ack;
	msm_rpm_request_poll_mode.sel_masks_ack = sel_masks_ack;
	msm_rpm_request_poll_mode.done = NULL;

	spin_lock_irqsave(&msm_rpm_irq_lock, flags);
	irq_chip = irq_get_chip(irq);
	if (!irq_chip) {
		spin_unlock_irqrestore(&msm_rpm_irq_lock, flags);
		return -ENOSPC;
	}
	irq_chip->irq_mask(irq_get_irq_data(irq));

	if (msm_rpm_request) {
		msm_rpm_busy_wait_for_request_completion(true);
		BUG_ON(msm_rpm_request);
	}

	msm_rpm_request = &msm_rpm_request_poll_mode;

	for (i = 0; i < count; i++) {
		BUG_ON(req[i].id > MSM_RPM_ID_LAST);
		msm_rpm_write(MSM_RPM_PAGE_REQ, req[i].id, req[i].value);
	}

	msm_rpm_write_contiguous(MSM_RPM_PAGE_CTRL,
		MSM_RPM_CTRL_REQ_SEL_0, sel_masks, MSM_RPM_SEL_MASK_SIZE);
	msm_rpm_write(MSM_RPM_PAGE_CTRL, MSM_RPM_CTRL_REQ_CTX_0, ctx_mask);

	/* Ensure RPM data is written before sending the interrupt */
	mb();
	msm_rpm_send_req_interrupt();

	msm_rpm_busy_wait_for_request_completion(false);
	BUG_ON(msm_rpm_request);

	irq_chip->irq_unmask(irq_get_irq_data(irq));
	spin_unlock_irqrestore(&msm_rpm_irq_lock, flags);

	BUG_ON((ctx_mask_ack & ~(msm_rpm_get_ctx_mask(MSM_RPM_CTX_REJECTED)))
		!= ctx_mask);
	BUG_ON(memcmp(sel_masks, sel_masks_ack, sizeof(sel_masks_ack)));

	return (ctx_mask_ack & msm_rpm_get_ctx_mask(MSM_RPM_CTX_REJECTED))
		? -ENOSPC : 0;
}

/* Upon return, the <req> array will contain values from the ack page.
 *
 * Return value:
 *   0: success
 *   -EINTR: interrupted
 *   -EINVAL: invalid <ctx> or invalid id in <req> array
 *   -ENOSPC: request rejected
 */
static int msm_rpm_set_common(
	int ctx, struct msm_rpm_iv_pair *req, int count, bool noirq)
{
	uint32_t sel_masks[MSM_RPM_SEL_MASK_SIZE] = {};
	int rc;

	if (ctx >= MSM_RPM_CTX_SET_COUNT) {
		rc = -EINVAL;
		goto set_common_exit;
	}

	rc = msm_rpm_fill_sel_masks(sel_masks, req, count);
	if (rc)
		goto set_common_exit;

	if (noirq) {
		unsigned long flags;

		spin_lock_irqsave(&msm_rpm_lock, flags);
		rc = msm_rpm_set_exclusive_noirq(ctx, sel_masks, req, count);
		spin_unlock_irqrestore(&msm_rpm_lock, flags);
	} else {
		rc = mutex_lock_interruptible(&msm_rpm_mutex);
		if (rc)
			goto set_common_exit;

		rc = msm_rpm_set_exclusive(ctx, sel_masks, req, count);
		mutex_unlock(&msm_rpm_mutex);
	}

set_common_exit:
	return rc;
}

/*
 * Return value:
 *   0: success
 *   -EINTR: interrupted
 *   -EINVAL: invalid <ctx> or invalid id in <req> array
 */
static int msm_rpm_clear_common(
	int ctx, struct msm_rpm_iv_pair *req, int count, bool noirq)
{
	uint32_t sel_masks[MSM_RPM_SEL_MASK_SIZE] = {};
	struct msm_rpm_iv_pair r[MSM_RPM_SEL_MASK_SIZE];
	int rc;
	int i;

	if (ctx >= MSM_RPM_CTX_SET_COUNT) {
		rc = -EINVAL;
		goto clear_common_exit;
	}

	rc = msm_rpm_fill_sel_masks(sel_masks, req, count);
	if (rc)
		goto clear_common_exit;

	for (i = 0; i < ARRAY_SIZE(r); i++) {
		r[i].id = MSM_RPM_ID_INVALIDATE_0 + i;
		r[i].value = sel_masks[i];
	}

	memset(sel_masks, 0, sizeof(sel_masks));
	sel_masks[msm_rpm_get_sel_mask_reg(MSM_RPM_SEL_INVALIDATE)] |=
		msm_rpm_get_sel_mask(MSM_RPM_SEL_INVALIDATE);

	if (noirq) {
		unsigned long flags;

		spin_lock_irqsave(&msm_rpm_lock, flags);
		rc = msm_rpm_set_exclusive_noirq(ctx, sel_masks, r,
			ARRAY_SIZE(r));
		spin_unlock_irqrestore(&msm_rpm_lock, flags);
		BUG_ON(rc);
	} else {
		rc = mutex_lock_interruptible(&msm_rpm_mutex);
		if (rc)
			goto clear_common_exit;

		rc = msm_rpm_set_exclusive(ctx, sel_masks, r, ARRAY_SIZE(r));
		mutex_unlock(&msm_rpm_mutex);
		BUG_ON(rc);
	}

clear_common_exit:
	return rc;
}

/*
 * Note: assumes caller has acquired <msm_rpm_mutex>.
 */
static void msm_rpm_update_notification(uint32_t ctx,
	struct msm_rpm_notif_config *curr_cfg,
	struct msm_rpm_notif_config *new_cfg)
{
	if (memcmp(curr_cfg, new_cfg, sizeof(*new_cfg))) {
		uint32_t sel_masks[MSM_RPM_SEL_MASK_SIZE] = {};
		int rc;

		sel_masks[msm_rpm_get_sel_mask_reg(MSM_RPM_SEL_NOTIFICATION)]
			|= msm_rpm_get_sel_mask(MSM_RPM_SEL_NOTIFICATION);

		rc = msm_rpm_set_exclusive(ctx,
			sel_masks, new_cfg->iv, ARRAY_SIZE(new_cfg->iv));
		BUG_ON(rc);

		memcpy(curr_cfg, new_cfg, sizeof(*new_cfg));
	}
}

/*
 * Note: assumes caller has acquired <msm_rpm_mutex>.
 */
static void msm_rpm_initialize_notification(void)
{
	struct msm_rpm_notif_config cfg;
	unsigned int ctx;
	int i;

	for (ctx = MSM_RPM_CTX_SET_0; ctx <= MSM_RPM_CTX_SET_SLEEP; ctx++) {
		cfg = msm_rpm_notif_cfgs[ctx];

		for (i = 0; i < MSM_RPM_SEL_MASK_SIZE; i++) {
			configured_iv(&cfg)[i].id =
				MSM_RPM_ID_NOTIFICATION_CONFIGURED_0 + i;
			configured_iv(&cfg)[i].value = ~0UL;

			registered_iv(&cfg)[i].id =
				MSM_RPM_ID_NOTIFICATION_REGISTERED_0 + i;
			registered_iv(&cfg)[i].value = 0;
		}

		msm_rpm_update_notification(ctx,
			&msm_rpm_notif_cfgs[ctx], &cfg);
	}
}

/******************************************************************************
 * Public functions
 *****************************************************************************/

void msm_rpm_print_sleep_tick(void)
{
	uint32_t *mpm_sleep_tick = (void *) (MSM_RPM_MPM_BASE + 0x24);
	pr_info("MPM_SLEEP_TICK: %llums\n", ((uint64_t)(*mpm_sleep_tick) * 1000) >> 15);
}
EXPORT_SYMBOL(msm_rpm_print_sleep_tick);

int msm_rpm_local_request_is_outstanding(void)
{
	unsigned long flags;
	int outstanding = 0;

	if (!spin_trylock_irqsave(&msm_rpm_lock, flags))
		goto local_request_is_outstanding_exit;

	if (!spin_trylock(&msm_rpm_irq_lock))
		goto local_request_is_outstanding_unlock;

	outstanding = (msm_rpm_request != NULL);
	spin_unlock(&msm_rpm_irq_lock);

local_request_is_outstanding_unlock:
	spin_unlock_irqrestore(&msm_rpm_lock, flags);

local_request_is_outstanding_exit:
	return outstanding;
}

/*
 * Read the specified status registers and return their values.
 *
 * status: array of id-value pairs.  Each <id> specifies a status register,
 *         i.e, one of MSM_RPM_STATUS_ID_xxxx.  Upon return, each <value> will
 *         contain the value of the status register.
 * count: number of id-value pairs in the array
 *
 * Return value:
 *   0: success
 *   -EBUSY: RPM is updating the status page; values across different registers
 *           may not be consistent
 *   -EINVAL: invalid id in <status> array
 */
int msm_rpm_get_status(struct msm_rpm_iv_pair *status, int count)
{
	uint32_t seq_begin;
	uint32_t seq_end;
	int rc;
	int i;

	seq_begin = msm_rpm_read(MSM_RPM_PAGE_STATUS,
				MSM_RPM_STATUS_ID_SEQUENCE);

	for (i = 0; i < count; i++) {
		if (status[i].id > MSM_RPM_STATUS_ID_LAST) {
			rc = -EINVAL;
			goto get_status_exit;
		}

		status[i].value = msm_rpm_read(MSM_RPM_PAGE_STATUS,
						status[i].id);
	}

	seq_end = msm_rpm_read(MSM_RPM_PAGE_STATUS,
				MSM_RPM_STATUS_ID_SEQUENCE);

	rc = (seq_begin != seq_end || (seq_begin & 0x01)) ? -EBUSY : 0;

get_status_exit:
	return rc;
}
EXPORT_SYMBOL(msm_rpm_get_status);

/*
 * Issue a resource request to RPM to set resource values.
 *
 * Note: the function may sleep and must be called in a task context.
 *
 * ctx: the request's context.
 *      There two contexts that a RPM driver client can use:
 *      MSM_RPM_CTX_SET_0 and MSM_RPM_CTX_SET_SLEEP.  For resource values
 *      that are intended to take effect when the CPU is active,
 *      MSM_RPM_CTX_SET_0 should be used.  For resource values that are
 *      intended to take effect when the CPU is not active,
 *      MSM_RPM_CTX_SET_SLEEP should be used.
 * req: array of id-value pairs.  Each <id> specifies a RPM resource,
 *      i.e, one of MSM_RPM_ID_xxxx.  Each <value> specifies the requested
 *      resource value.
 * count: number of id-value pairs in the array
 *
 * Return value:
 *   0: success
 *   -EINTR: interrupted
 *   -EINVAL: invalid <ctx> or invalid id in <req> array
 *   -ENOSPC: request rejected
 */
int msm_rpm_set(int ctx, struct msm_rpm_iv_pair *req, int count)
{
	return msm_rpm_set_common(ctx, req, count, false);
}
EXPORT_SYMBOL(msm_rpm_set);

/*
 * Issue a resource request to RPM to set resource values.
 *
 * Note: the function is similar to msm_rpm_set() except that it must be
 *       called with interrupts masked.  If possible, use msm_rpm_set()
 *       instead, to maximize CPU throughput.
 */
int msm_rpm_set_noirq(int ctx, struct msm_rpm_iv_pair *req, int count)
{
	WARN(!irqs_disabled(), "msm_rpm_set_noirq can only be called "
		"safely when local irqs are disabled.  Consider using "
		"msm_rpm_set or msm_rpm_set_nosleep instead.");
	return msm_rpm_set_common(ctx, req, count, true);
}
EXPORT_SYMBOL(msm_rpm_set_noirq);

/*
 * Issue a resource request to RPM to clear resource values.  Once the
 * values are cleared, the resources revert back to their default values
 * for this RPM master.
 *
 * Note: the function may sleep and must be called in a task context.
 *
 * ctx: the request's context.
 * req: array of id-value pairs.  Each <id> specifies a RPM resource,
 *      i.e, one of MSM_RPM_ID_xxxx.  <value>'s are ignored.
 * count: number of id-value pairs in the array
 *
 * Return value:
 *   0: success
 *   -EINTR: interrupted
 *   -EINVAL: invalid <ctx> or invalid id in <req> array
 */
int msm_rpm_clear(int ctx, struct msm_rpm_iv_pair *req, int count)
{
	return msm_rpm_clear_common(ctx, req, count, false);
}
EXPORT_SYMBOL(msm_rpm_clear);

/*
 * Issue a resource request to RPM to clear resource values.
 *
 * Note: the function is similar to msm_rpm_clear() except that it must be
 *       called with interrupts masked.  If possible, use msm_rpm_clear()
 *       instead, to maximize CPU throughput.
 */
int msm_rpm_clear_noirq(int ctx, struct msm_rpm_iv_pair *req, int count)
{
	WARN(!irqs_disabled(), "msm_rpm_clear_noirq can only be called "
		"safely when local irqs are disabled.  Consider using "
		"msm_rpm_clear or msm_rpm_clear_nosleep instead.");
	return msm_rpm_clear_common(ctx, req, count, true);
}
EXPORT_SYMBOL(msm_rpm_clear_noirq);

/*
 * Register for RPM notification.  When the specified resources
 * change their status on RPM, RPM sends out notifications and the
 * driver will "up" the semaphore in struct msm_rpm_notification.
 *
 * Note: the function may sleep and must be called in a task context.
 *
 *       Memory for <n> must not be freed until the notification is
 *       unregistered.  Memory for <req> can be freed after this
 *       function returns.
 *
 * n: the notifcation object.  Caller should initialize only the
 *    semaphore field.  When a notification arrives later, the
 *    semaphore will be "up"ed.
 * req: array of id-value pairs.  Each <id> specifies a status register,
 *      i.e, one of MSM_RPM_STATUS_ID_xxxx.  <value>'s are ignored.
 * count: number of id-value pairs in the array
 *
 * Return value:
 *   0: success
 *   -EINTR: interrupted
 *   -EINVAL: invalid id in <req> array
 */
int msm_rpm_register_notification(struct msm_rpm_notification *n,
	struct msm_rpm_iv_pair *req, int count)
{
	unsigned long flags;
	unsigned int ctx;
	struct msm_rpm_notif_config cfg;
	int rc;
	int i;

	INIT_LIST_HEAD(&n->list);
	rc = msm_rpm_fill_sel_masks(n->sel_masks, req, count);
	if (rc)
		goto register_notification_exit;

	rc = mutex_lock_interruptible(&msm_rpm_mutex);
	if (rc)
		goto register_notification_exit;

	if (!msm_rpm_init_notif_done) {
		msm_rpm_initialize_notification();
		msm_rpm_init_notif_done = true;
	}

	spin_lock_irqsave(&msm_rpm_irq_lock, flags);
	list_add(&n->list, &msm_rpm_notifications);
	spin_unlock_irqrestore(&msm_rpm_irq_lock, flags);

	ctx = MSM_RPM_CTX_SET_0;
	cfg = msm_rpm_notif_cfgs[ctx];

	for (i = 0; i < MSM_RPM_SEL_MASK_SIZE; i++)
		registered_iv(&cfg)[i].value |= n->sel_masks[i];

	msm_rpm_update_notification(ctx, &msm_rpm_notif_cfgs[ctx], &cfg);
	mutex_unlock(&msm_rpm_mutex);

register_notification_exit:
	return rc;
}
EXPORT_SYMBOL(msm_rpm_register_notification);

/*
 * Unregister a notification.
 *
 * Note: the function may sleep and must be called in a task context.
 *
 * n: the notifcation object that was registered previously.
 *
 * Return value:
 *   0: success
 *   -EINTR: interrupted
 */
int msm_rpm_unregister_notification(struct msm_rpm_notification *n)
{
	unsigned long flags;
	unsigned int ctx;
	struct msm_rpm_notif_config cfg;
	int rc;
	int i;

	rc = mutex_lock_interruptible(&msm_rpm_mutex);
	if (rc)
		goto unregister_notification_exit;

	ctx = MSM_RPM_CTX_SET_0;
	cfg = msm_rpm_notif_cfgs[ctx];

	for (i = 0; i < MSM_RPM_SEL_MASK_SIZE; i++)
		registered_iv(&cfg)[i].value = 0;

	spin_lock_irqsave(&msm_rpm_irq_lock, flags);
	list_del(&n->list);
	list_for_each_entry(n, &msm_rpm_notifications, list)
		for (i = 0; i < MSM_RPM_SEL_MASK_SIZE; i++)
			registered_iv(&cfg)[i].value |= n->sel_masks[i];
	spin_unlock_irqrestore(&msm_rpm_irq_lock, flags);

	msm_rpm_update_notification(ctx, &msm_rpm_notif_cfgs[ctx], &cfg);
	mutex_unlock(&msm_rpm_mutex);

unregister_notification_exit:
	return rc;
}
EXPORT_SYMBOL(msm_rpm_unregister_notification);

static uint32_t fw_major, fw_minor, fw_build;

static ssize_t driver_version_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u.%u.%u\n",
			RPM_MAJOR_VER, RPM_MINOR_VER, RPM_BUILD_VER);
}

static ssize_t fw_version_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u.%u.%u\n",
			fw_major, fw_minor, fw_build);
}

static struct kobj_attribute driver_version_attr = __ATTR_RO(driver_version);
static struct kobj_attribute fw_version_attr = __ATTR_RO(fw_version);

static struct attribute *driver_attributes[] = {
	&driver_version_attr.attr,
	&fw_version_attr.attr,
	NULL
};

static struct attribute_group driver_attr_group = {
	.attrs = driver_attributes,
};

static int __devinit msm_rpm_probe(struct platform_device *pdev)
{
	return sysfs_create_group(&pdev->dev.kobj, &driver_attr_group);
}

static int __devexit msm_rpm_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &driver_attr_group);
	return 0;
}

static struct platform_driver msm_rpm_platform_driver = {
	.probe = msm_rpm_probe,
	.remove = __devexit_p(msm_rpm_remove),
	.driver = {
		.name = "msm_rpm",
		.owner = THIS_MODULE,
	},
};

static void __init msm_rpm_populate_map(void)
{
	int i, k;

	for (i = 0; i < ARRAY_SIZE(msm_rpm_map); i++)
		msm_rpm_map[i] = MSM_RPM_SEL_LAST + 1;

	for (i = 0; i < rpm_map_data_size; i++) {
		struct msm_rpm_map_data *raw_data = &rpm_map_data[i];

		for (k = 0; k < raw_data->count; k++)
			msm_rpm_map[raw_data->id + k] = raw_data->sel;
	}
}

#ifdef CONFIG_ARCH_MSM8960
#define IMEM_DEBUG_LOC (0x2A03F7F0)
unsigned int pa_memtest_rpm;
#endif

int __init msm_rpm_init(struct msm_rpm_platform_data *data)
{
	unsigned int irq;
	int rc;
#ifdef CONFIG_ARCH_MSM8960
	int i;
#endif
	msm_rpm_platform = data;

	msm_rpm_stat_data = (stats_blob *)msm_rpm_platform->reg_base_addrs[MSM_RPM_PAGE_STAT];

#ifdef CONFIG_ARCH_MSM8960
	if (rpm_debug_enable != 0) {
		unsigned int *rpm_memtest;
		void *imem_loc = ioremap_nocache(IMEM_DEBUG_LOC, 4);
		rpm_memtest = kmalloc(1024*4, GFP_KERNEL);
		pa_memtest_rpm = __pa(rpm_memtest);
		pr_info("RPMTest address: %x\n", pa_memtest_rpm);

		for(i = 0; i < 1024; i++) {
			rpm_memtest[i] = 0xEFBEADDE;
		}

		writel(pa_memtest_rpm, imem_loc);
		iounmap(imem_loc);

		msm_rpm_stat_data->rpm_debug_mode |= RPM_DEBUG_RAM_DEBUG;
	}
	if ((get_radio_flag() & 0x8) && msm_rpm_stat_data)
		msm_rpm_stat_data->rpm_debug_mode |= RPM_DEBUG_RAM_DUMP;

	pr_info("%s : rpm_debug_mode : 0x%x\n", __func__, msm_rpm_stat_data->rpm_debug_mode);
#endif

	fw_major = msm_rpm_read(MSM_RPM_PAGE_STATUS,
					MSM_RPM_STATUS_ID_VERSION_MAJOR);
	fw_minor = msm_rpm_read(MSM_RPM_PAGE_STATUS,
					MSM_RPM_STATUS_ID_VERSION_MINOR);
	fw_build = msm_rpm_read(MSM_RPM_PAGE_STATUS,
					MSM_RPM_STATUS_ID_VERSION_BUILD);
	pr_info("%s: RPM firmware %u.%u.%u\n", __func__,
			fw_major, fw_minor, fw_build);

	if (fw_major != RPM_MAJOR_VER) {
		pr_err("%s: RPM version %u.%u.%u incompatible with "
				"this driver version %u.%u.%u\n", __func__,
				fw_major, fw_minor, fw_build,
				RPM_MAJOR_VER, RPM_MINOR_VER, RPM_BUILD_VER);
		return -EFAULT;
	}

	msm_rpm_write(MSM_RPM_PAGE_CTRL, MSM_RPM_CTRL_VERSION_MAJOR,
			RPM_MAJOR_VER);
	msm_rpm_write(MSM_RPM_PAGE_CTRL, MSM_RPM_CTRL_VERSION_MINOR,
			RPM_MINOR_VER);
	msm_rpm_write(MSM_RPM_PAGE_CTRL, MSM_RPM_CTRL_VERSION_BUILD,
			RPM_BUILD_VER);

	irq = msm_rpm_platform->irq_ack;

	rc = request_irq(irq, msm_rpm_ack_interrupt,
			IRQF_TRIGGER_RISING | IRQF_NO_SUSPEND,
			"rpm_drv", msm_rpm_ack_interrupt);
	if (rc) {
		pr_err("%s: failed to request irq %d: %d\n",
			__func__, irq, rc);
		return rc;
	}

	rc = irq_set_irq_wake(irq, 1);
	if (rc) {
		pr_err("%s: failed to set wakeup irq %u: %d\n",
			__func__, irq, rc);
		return rc;
	}

	msm_rpm_populate_map();
	msm_rpm_print_sleep_tick();
	return platform_driver_register(&msm_rpm_platform_driver);
}

#if defined(CONFIG_ARCH_MSM8X60)
void msm_rpm_dump_stat(void)
{
	int i = 0, j = 0;

	if (msm_rpm_stat_data) {
		pr_info("%s: %u, %llums, %u, %llums, 0x%x, 0x%x\n", __func__,
			msm_rpm_stat_data->stats[RPM_STAT_XO_SHUTDOWN_COUNT].value,
			((uint64_t)msm_rpm_stat_data->stats[RPM_STAT_XO_SHUTDOWN_TIME].value * 1000) >> 15,
			msm_rpm_stat_data->stats[RPM_STAT_VDD_MIN_COUNT].value,
			((uint64_t)msm_rpm_stat_data->stats[RPM_STAT_VDD_MIN_TIME].value * 1000) >> 15,
			msm_rpm_stat_data->mpm_int_status[0], msm_rpm_stat_data->mpm_int_status[1]);
		for (i = 0; i < RPM_MASTER_COUNT; i++) {
#ifdef CONFIG_ARCH_MSM8X60_LTE
			pr_info("sleep_info_m.%d - %u (%d), %llums, %d %d %d %d, 0x%x 0x%x\n", i, msm_rpm_stat_data->sleep_info_ex[i].count,
				msm_rpm_stat_data->sleep_info_ex[i].is_in_sleep_mode, ((uint64_t)msm_rpm_stat_data->sleep_info_ex[i].total_sleep_duration * 1000) >> 15,
				msm_rpm_stat_data->sleep_info[i].cxo, msm_rpm_stat_data->sleep_info[i].pxo,
				msm_rpm_stat_data->sleep_info[i].vdd_mem, msm_rpm_stat_data->sleep_info[i].vdd_dig,
				msm_rpm_stat_data->mpm_trigger[i][0], msm_rpm_stat_data->mpm_trigger[i][1]);
#else
			pr_info("sleep_info_m.%d - %llums, %llums, %d %d %d %d, 0x%x 0x%x\n", i, ((uint64_t)msm_rpm_stat_data->wake_info[i].timestamp * 1000) >> 15,
				((uint64_t)msm_rpm_stat_data->sleep_info[i].timestamp * 1000) >> 15, msm_rpm_stat_data->sleep_info[i].cxo,
				msm_rpm_stat_data->sleep_info[i].pxo, msm_rpm_stat_data->sleep_info[i].vdd_mem,
				msm_rpm_stat_data->sleep_info[i].vdd_dig, msm_rpm_stat_data->mpm_trigger[i][0],
				msm_rpm_stat_data->mpm_trigger[i][1]);
#endif
		}
		for (i = 0; i < 2; i++) {
			msm_rpm_stat_data->mpm_int_status[i] = 0;
			for (j = 0; j < RPM_MASTER_COUNT; j++)
				msm_rpm_stat_data->mpm_trigger[j][i] = 0;
		}
	}
}

void msm_rpm_set_suspend_flag(bool app_from_suspend)
{
	if (msm_rpm_stat_data)
		msm_rpm_stat_data->app_from_suspend = (!!app_from_suspend);
}

void __init msm_rpm_lpm_init(uint32_t *lpm_setting, uint32_t num)
{
	uint32_t i = 0;
	for (i = 0; i < num; i++)
		msm_rpm_write(MSM_RPM_PAGE_STAT, RPM_LPM_PM8058 + i, lpm_setting[i]);
}
#elif defined(CONFIG_ARCH_MSM8960)
void msm_rpm_dump_stat(void)
{
	int i = 0, j = 0;

	if (msm_rpm_stat_data) {
		pr_info("%s: %u, %llums, %u, %llums, 0x%x, 0x%x\n", __func__,
			msm_rpm_stat_data->stats[RPM_STAT_XO_SHUTDOWN_COUNT].value,
			((uint64_t)msm_rpm_stat_data->stats[RPM_STAT_XO_SHUTDOWN_TIME].value * 1000) >> 15,
			msm_rpm_stat_data->stats[RPM_STAT_VDD_MIN_COUNT].value,
			((uint64_t)msm_rpm_stat_data->stats[RPM_STAT_VDD_MIN_TIME].value * 1000) >> 15,
			msm_rpm_stat_data->mpm_int_status[0], msm_rpm_stat_data->mpm_int_status[1]);
		for (i = 0; i < RPM_MASTER_COUNT; i++) {
			pr_info("sleep_info_m.%d - %u (%d), %llums, %d %d %d %d, 0x%x 0x%x\n", i, msm_rpm_stat_data->sleep_info[i].count,
				msm_rpm_stat_data->sleep_info[i].is_sleep_mode, ((uint64_t)msm_rpm_stat_data->sleep_info[i].total_duration * 1000) >> 15,
				msm_rpm_stat_data->sleep_info[i].cxo, msm_rpm_stat_data->sleep_info[i].pxo,
				msm_rpm_stat_data->sleep_info[i].vdd_mem, msm_rpm_stat_data->sleep_info[i].vdd_dig,
				msm_rpm_stat_data->mpm_trigger[i][0], msm_rpm_stat_data->mpm_trigger[i][1]);
		}
		for (i = 0; i < 2; i++) {
			msm_rpm_stat_data->mpm_int_status[i] = 0;
			for (j = 0; j < RPM_MASTER_COUNT; j++)
				msm_rpm_stat_data->mpm_trigger[j][i] = 0;
		}
	}
}

void msm_rpm_set_suspend_flag(bool app_from_suspend)
{
	if (msm_rpm_stat_data) {
		if (app_from_suspend)
			msm_rpm_stat_data->rpm_debug_mode |= RPM_DEBUG_APP_FROM_SUSPEND;
		else
			msm_rpm_stat_data->rpm_debug_mode &= !RPM_DEBUG_APP_FROM_SUSPEND;
	}
}

static int __init htc_rpm_debug_parser(char *str)
{
	int val;

	val = simple_strtoul(str, NULL, 0);
	rpm_debug_enable = val;

	return 1;
}
__setup("rpm_debug.enable=", htc_rpm_debug_parser);

#else
void msm_rpm_dump_stat(void) { }
#endif

#ifdef CONFIG_ARCH_MSM8X60_LTE
int htc_get_XO_Vdd_min_info(uint32_t* XO_count, uint64_t* XO_time, uint32_t* Vddmin_count, uint64_t* Vddmin_time )
{
	if(!msm_rpm_stat_data)
		return 0;
	*XO_count = msm_rpm_stat_data->stats[RPM_STAT_XO_SHUTDOWN_COUNT].value;
	*XO_time = ((uint64_t)msm_rpm_stat_data->stats[RPM_STAT_XO_SHUTDOWN_TIME].value * 1000) >> 15;
	*Vddmin_count = msm_rpm_stat_data->stats[RPM_STAT_VDD_MIN_COUNT].value;
	*Vddmin_time = ((uint64_t)msm_rpm_stat_data->stats[RPM_STAT_VDD_MIN_TIME].value * 1000) >> 15;
	return 1;
}

void htc_get_MPSS_timestamp(uint64_t* active_set_time, uint64_t* sleep_set_time)
{
	if(!msm_rpm_stat_data)
		return;
	*active_set_time = ((uint64_t)msm_rpm_stat_data->wake_info[1].timestamp * 1000) >> 15;
	*sleep_set_time = ((uint64_t)msm_rpm_stat_data->sleep_info[1].timestamp * 1000) >> 15;
}

uint64_t htc_get_MPSS_total_sleep_time(void)
{
	uint32_t *mpm_sleep_tick;
	uint32_t total;
	if (!msm_rpm_stat_data)
		return 0;

	if (msm_rpm_stat_data->sleep_info_ex[1].is_in_sleep_mode == 1) {
		mpm_sleep_tick = (void *) (MSM_RPM_MPM_BASE + 0x24);
		total = ((*mpm_sleep_tick) - (msm_rpm_stat_data->sleep_info[1].timestamp)) + msm_rpm_stat_data->sleep_info_ex[1].total_sleep_duration;
		return ((uint64_t)total * 1000) >> 15;
	} else {
		return ((uint64_t)(msm_rpm_stat_data->sleep_info_ex[1].total_sleep_duration) * 1000) >> 15;
	}
}

void htc_get_LPASS_timestamp(uint64_t* active_set_time, uint64_t* sleep_set_time)
{
	if(!msm_rpm_stat_data)
		return;
	*active_set_time = ((uint64_t)msm_rpm_stat_data->wake_info[2].timestamp * 1000) >> 15;
	*sleep_set_time = ((uint64_t)msm_rpm_stat_data->sleep_info[2].timestamp * 1000) >> 15;
}

uint64_t htc_get_LPASS_total_sleep_time(void)
{
	uint32_t *mpm_sleep_tick;
	uint32_t total;
	if (!msm_rpm_stat_data)
		return 0;

	if (msm_rpm_stat_data->sleep_info_ex[2].is_in_sleep_mode == 1) {
		mpm_sleep_tick = (void *) (MSM_RPM_MPM_BASE + 0x24);
		total = ((*mpm_sleep_tick) - (msm_rpm_stat_data->sleep_info[2].timestamp)) + msm_rpm_stat_data->sleep_info_ex[2].total_sleep_duration;
		return ((uint64_t)total * 1000) >> 15;
	} else {
		return ((uint64_t)(msm_rpm_stat_data->sleep_info_ex[2].total_sleep_duration) * 1000) >> 15;
	}
}
#endif

