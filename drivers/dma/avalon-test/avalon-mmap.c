// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 *
 * Avalon DMA driver
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/dma-direction.h>

#include "avalon-dev.h"
#include "avalon-sg-buf.h"

const gfp_t gfp_flags = GFP_KERNEL;

static void avalon_drv_vm_close(struct vm_area_struct *vma)
{
	struct dma_sg_buf *sg_buf = vma->vm_private_data;

	dma_sg_buf_free(sg_buf);
}

static const struct vm_operations_struct avalon_drv_vm_ops = {
	.close	= avalon_drv_vm_close,
};

int avalon_dev_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct avalon_dev *adev = container_of(file->private_data,
		struct avalon_dev, misc_dev);
	struct device *dev = chan_to_dev(adev->dma_chan);
	unsigned long addr = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	enum dma_data_direction dir;
	struct dma_sg_buf *sg_buf;
	int ret;
	int i;

	if (!IS_ALIGNED(addr, PAGE_SIZE) || !IS_ALIGNED(size, PAGE_SIZE))
		return -EINVAL;
	if ((vma->vm_pgoff * PAGE_SIZE + size) > mem_size)
		return -EINVAL;
	if (!(((vma->vm_flags & (VM_READ | VM_WRITE)) == VM_READ) ||
	      ((vma->vm_flags & (VM_READ | VM_WRITE)) == VM_WRITE)))
		return -EINVAL;
	if (!(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	vma->vm_ops = &avalon_drv_vm_ops;

	if (vma->vm_flags & VM_WRITE)
		dir = DMA_TO_DEVICE;
	else
		dir = DMA_FROM_DEVICE;

	sg_buf = dma_sg_buf_alloc(dev, size, dir, gfp_flags);
	if (IS_ERR(sg_buf))
		return PTR_ERR(sg_buf);

	for (i = 0; size > 0; i++) {
		ret = vm_insert_page(vma, addr, sg_buf->pages[i]);
		if (ret) {
			dma_sg_buf_free(sg_buf);
			return ret;
		}

		addr += PAGE_SIZE;
		size -= PAGE_SIZE;
	};

	vma->vm_private_data = sg_buf;

	return 0;
}
