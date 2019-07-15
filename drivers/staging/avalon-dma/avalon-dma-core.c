// SPDX-License-Identifier: GPL-2.0
/*
 * Avalon DMA engine
 *
 * Copyright (C) 2018-2019 DAQRI, http://www.daqri.com/
 *
 * Created by Alexander Gordeev <alexander.gordeev@daqri.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of the
 * Linux distribution for more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include <linux/avalon-dma.h>

#include "avalon-dma-core.h"
#include "avalon-dma-util.h"
#include "avalon-dma-interrupt.h"

#define AVALON_DMA_DESC_ALLOC
#define AVALON_DMA_DESC_COUNT	0

static struct avalon_dma_tx_descriptor *__alloc_desc(gfp_t flags)
{
	struct avalon_dma_tx_descriptor *desc;

	desc = kzalloc(sizeof(*desc), flags);
	if (!desc)
		return NULL;

	INIT_LIST_HEAD(&desc->node);
	desc->direction = DMA_NONE;

	return desc;
}

static void free_descs(struct list_head *descs)
{
	struct avalon_dma_tx_descriptor *desc;
	struct list_head *node, *tmp;

	list_for_each_safe(node, tmp, descs) {
		desc = list_entry(node, struct avalon_dma_tx_descriptor, node);
		list_del(node);

		kfree(desc);
	}
}

static int alloc_descs(struct list_head *descs, int nr_descs)
{
	struct avalon_dma_tx_descriptor *desc;
	int i;

	for (i = 0; i < nr_descs; i++) {
		desc = __alloc_desc(GFP_KERNEL);
		if (!desc) {
			free_descs(descs);
			return -ENOMEM;
		}
		list_add(&desc->node, descs);
	}

	return 0;
}

#ifdef AVALON_DMA_DESC_ALLOC
struct avalon_dma_tx_descriptor *get_desc_locked(spinlock_t *lock,
						 struct list_head *descs)
{
	struct avalon_dma_tx_descriptor *desc;

	assert_spin_locked(lock);

	if (unlikely(list_empty(descs))) {
		gfp_t gfp_flags = GFP_KERNEL;

		if (WARN_ON(in_interrupt()))
			gfp_flags |= GFP_ATOMIC;

		desc = __alloc_desc(gfp_flags);
		if (!desc)
			return NULL;

		list_add(&desc->node, descs);
	} else {
		desc = list_first_entry(descs,
					struct avalon_dma_tx_descriptor,
					node);
	}

	return desc;
}
#else
struct avalon_dma_tx_descriptor *get_desc_locked(spinlock_t *lock,
						 struct list_head *descs)
{
	assert_spin_locked(lock);

	if (unlikely(list_empty(descs)))
		return NULL;

	return list_first_entry(descs, struct avalon_dma_tx_descriptor, node);
}
#endif

int avalon_dma_init(struct avalon_dma *avalon_dma,
		    struct device *dev,
		    void __iomem *regs,
		    unsigned int irq)
{
	int ret;

	memset(avalon_dma, 0, sizeof(*avalon_dma));

	spin_lock_init(&avalon_dma->lock);

	avalon_dma->dev		= dev;
	avalon_dma->regs	= regs;
	avalon_dma->irq		= irq;

	avalon_dma->active_desc	= NULL;

	avalon_dma->h2d_last_id = -1;
	avalon_dma->d2h_last_id = -1;

	INIT_LIST_HEAD(&avalon_dma->desc_allocated);
	INIT_LIST_HEAD(&avalon_dma->desc_submitted);
	INIT_LIST_HEAD(&avalon_dma->desc_issued);
	INIT_LIST_HEAD(&avalon_dma->desc_completed);

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret)
		goto dma_set_mask_err;

	ret = alloc_descs(&avalon_dma->desc_allocated,
			  AVALON_DMA_DESC_COUNT);
	if (ret)
		goto alloc_descs_err;

	avalon_dma->dma_desc_table_rd.cpu_addr = dma_alloc_coherent(
		dev,
		sizeof(struct dma_desc_table),
		&avalon_dma->dma_desc_table_rd.dma_addr,
		GFP_KERNEL);
	if (!avalon_dma->dma_desc_table_rd.cpu_addr) {
		ret = -ENOMEM;
		goto alloc_rd_dma_table_err;
	}

	avalon_dma->dma_desc_table_wr.cpu_addr = dma_alloc_coherent(
		dev,
		sizeof(struct dma_desc_table),
		&avalon_dma->dma_desc_table_wr.dma_addr,
		GFP_KERNEL);
	if (!avalon_dma->dma_desc_table_wr.cpu_addr) {
		ret = -ENOMEM;
		goto alloc_wr_dma_table_err;
	}

	tasklet_init(&avalon_dma->tasklet,
		     avalon_dma_tasklet, (unsigned long)avalon_dma);

	ret = request_irq(irq, avalon_dma_interrupt, IRQF_SHARED,
			  INTERRUPT_NAME, avalon_dma);
	if (ret)
		goto req_irq_err;

	return 0;

req_irq_err:
	tasklet_kill(&avalon_dma->tasklet);

	dma_free_coherent(
		dev,
		sizeof(struct dma_desc_table),
		avalon_dma->dma_desc_table_wr.cpu_addr,
		avalon_dma->dma_desc_table_wr.dma_addr);

alloc_wr_dma_table_err:
	dma_free_coherent(
		dev,
		sizeof(struct dma_desc_table),
		avalon_dma->dma_desc_table_rd.cpu_addr,
		avalon_dma->dma_desc_table_rd.dma_addr);

alloc_rd_dma_table_err:
	free_descs(&avalon_dma->desc_allocated);

alloc_descs_err:
dma_set_mask_err:
	return ret;
}
EXPORT_SYMBOL_GPL(avalon_dma_init);

static void avalon_dma_sync(struct avalon_dma *avalon_dma)
{
	struct list_head *head = &avalon_dma->desc_allocated;
 	struct avalon_dma_tx_descriptor *desc;
	int nr_retries = 0;
	unsigned long flags;

	/*
	 * FIXME Implement graceful race-free completion
	 */
