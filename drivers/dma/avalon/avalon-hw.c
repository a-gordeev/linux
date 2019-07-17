// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 *
 * Avalon DMA engine
 */
#include <linux/kernel.h>

#include "avalon-hw.h"

#define DMA_DESC_MAX		AVALON_DMA_DESC_NUM

static void setup_desc(struct dma_desc *desc, u32 desc_id,
		       u64 dest, u64 src, u32 size)
{
	desc->src_lo = cpu_to_le32(src & 0xfffffffful);
	desc->src_hi = cpu_to_le32((src >> 32));
	desc->dst_lo = cpu_to_le32(dest & 0xfffffffful);
	desc->dst_hi = cpu_to_le32((dest >> 32));
	desc->ctl_dma_len = cpu_to_le32((size >> 2) | (desc_id << 18));
	desc->reserved[0] = cpu_to_le32(0x0);
	desc->reserved[1] = cpu_to_le32(0x0);
	desc->reserved[2] = cpu_to_le32(0x0);
}

static
int setup_descs(struct dma_desc *descs, unsigned int desc_id,
		enum dma_data_direction direction,
		dma_addr_t dev_addr, dma_addr_t host_addr, unsigned int len,
		unsigned int *_set)
{
	int nr_descs = 0;
	unsigned int set = 0;
	dma_addr_t src;
	dma_addr_t dest;

	if (desc_id >= DMA_DESC_MAX)
		return -EINVAL;

	if (direction == DMA_TO_DEVICE) {
		src = host_addr;
		dest = dev_addr;
	} else {
		src = dev_addr;
		dest = host_addr;
	}

	while (len) {
		unsigned int xfer_len = min_t(unsigned int, len, AVALON_DMA_MAX_TANSFER_SIZE);

		setup_desc(descs, desc_id, dest, src, xfer_len);

		set += xfer_len;

		nr_descs++;
		if (nr_descs >= DMA_DESC_MAX)
			break;

		desc_id++;
		if (desc_id >= DMA_DESC_MAX)
			break;

		descs++;

		dest += xfer_len;
		src += xfer_len;

		len -= xfer_len;
	}

	*_set = set;

	return nr_descs;
}

int setup_descs_sg(struct dma_desc *descs, unsigned int desc_id,
		   enum dma_data_direction direction,
		   dma_addr_t dev_addr,
		   struct dma_segment *seg, unsigned int nr_segs,
		   unsigned int seg_start, unsigned int seg_off,
		   unsigned int *seg_stop, unsigned int *seg_set)
{
	unsigned int set = -1;
	int nr_descs = 0;
	int ret;
	int i;

	if (seg_start >= nr_segs)
		return -EINVAL;
	if ((direction != DMA_TO_DEVICE) && (direction != DMA_FROM_DEVICE))
		return -EINVAL;

	/*
	 * Skip all SGEs that have been fully transmitted.
	 */
	for (i = 0; i < seg_start; i++)
		dev_addr += seg[i].dma_len;

	/*
	 * Skip the current SGE if it has been fully transmitted.
	 */
	if (seg[i].dma_len == seg_off) {
		dev_addr += seg_off;
		seg_off = 0;
		i++;
	}

	/*
	 * Setup as many SGEs as the controller is able to transmit.
	 */
	for (; i < nr_segs; i++) {
		dma_addr_t dma_addr = seg[i].dma_addr;
		unsigned int dma_len = seg[i].dma_len;

		/*
		 * The offset can not be longer than the SGE length.
		 */
		if (dma_len < seg_off)
			return -EINVAL;

		if (seg_off) {
			dev_addr += seg_off;
			dma_addr += seg_off;
			dma_len -= seg_off;

			seg_off = 0;
		}

		ret = setup_descs(descs, desc_id, direction,
				  dev_addr, dma_addr, dma_len, &set);
		if (ret < 0)
			return ret;

		if ((desc_id + ret > DMA_DESC_MAX) ||
		    (nr_descs + ret > DMA_DESC_MAX))
			return -EINVAL;

		nr_descs += ret;
		desc_id += ret;

		/*
		 * Stop when descriptor table entries are exhausted.
		 */
		if (desc_id == DMA_DESC_MAX)
			break;

		/*
		 * The descriptor table still has free entries, thus
		 * the current SGE should have fit.
		 */
		if (dma_len != set)
			return -EINVAL;

		if (i >= nr_segs - 1)
			break;

		descs += ret;
		dev_addr += dma_len;
	}

	/*
	 * Remember the SGE that next transmission should be started from.
	 */
	if (nr_descs) {
		*seg_stop = i;
		*seg_set = set;
	} else {
		*seg_stop = seg_start;
		*seg_set = seg_off;
	}

	return nr_descs;
}

void start_xfer(void __iomem *base, size_t ctrl_off,
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
