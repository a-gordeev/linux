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
#ifndef __AVALON_DMA_H__
#define __AVALON_DMA_H__

#include <linux/dma-direction.h>
#include <linux/interrupt.h>
#include <linux/scatterlist.h>

#include <linux/avalon-dma-hw.h>

typedef void (*avalon_dma_xfer_callback)(void *dma_async_param);

struct avalon_dma_tx_desc;

struct avalon_dma {
	spinlock_t lock;
	struct device *dev;
	struct tasklet_struct tasklet;
	unsigned int irq;

	struct avalon_dma_tx_desc *active_desc;

	struct list_head desc_allocated;
	struct list_head desc_submitted;
	struct list_head desc_issued;
	struct list_head desc_completed;

	struct __dma_desc_table {
		struct dma_desc_table *cpu_addr;
		dma_addr_t dma_addr;
	} dma_desc_table_rd, dma_desc_table_wr;

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

#define TARGET_MEM_BASE		CONFIG_AVALON_DMA_TARGET_BASE
#define TARGET_MEM_SIZE		CONFIG_AVALON_DMA_TARGET_SIZE

#endif
