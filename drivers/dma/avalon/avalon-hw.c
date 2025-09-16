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

void setup_desc(struct dma_desc *desc, u32 desc_id,
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
