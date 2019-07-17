// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 *
 * Avalon DMA engine
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>

#include "avalon-hw.h"
#include "avalon-core.h"

#define INTERRUPT_NAME		"avalon_dma"

static unsigned int dma_mask_width = 64;
module_param(dma_mask_width, uint, 0644);
MODULE_PARM_DESC(dma_mask_width, "Avalon DMA bitmask width (default: 64)");

unsigned long ctrl_base;
module_param(ctrl_base, ulong, 0644);
MODULE_PARM_DESC(ctrl_base, "Avalon DMA controller base (default: 0)");

static unsigned int rd_ep_dst_lo = 0x80000000;
module_param(rd_ep_dst_lo, uint, 0644);
MODULE_PARM_DESC(rd_ep_dst_lo,
		 "Read status and desc table low (default: 0x80000000)");

static unsigned int rd_ep_dst_hi = 0;
module_param(rd_ep_dst_hi, uint, 0644);
MODULE_PARM_DESC(rd_ep_dst_hi,
		 "Read status and desc table hi (default: 0)");

static unsigned int wr_ep_dst_lo = 0x80002000;
module_param(wr_ep_dst_lo, uint, 0644);
MODULE_PARM_DESC(wr_ep_dst_lo,
		 "Write status and desc table low (default: 0x80002000)");

static unsigned int wr_ep_dst_hi = 0;
module_param(wr_ep_dst_hi, uint, 0644);
MODULE_PARM_DESC(wr_ep_dst_hi,
		 "Write status and desc table hi (default: 0)");

static int setup_dma_descs(struct dma_desc *dma_descs,
			   struct avalon_dma_desc *desc)
{
	unsigned int seg_stop;
	unsigned int seg_set;
	int ret;

	ret = setup_descs_sg(dma_descs, 0,
			     desc->direction,
			     desc->dev_addr,
			     desc->seg, desc->nr_segs,
			     desc->seg_curr, desc->seg_off,
			     &seg_stop, &seg_set);
	if (ret < 0)
		return ret;

	if (seg_stop == desc->seg_curr) {
		desc->seg_off += seg_set;
	} else {
		desc->seg_curr = seg_stop;
		desc->seg_off = seg_set;
	}

	return ret;
}

static int start_dma_xfer(struct avalon_dma_hw *hw,
			  struct avalon_dma_desc *desc)
{
	size_t ctrl_off;
	struct __dma_desc_table *__table;
	struct dma_desc_table *table;
	u32 rc_src_hi, rc_src_lo;
	u32 ep_dst_lo, ep_dst_hi;
	int last_id, *__last_id;
	int nr_descs;

	if (desc->direction == DMA_MEM_TO_DEV) {
		__table = &hw->dma_desc_table_rd;

		ctrl_off = AVALON_DMA_RD_CTRL_OFFSET;

		ep_dst_hi = rd_ep_dst_hi;
		ep_dst_lo = rd_ep_dst_lo;

		__last_id = &hw->h2d_last_id;
	} else if (desc->direction == DMA_DEV_TO_MEM) {
		__table = &hw->dma_desc_table_wr;

		ctrl_off = AVALON_DMA_WR_CTRL_OFFSET;

		ep_dst_hi = wr_ep_dst_hi;
		ep_dst_lo = wr_ep_dst_lo;

		__last_id = &hw->d2h_last_id;
	} else {
		return -EINVAL;
	}

	table = __table->cpu_addr;
	memset(&table->flags, 0, sizeof(table->flags));

	nr_descs = setup_dma_descs(table->descs, desc);
	if (nr_descs < 0)
		return nr_descs;

	last_id = nr_descs - 1;
	*__last_id = last_id;

	rc_src_hi = (u32)(__table->dma_addr >> 32);
	rc_src_lo = (u32)__table->dma_addr;

	start_xfer(hw->regs, ctrl_off,
		   rc_src_hi, rc_src_lo,
		   ep_dst_hi, ep_dst_lo,
		   last_id);

	return 0;
}

static bool is_desc_complete(struct avalon_dma_desc *desc)
{
	if (desc->seg_curr < (desc->nr_segs - 1))
		return false;

	if (desc->seg_off < desc->seg[desc->seg_curr].dma_len)
		return false;

	return true;
}

