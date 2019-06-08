#ifndef __AVALON_DMA_CORE_H__
#define __AVALON_DMA_CORE_H__

#include <linux/interrupt.h>
#include <linux/dma-direction.h>

#include <linux/avalon-dma.h>

#define INTERRUPT_NAME	"avalon_dma"

struct avalon_dma_tx_descriptor {
	struct list_head node;

	struct avalon_dma *avalon_dma;

	enum avalon_dma_xfer_desc_type {
		xfer_buffer,
		xfer_sg_table
	} type;

	enum dma_data_direction direction;

	avalon_dma_xfer_callback callback;
	void *callback_param;

	union avalon_dma_xfer_info {
		struct xfer_buffer {
			dma_addr_t dev_addr;
			dma_addr_t host_addr;
			unsigned int size;
			unsigned int offset;
		} xfer_buffer;
		struct xfer_sg_table {
			dma_addr_t dev_addr;
			struct sg_table *sg_table;
			struct scatterlist *sg_curr;
			unsigned int sg_offset;
		} xfer_sg_table;
	} xfer_info;
};

int avalon_dma_start_xfer(struct avalon_dma *avalon_dma,
			  struct avalon_dma_tx_descriptor *desc);

#endif

