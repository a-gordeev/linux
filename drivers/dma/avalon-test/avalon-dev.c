// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 *
 * Avalon DMA driver
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pci.h>

#include "avalon-dev.h"
#include "avalon-ioctl.h"
#include "avalon-mmap.h"

unsigned int mem_base = 0x70000000;
module_param(mem_base, uint, 0644);
MODULE_PARM_DESC(mem_base, "Device memory base (default: 0x70000000)");

unsigned int mem_size = 0x10000000;
module_param(mem_size, uint, 0644);
MODULE_PARM_DESC(mem_size, "Device memory size (default: 0x10000000)");

unsigned int dma_size = 0x200000;
module_param(dma_size, uint, 0644);
MODULE_PARM_DESC(dma_size, "DMA buffer transfer size (default: 0x200000)");

unsigned int dma_size_sg = 0x10000000;
module_param(dma_size_sg, uint, 0644);
MODULE_PARM_DESC(dma_size_sg,
		 "DMA scatter list transfer size (default: 0x10000000)");

unsigned int nr_dma_reps = 4;
module_param(nr_dma_reps, uint, 0644);
MODULE_PARM_DESC(nr_dma_reps, "Device memory size (default: 4)");

unsigned int dmas_per_cpu = 8;
module_param(dmas_per_cpu, uint, 0644);
MODULE_PARM_DESC(dmas_per_cpu, "Device memory size (default: 8)");

const struct file_operations avalon_dev_fops = {
	.llseek		= generic_file_llseek,
	.unlocked_ioctl	= avalon_dev_ioctl,
	.mmap		= avalon_dev_mmap,
};

static struct avalon_dev avalon_dev;

static bool filter(struct dma_chan *chan, void *filter_param)
{
	return !strcmp(chan->device->dev->driver->name, "avalon-dma");
}

static int __init avalon_drv_init(void)
{
	struct avalon_dev *adev = &avalon_dev;
	struct dma_chan *chan;
	dma_cap_mask_t mask;
	int ret;

	if (!IS_ALIGNED(mem_base, PAGE_SIZE) ||
	    !IS_ALIGNED(mem_size, PAGE_SIZE) ||
	    !IS_ALIGNED(dma_size, sizeof(u32)) ||
	    !IS_ALIGNED(dma_size_sg, sizeof(u32)))
		return -EINVAL;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	chan = dma_request_channel(mask, filter, NULL);
	if (!chan)
		return -ENODEV;

	adev->dma_chan		= chan;

	adev->misc_dev.minor	= MISC_DYNAMIC_MINOR;
	adev->misc_dev.name	= DEVICE_NAME;
	adev->misc_dev.nodename	= DEVICE_NAME;
	adev->misc_dev.fops	= &avalon_dev_fops;
	adev->misc_dev.mode	= 0644;

	ret = misc_register(&adev->misc_dev);
	if (ret) {
		dma_release_channel(chan);
		return ret;
	}

	dma_size = min(dma_size_sg, mem_size);
	dma_size_sg = min(dma_size_sg, mem_size);

	return 0;
}

static void __exit avalon_drv_exit(void)
{
	struct avalon_dev *adev = &avalon_dev;

	misc_deregister(&adev->misc_dev);
	dma_release_channel(adev->dma_chan);
}

module_init(avalon_drv_init);
module_exit(avalon_drv_exit);

MODULE_AUTHOR("Alexander Gordeev <a.gordeev.box@gmail.com>");
MODULE_DESCRIPTION("Avalon DMA control driver");
MODULE_LICENSE("GPL v2");