static irqreturn_t avalon_dma_interrupt(int irq, void *dev_id)
{
	struct avalon_dma *adma = (struct avalon_dma *)dev_id;
	struct avalon_dma_chan *chan = &adma->chan;
	struct avalon_dma_hw *hw = &chan->hw;
	__le32 *rd_flags = hw->dma_desc_table_rd.cpu_addr->flags;
	__le32 *wr_flags = hw->dma_desc_table_wr.cpu_addr->flags;
	struct avalon_dma_desc *desc;
	struct virt_dma_desc *vdesc;
	bool rd_done;
	bool wr_done;

	spin_lock(&chan->vchan.lock);

	rd_done = (hw->h2d_last_id < 0);
	wr_done = (hw->d2h_last_id < 0);

	if (rd_done && wr_done) {
		spin_unlock(&chan->vchan.lock);
		return IRQ_NONE;
	}

	/*
	 * The Intel documentation claims "The Descriptor Controller
	 * writes a 1 to the done bit of the status DWORD to indicate
	 * successful completion. The Descriptor Controller also sends
	 * an MSI interrupt for the final descriptor. After receiving
	 * this MSI, host software can poll the done bit to determine
	 * status."
	 *
	 * The above could be read like MSI interrupt might be delivered
	 * before the corresponding done bit is set. But in reality it
	 * does not happen at all (or happens really rare). So put here
	 * the done bit polling, just in case.
	 */
	do {
		if (!rd_done && rd_flags[hw->h2d_last_id])
			rd_done = true;
		if (!wr_done && wr_flags[hw->d2h_last_id])
			wr_done = true;
		cpu_relax();
	} while (!rd_done || !wr_done);

	hw->h2d_last_id = -1;
	hw->d2h_last_id = -1;

	desc = chan->active_desc;

	if (is_desc_complete(desc)) {
		list_del(&desc->vdesc.node);
		vchan_cookie_complete(&desc->vdesc);

		desc->direction = DMA_NONE;

		vdesc = vchan_next_desc(&chan->vchan);
		if (vdesc) {
			desc = to_avalon_dma_desc(vdesc);
			chan->active_desc = desc;
		} else {
			chan->active_desc = NULL;
		}
	}

	if (chan->active_desc)
		start_dma_xfer(hw, desc);

	spin_unlock(&chan->vchan.lock);

	return IRQ_HANDLED;
}

static int avalon_dma_terminate_all(struct dma_chan *dma_chan)
{
	struct virt_dma_chan *vchan = to_virt_chan(dma_chan);

	vchan_free_chan_resources(vchan);

	return 0;
}

static void avalon_dma_synchronize(struct dma_chan *dma_chan)
{
	struct virt_dma_chan *vchan = to_virt_chan(dma_chan);

	vchan_synchronize(vchan);
}

static int avalon_dma_init(struct avalon_dma *adma,
			   struct device *dev,
			   void __iomem *regs,
			   unsigned int irq)
{
	struct avalon_dma_chan *chan = &adma->chan;
	struct avalon_dma_hw *hw = &chan->hw;
	int ret;

	adma->dev		= dev;
	adma->irq		= irq;

	chan->active_desc	= NULL;

	hw->regs		= regs;
	hw->h2d_last_id		= -1;
	hw->d2h_last_id		= -1;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(dma_mask_width));
	if (ret)
		return ret;

	hw->dma_desc_table_rd.cpu_addr = dma_alloc_coherent(
		dev,
		sizeof(struct dma_desc_table),
		&hw->dma_desc_table_rd.dma_addr,
		GFP_KERNEL);
	if (!hw->dma_desc_table_rd.cpu_addr)
		return -ENOMEM;

	hw->dma_desc_table_wr.cpu_addr = dma_alloc_coherent(
		dev,
		sizeof(struct dma_desc_table),
		&hw->dma_desc_table_wr.dma_addr,
		GFP_KERNEL);
	if (!hw->dma_desc_table_wr.cpu_addr) {
		ret = -ENOMEM;
		goto free_table_rd;
	}

	ret = request_irq(irq, avalon_dma_interrupt, IRQF_SHARED,
			  INTERRUPT_NAME, adma);
	if (ret)
		goto free_table_wr;

	return 0;

free_table_wr:
	dma_free_coherent(
		dev,
		sizeof(struct dma_desc_table),
		hw->dma_desc_table_wr.cpu_addr,
		hw->dma_desc_table_wr.dma_addr);

free_table_rd:
	dma_free_coherent(
		dev,
		sizeof(struct dma_desc_table),
		hw->dma_desc_table_rd.cpu_addr,
		hw->dma_desc_table_rd.dma_addr);

	return ret;
}

static void avalon_dma_term(struct avalon_dma *adma)
{
	struct avalon_dma_chan *chan = &adma->chan;
	struct avalon_dma_hw *hw = &chan->hw;
	struct device *dev = adma->dev;

	free_irq(adma->irq, adma);

	dma_free_coherent(
		dev,
		sizeof(struct dma_desc_table),
		hw->dma_desc_table_rd.cpu_addr,
		hw->dma_desc_table_rd.dma_addr);

	dma_free_coherent(
		dev,
		sizeof(struct dma_desc_table),
		hw->dma_desc_table_wr.cpu_addr,
		hw->dma_desc_table_wr.dma_addr);
}

static int avalon_dma_device_config(struct dma_chan *dma_chan,
				    struct dma_slave_config *config)
{
	struct avalon_dma_chan *chan = to_avalon_dma_chan(dma_chan);

	if (!IS_ALIGNED(config->src_addr, sizeof(u32)) ||
	    !IS_ALIGNED(config->dst_addr, sizeof(u32)))
		return -EINVAL;

