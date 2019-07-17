/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 *
 * Avalon DMA engine
 */
#ifndef __AVALON_HW_H__
#define __AVALON_HW_H__

#include <linux/io.h>
#include <linux/dmaengine.h>

#define AVALON_DMA_DESC_NUM		128

#define AVALON_DMA_FIXUP_SIZE		0x100
#define AVALON_DMA_MAX_TANSFER_SIZE	(0x100000 - AVALON_DMA_FIXUP_SIZE)

#define AVALON_DMA_RD_CTRL_OFFSET	0x0
#define AVALON_DMA_WR_CTRL_OFFSET	0x100

extern unsigned long ctrl_base;

static inline
u32 __av_read32(void __iomem *base, size_t ctrl_off, size_t reg_off)
{
	size_t offset = ctrl_base + ctrl_off + reg_off;

	return ioread32(base + offset);
}

static inline
void __av_write32(u32 val,
		  void __iomem *base, size_t ctrl_off, size_t reg_off)
{
	size_t offset = ctrl_base + ctrl_off + reg_off;

	iowrite32(val, base + offset);
}

#define av_read32(b, o, r) \
	__av_read32(b, o, offsetof(struct dma_ctrl, r))
#define av_write32(v, b, o, r) \
	__av_write32(v, b, o, offsetof(struct dma_ctrl, r))

struct dma_ctrl {
	__le32 rc_src_lo;
	__le32 rc_src_hi;
	__le32 ep_dst_lo;
	__le32 ep_dst_hi;
	__le32 last_ptr;
	__le32 table_size;
	__le32 control;
} __packed;

struct dma_desc {
	__le32 src_lo;
	__le32 src_hi;
	__le32 dst_lo;
	__le32 dst_hi;
	__le32 ctl_dma_len;
	__le32 reserved[3];
} __packed;

struct dma_desc_table {
	__le32 flags[AVALON_DMA_DESC_NUM];
	struct dma_desc descs[AVALON_DMA_DESC_NUM];
} __packed;

struct dma_segment {
	dma_addr_t	dma_addr;
	unsigned int	dma_len;
};

int setup_descs_sg(struct dma_desc *descs, unsigned int desc_id,
		   enum dma_transfer_direction direction,
		   dma_addr_t dev_addr,
		   struct dma_segment *seg, unsigned int nr_segs,
		   unsigned int seg_start, unsigned int sg_off,
		   unsigned int *seg_stop, unsigned int *seg_set);

void start_xfer(void __iomem *base, size_t ctrl_off,
		u32 rc_src_hi, u32 rc_src_lo,
		u32 ep_dst_hi, u32 ep_dst_lo,
		int last_id);
#endif
