#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include "avalon-drv-util.h"

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

	buf = kzalloc(sizeof *buf, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->dma_dir = dma_dir;
	buf->size = size;
	/* size is already page aligned */
	buf->num_pages = size >> PAGE_SHIFT;
	buf->dma_sgt = &buf->sg_table;

	buf->pages = kvmalloc_array(buf->num_pages, sizeof(struct page *),
				    GFP_KERNEL | __GFP_ZERO);
	if (!buf->pages)
		goto fail_pages_array_alloc;

	ret = dma_sg_alloc_compacted(buf, gfp_flags);
	if (ret)
		goto fail_pages_alloc;

	ret = sg_alloc_table_from_pages(buf->dma_sgt, buf->pages,
			buf->num_pages, 0, size, GFP_KERNEL);
	if (ret)
		goto fail_table_alloc;

	buf->dev = get_device(dev);

	sgt = &buf->sg_table;

	sgt->nents = dma_map_sg_attrs(buf->dev, sgt->sgl, sgt->orig_nents,
				      buf->dma_dir, DMA_ATTR_SKIP_CPU_SYNC);
	if (!sgt->nents)
		goto fail_map;

	buf->vaddr = vm_map_ram(buf->pages, buf->num_pages, -1, PAGE_KERNEL);
	if (!buf->vaddr)
		goto fail_vm_map;

	return buf;

fail_vm_map:
	dma_unmap_sg_attrs(buf->dev, sgt->sgl, sgt->orig_nents,
			   buf->dma_dir, DMA_ATTR_SKIP_CPU_SYNC);
fail_map:
	put_device(buf->dev);
	sg_free_table(buf->dma_sgt);
fail_table_alloc:
	num_pages = buf->num_pages;
	while (num_pages--)
		__free_page(buf->pages[num_pages]);
fail_pages_alloc:
	kvfree(buf->pages);
fail_pages_array_alloc:
	kfree(buf);
	return ERR_PTR(-ENOMEM);
}

void dma_sg_buf_free(struct dma_sg_buf *buf)
{
	struct sg_table *sgt = &buf->sg_table;
	int i = buf->num_pages;

	dma_unmap_sg_attrs(buf->dev, sgt->sgl, sgt->orig_nents,
			   buf->dma_dir, DMA_ATTR_SKIP_CPU_SYNC);
	vm_unmap_ram(buf->vaddr, buf->num_pages);
	sg_free_table(buf->dma_sgt);
	while (--i >= 0)
		__free_page(buf->pages[i]);
	kvfree(buf->pages);
	put_device(buf->dev);
	kfree(buf);
}