	chan->src_addr = config->src_addr;
	chan->dst_addr = config->dst_addr;

	return 0;
}

static struct dma_async_tx_descriptor *
avalon_dma_prep_slave_sg(struct dma_chan *dma_chan,
			 struct scatterlist *sg, unsigned int sg_len,
			 enum dma_transfer_direction direction,
			 unsigned long flags, void *context)
{
	struct avalon_dma_chan *chan = to_avalon_dma_chan(dma_chan);
	struct avalon_dma_desc *desc;
	dma_addr_t dev_addr;
	int i;

	if (direction == DMA_MEM_TO_DEV)
		dev_addr = chan->dst_addr;
	else if (direction == DMA_DEV_TO_MEM)
		dev_addr = chan->src_addr;
	else
		return NULL;

	desc = kzalloc(struct_size(desc, seg, sg_len), GFP_NOWAIT);
	if (!desc)
		return NULL;

	desc->direction = direction;
	desc->dev_addr	= dev_addr;
	desc->seg_curr	= 0;
	desc->seg_off	= 0;
	desc->nr_segs	= sg_len;

	for (i = 0; i < sg_len; i++) {
		struct dma_segment *seg = &desc->seg[i];
		dma_addr_t dma_addr = sg_dma_address(sg);
		unsigned int dma_len = sg_dma_len(sg);

		if (!IS_ALIGNED(dma_addr, sizeof(u32)) ||
		    !IS_ALIGNED(dma_len, sizeof(u32)))
			return NULL;

		seg->dma_addr = dma_addr;
		seg->dma_len = dma_len;

		sg = sg_next(sg);
	}

	return vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);
}

static void avalon_dma_issue_pending(struct dma_chan *dma_chan)
{
	struct avalon_dma_chan *chan = to_avalon_dma_chan(dma_chan);
	struct avalon_dma_hw *hw = &chan->hw;
	struct avalon_dma_desc *desc;
	struct virt_dma_desc *vdesc;
	unsigned long flags;

	spin_lock_irqsave(&chan->vchan.lock, flags);

	if (!vchan_issue_pending(&chan->vchan))
		goto out;

	/*
	 * Do nothing if a DMA transmission is currently active.
	 * BOTH read and write status must be checked here!
	 */
	if (hw->d2h_last_id < 0 && hw->h2d_last_id < 0) {
		if (chan->active_desc)
			goto out;

		vdesc = vchan_next_desc(&chan->vchan);
		desc = to_avalon_dma_desc(vdesc);
		chan->active_desc = desc;

		if (start_dma_xfer(hw, desc))
			goto out;
	}

out:
	spin_unlock_irqrestore(&chan->vchan.lock, flags);
}

static void avalon_dma_desc_free(struct virt_dma_desc *vdesc)
{
	struct avalon_dma_desc *desc = to_avalon_dma_desc(vdesc);

	kfree(desc);
}

struct avalon_dma *avalon_dma_register(struct device *dev,
				       void __iomem *regs,
				       unsigned int irq)
{
	struct avalon_dma *adma;
	struct avalon_dma_chan *chan;
	struct dma_device *dma_dev;
	int ret;

	adma = kzalloc(sizeof(*adma), GFP_KERNEL);
	if (!adma)
		return ERR_PTR(-ENOMEM);

	ret = avalon_dma_init(adma, dev, regs, irq);
	if (ret)
		goto err;

	dev->dma_parms = &adma->dma_parms;
	dma_set_max_seg_size(dev, UINT_MAX);

	dma_dev = &adma->dma_dev;
	dma_cap_set(DMA_SLAVE, dma_dev->cap_mask);

	dma_dev->device_tx_status = dma_cookie_status;
	dma_dev->device_prep_slave_sg = avalon_dma_prep_slave_sg;
	dma_dev->device_issue_pending = avalon_dma_issue_pending;
	dma_dev->device_terminate_all = avalon_dma_terminate_all;
	dma_dev->device_synchronize = avalon_dma_synchronize;
	dma_dev->device_config = avalon_dma_device_config;

	dma_dev->dev = dev;
	dma_dev->chancnt = 1;

	dma_dev->src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	dma_dev->dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	dma_dev->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	dma_dev->residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;

	INIT_LIST_HEAD(&dma_dev->channels);

	chan = &adma->chan;
	chan->src_addr = -1;
	chan->dst_addr = -1;
	chan->vchan.desc_free = avalon_dma_desc_free;

	vchan_init(&chan->vchan, dma_dev);

	ret = dma_async_device_register(dma_dev);
	if (ret)
		goto err;

	return adma;

err:
	kfree(adma);

	return ERR_PTR(ret);
}

void avalon_dma_unregister(struct avalon_dma *adma)
{
	dmaengine_terminate_sync(&adma->chan.vchan.chan);
	dma_async_device_unregister(&adma->dma_dev);

	avalon_dma_term(adma);

	kfree(adma);
}