again:
	synchronize_irq(avalon_dma->irq);

	spin_lock_irqsave(&avalon_dma->lock, flags);

	if (!list_empty(&avalon_dma->desc_submitted) ||
	    !list_empty(&avalon_dma->desc_issued) ||
	    !list_empty(&avalon_dma->desc_completed)) {

		spin_unlock_irqrestore(&avalon_dma->lock, flags);

		msleep(250);
		nr_retries++;

		goto again;
	}

	BUG_ON(avalon_dma->active_desc);

	list_splice_tail_init(&avalon_dma->desc_submitted, head);
	list_splice_tail_init(&avalon_dma->desc_issued, head);
	list_splice_tail_init(&avalon_dma->desc_completed, head);

	list_for_each_entry(desc, head, node)
		desc->direction = DMA_NONE;

	spin_unlock_irqrestore(&avalon_dma->lock, flags);

	WARN_ON_ONCE(nr_retries);
}

void avalon_dma_term(struct avalon_dma *avalon_dma)
{
	struct device *dev = avalon_dma->dev;

	avalon_dma_sync(avalon_dma);

	free_irq(avalon_dma->irq, (void*)avalon_dma);
	tasklet_kill(&avalon_dma->tasklet);

	dma_free_coherent(
		dev,
		sizeof(struct dma_desc_table),
		avalon_dma->dma_desc_table_rd.cpu_addr,
		avalon_dma->dma_desc_table_rd.dma_addr);

	dma_free_coherent(
		dev,
		sizeof(struct dma_desc_table),
		avalon_dma->dma_desc_table_wr.cpu_addr,
		avalon_dma->dma_desc_table_wr.dma_addr);

	free_descs(&avalon_dma->desc_allocated);

	iounmap(avalon_dma->regs);
}
EXPORT_SYMBOL_GPL(avalon_dma_term);

