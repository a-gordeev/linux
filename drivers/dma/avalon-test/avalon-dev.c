// SPDX-License-Identifier: GPL-2.0
/*
 * Avalon DMA driver
 *
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>

#include "avalon-dev.h"
#include "avalon-ioctl.h"
#include "avalon-mmap.h"

const struct file_operations avalon_dev_fops = {
	.llseek		= generic_file_llseek,
	.unlocked_ioctl	= avalon_dev_ioctl,
	.mmap		= avalon_dev_mmap,
};

static struct avalon_dev avalon_dev;

static int __init avalon_drv_init(void)
{
	struct avalon_dev *adev = &avalon_dev;
	struct dma_chan *chan;
	dma_cap_mask_t mask;
	int ret;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	chan = dma_request_channel(mask, NULL, NULL);
	if (!chan)
		return -ENODEV;

	adev->dma_chan		= chan;

	adev->misc_dev.minor	= MISC_DYNAMIC_MINOR;
	adev->misc_dev.name	= DEVICE_NAME;
	adev->misc_dev.nodename	= DEVICE_NAME;
	adev->misc_dev.fops	= &avalon_dev_fops;
	adev->misc_dev.mode	= 0644;

	ret = misc_register(&adev->misc_dev);
	if (ret)
		return ret;

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
