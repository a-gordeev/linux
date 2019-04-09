#ifndef __AVALON_DMA_HW_H__
#define __AVALON_DMA_HW_H__

#define ALTERA_DMA_DESCRIPTOR_NUM		128
#define AVALON_MM_DMA_MAX_TANSFER_SIZE		(0x100000 - 0x100)

#define RD_CTRL_OFFSET				0x0
#define WR_CTRL_OFFSET				0x100

#define ALTERA_DMA_NUM_DWORDS			512

#define DESC_CTRLLER_BASE			0x00000000
#define RD_CTRL_BUF_BASE_LOW			0x80000000
#define RD_CTRL_BUF_BASE_HI			0x00000000
#define WR_CTRL_BUF_BASE_LOW			0x80002000
#define WR_CTRL_BUF_BASE_HI			0x00000000

#undef AVALON_DEBUG_HW_REGS

struct dma_controller {
	u32 rc_low_src_addr;
	u32 rc_high_src_addr;
	u32 ctlr_low_dest_addr;
	u32 ctrl_high_dest_addr;
	u32 last_ptr;
	u32 table_size;
	u32 control;
} __attribute__ ((packed));

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

static inline
u32 __av_rd(void __iomem *addr, size_t ctrl_off, size_t reg_off)
{
	size_t offset = DESC_CTRLLER_BASE + ctrl_off + reg_off;
	u32 ret = ioread32(addr + offset);

#ifdef AVALON_DEBUG_HW_REGS
	pr_warn("%s(%d): ioread32(%p, %lx) = %x", __FUNCTION__, __LINE__,
		addr, ctrl_off + reg_off, ret);
#endif

	return ret;
}

static inline
void __av_wr(u32 value, void __iomem *addr, size_t ctrl_off, size_t reg_off)
{
	size_t offset = DESC_CTRLLER_BASE + ctrl_off + reg_off;

#ifdef AVALON_DEBUG_HW_REGS
	pr_warn("%s(%d): iowrite32(%p, %lx, %x)", __FUNCTION__, __LINE__,
		addr, ctrl_off + reg_off, value);
#endif

	iowrite32(value, addr + offset);
}

#define av_rd_ctrl_read32(a, r) \
	__av_rd(a, RD_CTRL_OFFSET, offsetof(struct dma_controller, r))
#define av_rd_ctrl_write32(v, a, r) \
	__av_wr(v, a, RD_CTRL_OFFSET, offsetof(struct dma_controller, r))
#define av_wr_ctrl_read32(a, r) \
	__av_rd(a, WR_CTRL_OFFSET, offsetof(struct dma_controller, r))
#define av_wr_ctrl_write32(v, a, r) \
	__av_wr(v, a, WR_CTRL_OFFSET, offsetof(struct dma_controller, r))

#endif
