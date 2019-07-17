/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Avalon DMA engine
 *
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 */
#ifndef __AVALON_DMA_UTIL_H__
#define __AVALON_DMA_UTIL_H__

#include <linux/scatterlist.h>
#include <linux/dma-direction.h>

#define DMA_DESC_MAX	AVALON_DMA_DESC_NUM

int setup_descs(struct dma_desc *descs, unsigned int desc_id,
		enum dma_data_direction direction,
		dma_addr_t dev_addr, dma_addr_t host_addr, unsigned int len,
		unsigned int *set);
int setup_descs_sg(struct dma_desc *descs, unsigned int desc_id,
		   enum dma_data_direction direction,
		   dma_addr_t dev_addr, struct sg_table *sg_table,
		   struct scatterlist *sg_start, unsigned int sg_offset,
		   struct scatterlist **sg_stop, unsigned int *sg_set);

#endif
