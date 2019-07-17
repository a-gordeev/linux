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
#ifndef __AVALON_DMA_CORE_H__
#define __AVALON_DMA_CORE_H__

#include <linux/interrupt.h>
#include <linux/dma-direction.h>

#include <linux/avalon-dma.h>

#define INTERRUPT_NAME	"avalon_dma"

struct avalon_dma_tx_desc {
	struct list_head node;

	struct avalon_dma *avalon_dma;

	enum avalon_dma_xfer_desc_type {
		xfer_buf,
		xfer_sgt
	} type;

	enum dma_data_direction direction;

	avalon_dma_xfer_callback callback;
	void *callback_param;

	union avalon_dma_xfer_info {
		struct xfer_buf {
			dma_addr_t dev_addr;
			dma_addr_t host_addr;
			unsigned int size;
			unsigned int offset;
		} xfer_buf;
		struct xfer_sgt {
			dma_addr_t dev_addr;
			struct sg_table *sg_table;
			struct scatterlist *sg_curr;
			unsigned int sg_offset;
		} xfer_sgt;
	} xfer_info;
};

int avalon_dma_start_xfer(struct avalon_dma *avalon_dma,
			  struct avalon_dma_tx_desc *desc);

#endif

