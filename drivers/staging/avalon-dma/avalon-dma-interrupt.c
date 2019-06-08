#include <linux/kernel.h>
#include <linux/pci.h>

#include "avalon-dma-core.h"

static bool dma_desc_done(struct avalon_dma_tx_descriptor *desc)
{
	if (desc->type == xfer_buffer) {
		struct xfer_buffer *xfer_buffer = &desc->xfer_info.xfer_buffer;

		BUG_ON(xfer_buffer->offset > xfer_buffer->size);
		if (xfer_buffer->offset < xfer_buffer->size)
			return false;
	} else if (desc->type == xfer_sg_table) {
		struct xfer_sg_table *xfer_sgt = &desc->xfer_info.xfer_sg_table;
		struct scatterlist *sg_curr = xfer_sgt->sg_curr;
		unsigned int sg_len = sg_dma_len(sg_curr);

		if (!sg_is_last(sg_curr))
			return false;

		BUG_ON(xfer_sgt->sg_offset > sg_len);
		if (xfer_sgt->sg_offset < sg_len)
			return false;
	} else {
		BUG();
	}
 
	return true;
}

irqreturn_t avalon_dma_interrupt(int irq, void *dev_id)
{
	struct avalon_dma *avalon_dma = (struct avalon_dma*)dev_id;
	struct avalon_dma_tx_descriptor *desc;
	u32 *rd_flags = avalon_dma->lite_table_rd_cpu_virt_addr->flags;
	u32 *wr_flags = avalon_dma->lite_table_wr_cpu_virt_addr->flags;
	bool rd_done;
	bool wr_done;
	bool desc_done;

	spin_lock(&avalon_dma->lock);

	rd_done = (avalon_dma->h2d_last_id < 0);
	wr_done = (avalon_dma->d2h_last_id < 0);

	if (rd_done && wr_done) {
		spin_unlock(&avalon_dma->lock);
		return IRQ_NONE;
	}

	do {
		if (!rd_done && rd_flags[avalon_dma->h2d_last_id])
			rd_done = true;

		if (!wr_done && wr_flags[avalon_dma->d2h_last_id])
			wr_done = true;
	} while (!rd_done || !wr_done);

	avalon_dma->h2d_last_id = -1;
	avalon_dma->d2h_last_id = -1;

	BUG_ON(!avalon_dma->active_desc);
	desc = avalon_dma->active_desc;

	desc_done = dma_desc_done(desc);
	if (desc_done) {
		desc->direction = DMA_NONE;
		list_move_tail(&desc->node, &avalon_dma->desc_completed);

		if (list_empty(&avalon_dma->desc_issued)) {
			avalon_dma->active_desc = NULL;
		} else {
			desc = list_first_entry(&avalon_dma->desc_issued,
						struct avalon_dma_tx_descriptor,
						node);
			avalon_dma->active_desc = desc;
		}
	}

	if (avalon_dma->active_desc) {
		BUG_ON(desc != avalon_dma->active_desc);
		avalon_dma_start_xfer(avalon_dma, desc);
	}

	spin_unlock(&avalon_dma->lock);

	if (desc_done)
		tasklet_schedule(&avalon_dma->tasklet);

	return IRQ_HANDLED;
}

void avalon_dma_tasklet(unsigned long arg)
{
	struct avalon_dma *avalon_dma = (struct avalon_dma *)arg;
	struct avalon_dma_tx_descriptor *desc;
	LIST_HEAD(desc_completed);

	spin_lock_irq(&avalon_dma->lock);
	list_splice_tail_init(&avalon_dma->desc_completed, &desc_completed);
	spin_unlock_irq(&avalon_dma->lock);

	list_for_each_entry(desc, &desc_completed, node) {
		if (desc->callback)
			desc->callback(desc->callback_param);
	}

	spin_lock_irq(&avalon_dma->lock);
	list_splice_tail(&desc_completed, &avalon_dma->desc_allocated);
	spin_unlock_irq(&avalon_dma->lock);
}