static int submit_xfer(struct avalon_dma *avalon_dma,
		       enum avalon_dma_xfer_desc_type type,
		       enum dma_data_direction direction,
		       union avalon_dma_xfer_info *xfer_info,
		       avalon_dma_xfer_callback callback,
		       void *callback_param)
{
	struct avalon_dma_tx_descriptor *desc;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&avalon_dma->lock, flags);

	desc = get_desc_locked(&avalon_dma->lock, &avalon_dma->desc_allocated);
	if (WARN_ON(!desc)) {
		spin_unlock_irqrestore(&avalon_dma->lock, flags);
		return -EBUSY;
	}

	desc->avalon_dma = avalon_dma;
	desc->type = type;
	desc->direction = direction;
	desc->callback = callback;
	desc->callback_param = callback_param;

	if (type == xfer_buffer)
		desc->xfer_info.xfer_buffer = xfer_info->xfer_buffer;
	else if (type == xfer_sg_table)
		desc->xfer_info.xfer_sg_table = xfer_info->xfer_sg_table;
	else
		BUG();

	list_move_tail(&desc->node, &avalon_dma->desc_submitted);

	spin_unlock_irqrestore(&avalon_dma->lock, flags);

	return ret;
}

int avalon_dma_issue_pending(struct avalon_dma *avalon_dma)
{
	struct avalon_dma_tx_descriptor *desc;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&avalon_dma->lock, flags);

	if (WARN_ON(list_empty(&avalon_dma->desc_submitted))) {
		ret = -ENOENT;
		goto err;
	}

	list_splice_tail_init(&avalon_dma->desc_submitted,
			      &avalon_dma->desc_issued);

	/*
	 * We must check BOTH read and write status here!
	 */
	if (avalon_dma->d2h_last_id < 0 && avalon_dma->h2d_last_id < 0) {
		BUG_ON(avalon_dma->active_desc);

		desc = list_first_entry(&avalon_dma->desc_issued,
					struct avalon_dma_tx_descriptor,
					node);

		ret = avalon_dma_start_xfer(avalon_dma, desc);
		if (ret)
			goto err;

		avalon_dma->active_desc = desc;
	} else {
		BUG_ON(!avalon_dma->active_desc);
	}

