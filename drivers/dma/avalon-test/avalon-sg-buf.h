/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 * Avalon DMA driver
 */
#ifndef __AVALON_SG_BUF_H__
#define __AVALON_SG_BUF_H__

struct dma_sg_buf {
	struct device			*dev;
	void				*vaddr;
	struct page			**pages;
	enum dma_data_direction		dma_dir;
	struct sg_table			sgt;
	size_t				size;
	unsigned int			num_pages;
};

struct dma_sg_buf *dma_sg_buf_alloc(struct device *dev,
				    unsigned long size,
				    enum dma_data_direction dma_dir,
				    gfp_t gfp_flags);
void dma_sg_buf_free(struct dma_sg_buf *buf);

#endif
