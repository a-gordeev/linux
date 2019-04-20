#ifndef __AVALON_DMA_CORE_H__
#define __AVALON_DMA_CORE_H__

#include <linux/interrupt.h>
#include <linux/dma-direction.h>

#define INTERRUPT_NAME	"avalon_dma"

struct avalon_dma;

typedef void (*avalon_dma_xfer_callback)(void *dma_async_param);

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

struct avalon_dma {
	spinlock_t lock;
	struct pci_dev *pci_dev;
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
		    unsigned int irq,
		    struct pci_dev *pci_dev);
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

int avalon_dma_start_xfer(struct avalon_dma *avalon_dma,
			  struct avalon_dma_tx_descriptor *desc);

#endif