err:
	spin_unlock_irqrestore(&avalon_dma->lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(avalon_dma_issue_pending);

int avalon_dma_submit_xfer(struct avalon_dma *avalon_dma,
			   enum dma_data_direction direction,
			   dma_addr_t dev_addr,
			   dma_addr_t host_addr,
			   unsigned int size,
			   avalon_dma_xfer_callback callback,
			   void *callback_param)
{
	union avalon_dma_xfer_info xi;

	xi.xfer_buffer.dev_addr = dev_addr;
	xi.xfer_buffer.host_addr = host_addr;
	xi.xfer_buffer.size = size;
	xi.xfer_buffer.offset = 0;

	return submit_xfer(avalon_dma, xfer_buffer, direction, &xi,
			   callback, callback_param);
}
EXPORT_SYMBOL_GPL(avalon_dma_submit_xfer);

int avalon_dma_submit_xfer_sg(struct avalon_dma *avalon_dma,
			      enum dma_data_direction direction,
			      dma_addr_t dev_addr,
			      struct sg_table *sg_table,
			      avalon_dma_xfer_callback callback,
			      void *callback_param)
{
	union avalon_dma_xfer_info xi;

	xi.xfer_sg_table.dev_addr = dev_addr;
	xi.xfer_sg_table.sg_table = sg_table;
	xi.xfer_sg_table.sg_curr = sg_table->sgl;
	xi.xfer_sg_table.sg_offset = 0;

	return submit_xfer(avalon_dma, xfer_sg_table, direction, &xi,
			   callback, callback_param);
}
EXPORT_SYMBOL_GPL(avalon_dma_submit_xfer_sg);

static int setup_dma_descs_buf(struct dma_desc *dma_descs,
			       struct avalon_dma_tx_descriptor *desc)
{
	struct xfer_buffer *xfer_buffer = &desc->xfer_info.xfer_buffer;
	unsigned int offset = xfer_buffer->offset;
	unsigned int size = xfer_buffer->size - offset;
	dma_addr_t dev_addr = xfer_buffer->dev_addr + offset;
	dma_addr_t host_addr = xfer_buffer->host_addr + offset;
	unsigned int set;
	int ret;

	BUG_ON(size > xfer_buffer->size);
	ret = setup_descs(dma_descs, 0, desc->direction,
			  dev_addr, host_addr, size, &set);
	BUG_ON(!ret);
	if (ret > 0)
		xfer_buffer->offset += set;

	return ret;
}

static int setup_dma_descs_sg(struct dma_desc *dma_descs,
			      struct avalon_dma_tx_descriptor *desc)
{
	struct xfer_sg_table *xfer_sg_table = &desc->xfer_info.xfer_sg_table;
	struct scatterlist *sg_stop;
	unsigned int sg_set;
	int ret;

	ret = setup_descs_sg(dma_descs, 0,
			     desc->direction,
			     xfer_sg_table->dev_addr, xfer_sg_table->sg_table,
			     xfer_sg_table->sg_curr, xfer_sg_table->sg_offset,
			     &sg_stop, &sg_set);
	BUG_ON(!ret);
	if (ret > 0) {
		if (sg_stop == xfer_sg_table->sg_curr)
			xfer_sg_table->sg_offset += sg_set;
		else {
			xfer_sg_table->sg_curr = sg_stop;
			xfer_sg_table->sg_offset = sg_set;
		}
	}

	return ret;
}

static int setup_dma_descs(struct dma_desc *dma_descs,
			   struct avalon_dma_tx_descriptor *desc)
{
	int ret;

	if (desc->type == xfer_buffer) {
		ret = setup_dma_descs_buf(dma_descs, desc);
	} else if (desc->type == xfer_sg_table) {
		ret = setup_dma_descs_sg(dma_descs, desc);
	} else {
		BUG();
		ret = -ENOSYS;
	}

	return ret;
}

static void start_xfer(void __iomem *base, size_t ctrl_off,
		       u32 rc_src_hi, u32 rc_src_lo,
		       u32 ep_dst_hi, u32 ep_dst_lo,
		       int last_id)
{
	av_write32(rc_src_hi, base, ctrl_off, rc_src_hi);
	av_write32(rc_src_lo, base, ctrl_off, rc_src_lo);
	av_write32(ep_dst_hi, base, ctrl_off, ep_dst_hi);
	av_write32(ep_dst_lo, base, ctrl_off, ep_dst_lo);
	av_write32(last_id, base, ctrl_off, table_size);
	av_write32(last_id, base, ctrl_off, last_ptr);
}

int avalon_dma_start_xfer(struct avalon_dma *avalon_dma,
			  struct avalon_dma_tx_descriptor *desc)
{
	size_t ctrl_off;
	struct __dma_desc_table *__table;
	struct dma_desc_table *table;
	u32 rc_src_hi, rc_src_lo;
	u32 ep_dst_lo, ep_dst_hi;
	int last_id, *__last_id;
	int nr_descs;

	if (desc->direction == DMA_TO_DEVICE) {
		__table = &avalon_dma->dma_desc_table_rd;

		ctrl_off = AVALON_DMA_RD_CTRL_OFFSET;

		ep_dst_hi = AVALON_DMA_RD_EP_DST_HI;
		ep_dst_lo = AVALON_DMA_RD_EP_DST_LO;

		__last_id = &avalon_dma->h2d_last_id;
	} else if (desc->direction == DMA_FROM_DEVICE) {
		__table = &avalon_dma->dma_desc_table_wr;

		ctrl_off = AVALON_DMA_WR_CTRL_OFFSET;

		ep_dst_hi = AVALON_DMA_WR_EP_DST_HI;
		ep_dst_lo = AVALON_DMA_WR_EP_DST_LO;

		__last_id = &avalon_dma->d2h_last_id;
	} else {
		BUG();
	}

	table = __table->cpu_addr;
	memset(&table->flags, 0, sizeof(table->flags));

	nr_descs = setup_dma_descs(table->descs, desc);
	if (WARN_ON(nr_descs < 1))
		return nr_descs;

	last_id = nr_descs - 1;
	*__last_id = last_id;

	rc_src_hi = __table->dma_addr >> 32;
	rc_src_lo = (u32)__table->dma_addr;

	start_xfer(avalon_dma->regs, ctrl_off,
		   rc_src_hi, rc_src_lo,
		   ep_dst_hi, ep_dst_lo,
		   last_id);

	return 0;
}

MODULE_AUTHOR("Alexander Gordeev <alexander.gordeev@daqri.com>");
MODULE_DESCRIPTION("Avalon DMA engine driver");
MODULE_LICENSE("GPL v2");
