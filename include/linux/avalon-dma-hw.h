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
#ifndef __AVALON_DMA_HW_H__
#define __AVALON_DMA_HW_H__

#define AVALON_DMA_DESC_NUM		128

#define AVALON_DMA_FIXUP_SIZE		0x100
#define AVALON_DMA_MAX_TANSFER_SIZE	(0x100000 - AVALON_DMA_FIXUP_SIZE)

#define AVALON_DMA_CTRL_BASE		CONFIG_AVALON_DMA_CTRL_BASE
#define AVALON_DMA_RD_CTRL_OFFSET	0x0
#define AVALON_DMA_WR_CTRL_OFFSET	0x100

#define AVALON_DMA_RD_EP_DST_LO		CONFIG_AVALON_DMA_RD_EP_DST_LO
#define AVALON_DMA_RD_EP_DST_HI		CONFIG_AVALON_DMA_RD_EP_DST_HI
#define AVALON_DMA_WR_EP_DST_LO		CONFIG_AVALON_DMA_WR_EP_DST_LO
#define AVALON_DMA_WR_EP_DST_HI		CONFIG_AVALON_DMA_WR_EP_DST_HI

#undef AVALON_DEBUG_HW_REGS

struct dma_ctrl {
	u32 rc_src_lo;
	u32 rc_src_hi;
	u32 ep_dst_lo;
	u32 ep_dst_hi;
	u32 last_ptr;
	u32 table_size;
	u32 control;
} __attribute__ ((packed));

struct dma_desc {
	u32 src_lo;
	u32 src_hi;
	u32 dst_lo;
	u32 dst_hi;
	u32 ctl_dma_len;
	u32 reserved[3];
} __attribute__ ((packed));

struct dma_desc_table {
	u32 flags[AVALON_DMA_DESC_NUM];
	struct dma_desc descs[AVALON_DMA_DESC_NUM];
} __attribute__ ((packed));

static inline u32 __av_read32(void __iomem *base,
			      size_t ctrl_off,
			      size_t reg_off)
{
	size_t offset = AVALON_DMA_CTRL_BASE + ctrl_off + reg_off;
	u32 ret = ioread32(base + offset);

#ifdef AVALON_DEBUG_HW_REGS
	pr_warn("%s(%d): ioread32(%p, %lx) = %x", __FUNCTION__, __LINE__,
		base, ctrl_off + reg_off, ret);
#endif

	return ret;
}

static inline void __av_write32(u32 value,
				void __iomem *base,
				size_t ctrl_off,
				size_t reg_off)
{
	size_t offset = AVALON_DMA_CTRL_BASE + ctrl_off + reg_off;

#ifdef AVALON_DEBUG_HW_REGS
	pr_warn("%s(%d): iowrite32(%p, %lx, %x)", __FUNCTION__, __LINE__,
		base, ctrl_off + reg_off, value);
#endif

	iowrite32(value, base + offset);
}

#define av_read32(b, o, r) \
	__av_read32(b, o, offsetof(struct dma_ctrl, r))
#define av_write32(v, b, o, r) \
	__av_write32(v, b, o, offsetof(struct dma_ctrl, r))

#endif
