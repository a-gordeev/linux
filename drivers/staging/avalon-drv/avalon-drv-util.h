// SPDX-License-Identifier: GPL-2.0
/*
 * Avalon DMA driver
 *
 * Copyright (C) 2018-2019 DAQRI, http://www.daqri.com/
 *
 * Created by Alexander Gordeev <alexander.gordeev@daqri.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of the
 * Linux distribution for more details.
 */
#ifndef __AVALON_DRV_UTIL_H__
#define __AVALON_DRV_UTIL_H__

struct dma_sg_buf {
	struct device			*dev;
	void				*vaddr;
	struct page			**pages;
	enum dma_data_direction		dma_dir;
	struct sg_table			sg_table;
	struct sg_table			*dma_sgt;	/* redundant */
	size_t				size;
	unsigned int			num_pages;
};

struct dma_sg_buf *dma_sg_buf_alloc(struct device *dev,
				    unsigned long size,
				    enum dma_data_direction dma_dir,
				    gfp_t gfp_flags);
void dma_sg_buf_free(struct dma_sg_buf *buf);

void dump_mem(struct device *dev, void *data, size_t len);

#endif
