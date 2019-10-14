// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 *
 * Avalon DMA driver
 */
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include "avalon-sg-buf.h"

static int dma_sg_alloc_compacted(struct dma_sg_buf *buf, gfp_t gfp_flags)
{
	unsigned int last_page = 0;
	int size = buf->size;

	while (size > 0) {
		struct page *pages;
		int order;
		int i;

		order = get_order(size);
		/* Dont over allocate*/
		if ((PAGE_SIZE << order) > size)
			order--;

		pages = NULL;
		while (!pages) {
			pages = alloc_pages(gfp_flags | __GFP_NOWARN, order);
			if (pages)
				break;

			if (order == 0) {
				while (last_page--)
					__free_page(buf->pages[last_page]);
				return -ENOMEM;
			}
			order--;
		}

		split_page(pages, order);
		for (i = 0; i < (1 << order); i++)
			buf->pages[last_page++] = &pages[i];

		size -= PAGE_SIZE << order;
	}

	return 0;
}

struct dma_sg_buf *dma_sg_buf_alloc(struct device *dev,
				    unsigned long size,
				    enum dma_data_direction dma_dir,
				    gfp_t gfp_flags)
{
	struct dma_sg_buf *buf;
	struct sg_table *sgt;
	int ret;
	int num_pages;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->dma_dir = dma_dir;
	buf->size = size;
	/* size is already page aligned */
	buf->num_pages = size >> PAGE_SHIFT;

	buf->pages = kvcalloc(buf->num_pages, sizeof(struct page *), GFP_KERNEL);
	if (!buf->pages)
		goto free_buf;

	ret = dma_sg_alloc_compacted(buf, gfp_flags);
	if (ret)
		goto free_arr;

	ret = sg_alloc_table_from_pages(&buf->sgt, buf->pages,
					buf->num_pages, 0, size,
					GFP_KERNEL);
	if (ret)
		goto free_pages;

	buf->dev = get_device(dev);

	sgt = &buf->sgt;

	sgt->nents = dma_map_sg_attrs(buf->dev, sgt->sgl, sgt->orig_nents,
				      buf->dma_dir, DMA_ATTR_SKIP_CPU_SYNC);
	if (!sgt->nents)
		goto free_sgt;

	buf->vaddr = vm_map_ram(buf->pages, buf->num_pages, -1, PAGE_KERNEL);
	if (!buf->vaddr)
		goto unmap_sg;

	return buf;

unmap_sg:
	dma_unmap_sg_attrs(buf->dev, sgt->sgl, sgt->orig_nents,
			   buf->dma_dir, DMA_ATTR_SKIP_CPU_SYNC);
free_sgt:
	put_device(buf->dev);
	sg_free_table(&buf->sgt);
free_pages:
	num_pages = buf->num_pages;
	while (num_pages--)
		__free_page(buf->pages[num_pages]);
free_arr:
	kvfree(buf->pages);
free_buf:
	kfree(buf);

	return ERR_PTR(-ENOMEM);
}

void dma_sg_buf_free(struct dma_sg_buf *buf)
{
	struct sg_table *sgt = &buf->sgt;
	int i = buf->num_pages;

	dma_unmap_sg_attrs(buf->dev, sgt->sgl, sgt->orig_nents,
			   buf->dma_dir, DMA_ATTR_SKIP_CPU_SYNC);
	vm_unmap_ram(buf->vaddr, buf->num_pages);
	sg_free_table(&buf->sgt);
	while (--i >= 0)
		__free_page(buf->pages[i]);
	kvfree(buf->pages);
	put_device(buf->dev);
	kfree(buf);
}
