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
#include <linux/uio.h>

#include <uapi/linux/avalon-ioctl.h>

#include "avalon-xfer.h"

long avalon_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct avalon_dev *adev = container_of(file->private_data,
		struct avalon_dev, misc_dev);
	struct dma_chan *chan = adev->dma_chan;
	struct iovec iovec[2];
	void __user *buf = NULL, __user *buf_rd = NULL, __user *buf_wr = NULL;
	size_t len = 0, len_rd = 0, len_wr = 0;
	int ret = -EINVAL;

	switch (cmd) {
	case IOCTL_AVALON_DMA_GET_INFO: {
		struct avalon_dma_info info = {
			.mem_addr	= mem_base,
			.mem_size	= mem_size,
			.dma_size	= dma_size,
			.dma_size_sg	= dma_size_sg,
		};

		if (copy_to_user((void *)arg, &info, sizeof(info)))
			return -EFAULT;

		return 0;
	}
	case IOCTL_AVALON_DMA_SET_INFO:
		return -EINVAL;
	case IOCTL_AVALON_DMA_READ:
	case IOCTL_AVALON_DMA_WRITE:
	case IOCTL_AVALON_DMA_READ_SG:
	case IOCTL_AVALON_DMA_WRITE_SG:
	case IOCTL_AVALON_DMA_READ_SG_SMP:
	case IOCTL_AVALON_DMA_WRITE_SG_SMP:
		if (copy_from_user(iovec, (void *)arg, sizeof(iovec[0])))
			return -EFAULT;

		buf = iovec[0].iov_base;
		len = iovec[0].iov_len;

		break;
	case IOCTL_AVALON_DMA_RDWR:
	case IOCTL_AVALON_DMA_RDWR_SG:
		if (copy_from_user(iovec, (void *)arg, sizeof(iovec)))
			return -EFAULT;

		buf_rd = iovec[0].iov_base;
		len_rd = iovec[0].iov_len;

		buf_wr = iovec[1].iov_base;
		len_wr = iovec[1].iov_len;

		break;
	default:
		return -EINVAL;
	};

	switch (cmd) {
	case IOCTL_AVALON_DMA_READ:
		ret = xfer_single(chan, DMA_FROM_DEVICE, buf, len);
		break;
	case IOCTL_AVALON_DMA_WRITE:
		ret = xfer_single(chan, DMA_TO_DEVICE, buf, len);
		break;
	case IOCTL_AVALON_DMA_RDWR:
		ret = xfer_rw_single(chan, buf_rd, len_rd, buf_wr, len_wr);
		break;
	case IOCTL_AVALON_DMA_READ_SG:
		ret = xfer_sg(chan, DMA_FROM_DEVICE, buf, len, false);
		break;
	case IOCTL_AVALON_DMA_WRITE_SG:
		ret = xfer_sg(chan, DMA_TO_DEVICE, buf, len, false);
		break;
	case IOCTL_AVALON_DMA_READ_SG_SMP:
		ret = xfer_sg(chan, DMA_FROM_DEVICE, buf, len, true);
		break;
	case IOCTL_AVALON_DMA_WRITE_SG_SMP:
		ret = xfer_sg(chan, DMA_TO_DEVICE, buf, len, true);
		break;
	case IOCTL_AVALON_DMA_RDWR_SG:
		ret = xfer_rw_sg(chan, buf_rd, len_rd, buf_wr, len_wr);
		break;
	};

	return ret;
}
