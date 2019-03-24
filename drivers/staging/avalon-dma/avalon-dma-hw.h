#ifndef __AVALON_DMA_HW_H__
#define __AVALON_DMA_HW_H__

#define ALTERA_DMA_DESCRIPTOR_NUM		128
#define AVALON_MM_DMA_MAX_TANSFER_SIZE		(0x100000 - 0x100)

#define ALTERA_LITE_DMA_RD_RC_LOW_SRC_ADDR	0x0000
#define ALTERA_LITE_DMA_RD_RC_HIGH_SRC_ADDR	0x0004
#define ALTERA_LITE_DMA_RD_CTLR_LOW_DEST_ADDR	0x0008
#define ALTERA_LITE_DMA_RD_CTRL_HIGH_DEST_ADDR	0x000C
#define ALTERA_LITE_DMA_RD_LAST_PTR		0x0010
#define ALTERA_LITE_DMA_RD_TABLE_SIZE		0x0014
#define ALTERA_LITE_DMA_RD_CONTROL		0x0018

#define ALTERA_LITE_DMA_WR_RC_LOW_SRC_ADDR	0x0100
#define ALTERA_LITE_DMA_WR_RC_HIGH_SRC_ADDR	0x0104
#define ALTERA_LITE_DMA_WR_CTLR_LOW_DEST_ADDR	0x0108
#define ALTERA_LITE_DMA_WR_CTRL_HIGH_DEST_ADDR	0x010C
#define ALTERA_LITE_DMA_WR_LAST_PTR		0x0110
#define ALTERA_LITE_DMA_WR_TABLE_SIZE		0x0114
#define ALTERA_LITE_DMA_WR_CONTROL		0x0118

#define ALTERA_DMA_NUM_DWORDS			512

#define DESC_CTRLLER_BASE			0x00000000
#define RD_CTRL_BUF_BASE_LOW			0x80000000
#define RD_CTRL_BUF_BASE_HI			0x00000000
#define WR_CTRL_BUF_BASE_LOW			0x80002000
#define WR_CTRL_BUF_BASE_HI			0x00000000

#undef AVALON_DEBUG_HW_REGS

struct dma_descriptor {
	u32 src_addr_ldw;
	u32 src_addr_udw;
	u32 dest_addr_ldw;
	u32 dest_addr_udw;
	u32 ctl_dma_len;
	u32 reserved[3];
} __attribute__ ((packed));

struct lite_dma_desc_table {
	u32 flags[ALTERA_DMA_DESCRIPTOR_NUM];
	struct dma_descriptor descriptors[ALTERA_DMA_DESCRIPTOR_NUM];
} __attribute__ ((packed));

static inline u32 av_mm_dma_read32(void __iomem *addr, int reg)
{
	u32 ret = ioread32(addr + DESC_CTRLLER_BASE + reg);

#ifdef AVALON_DEBUG_HW_REGS
	pr_warn("%s(%d): ioread32(%p, %x) = %x", __FUNCTION__, __LINE__,
		addr, reg, ret);
#endif

	return ret;
}

static inline void av_mm_dma_write32(u32 value, void __iomem *addr, int reg)
{
#ifdef AVALON_DEBUG_HW_REGS
	pr_warn("%s(%d): iowrite32(%p, %x, %x)", __FUNCTION__, __LINE__,
		addr, reg, value);
#endif

	iowrite32(value, addr + DESC_CTRLLER_BASE + reg);
}

#endif
