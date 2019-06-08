#ifndef __AVALON_DMA_H__
#define __AVALON_DMA_H__

#include <linux/dma-direction.h>
#include <linux/interrupt.h>
#include <linux/scatterlist.h>

#include <linux/avalon-dma-hw.h>

typedef void (*avalon_dma_xfer_callback)(void *dma_async_param);

struct avalon_dma_tx_descriptor;

struct avalon_dma {
	spinlock_t lock;
	struct device *dev;
	struct tasklet_struct tasklet;
	unsigned int irq;

	struct avalon_dma_tx_descriptor *active_desc;

	struct list_head desc_allocated;
	struct list_head desc_submitted;
	struct list_head desc_issued;
	struct list_head desc_completed;

	struct lite_dma_desc_table *lite_table_rd_cpu_virt_addr;
	struct lite_dma_desc_table *lite_table_wr_cpu_virt_addr;

	dma_addr_t lite_table_rd_bus_addr; 
	dma_addr_t lite_table_wr_bus_addr;

	int h2d_last_id;
	int d2h_last_id;

	void __iomem *regs;
};

int avalon_dma_init(struct avalon_dma *avalon_dma,
		    struct device *dev,
		    void __iomem *regs,
		    unsigned int irq);
void avalon_dma_term(struct avalon_dma *avalon_dma);

int avalon_dma_submit_xfer(struct avalon_dma *avalon_dma,
			   enum dma_data_direction direction,
			   dma_addr_t dev_addr, dma_addr_t host_addr,
			   unsigned int size,
			   avalon_dma_xfer_callback callback,
			   void *callback_param);
int avalon_dma_submit_xfer_sg(struct avalon_dma *avalon_dma,
			      enum dma_data_direction direction,
			      dma_addr_t dev_addr, struct sg_table *sg_table,
			      avalon_dma_xfer_callback callback,
			      void *callback_param);
int avalon_dma_issue_pending(struct avalon_dma *avalon_dma);

#endif

