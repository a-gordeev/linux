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
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/pci.h>

#include "avalon-drv.h"
#include "avalon-drv-sg-buf.h"

const gfp_t gfp_flags = GFP_KERNEL;

static void avalon_drv_vm_close(struct vm_area_struct *vma)
{
	struct dma_sg_buf *sg_buf = vma->vm_private_data;
	struct device *dev = sg_buf->dev;

	dev_info(dev, "%s(%d) vma %px sg_buf %px",
		 __FUNCTION__, __LINE__, vma, sg_buf);

	dma_sg_buf_free(sg_buf);
}

static const struct vm_operations_struct avalon_drv_vm_ops = {
	.close	= avalon_drv_vm_close,
};

int avalon_dev_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct avalon_dev *avalon_dev = container_of(file->private_data,
		struct avalon_dev, misc_dev);
	struct device *dev = &avalon_dev->pci_dev->dev;
	unsigned long addr = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	enum dma_data_direction dir;
	struct dma_sg_buf *sg_buf;
	int ret;
	int i;

	dev_info(dev, "%s(%d) { vm_pgoff %08lx vm_flags %08lx, size %lu",
		 __FUNCTION__, __LINE__,
		 vma->vm_pgoff, vma->vm_flags, size);

	if (!(IS_ALIGNED(addr, PAGE_SIZE) && IS_ALIGNED(size, PAGE_SIZE)))
		return -EINVAL;
	if ((vma->vm_pgoff * PAGE_SIZE + size) > TARGET_MEM_SIZE)
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
	if (IS_ERR(sg_buf)) {
		ret = PTR_ERR(sg_buf);
		goto sg_buf_alloc_err;
	}

	for (i = 0; size > 0; i++) {
		ret = vm_insert_page(vma, addr, sg_buf->pages[i]);
		if (ret)
			goto ins_page_err;

		addr += PAGE_SIZE;
		size -= PAGE_SIZE;
	};

	vma->vm_private_data = sg_buf;

	dev_info(dev, "%s(%d) } vma %px sg_buf %px",
		 __FUNCTION__, __LINE__, vma, sg_buf);

	return 0;

ins_page_err:
	dma_sg_buf_free(sg_buf);

sg_buf_alloc_err:
	dev_err(dev, "%s(%d) vma %px err %d",
		__FUNCTION__, __LINE__, vma, ret);

	return ret;
}
