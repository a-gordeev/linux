// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>

#include "avalon-hw.h"

#define DMA_DESC_MAX		AVALON_DMA_DESC_NUM

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
