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
#include <linux/uio.h>

#include <uapi/linux/avalon-drv-ioctl.h>

#include "avalon-drv.h"
#include "avalon-drv-xfer.h"

static const gfp_t gfp_flags = GFP_KERNEL;

long avalon_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct avalon_dev *avalon_dev = container_of(file->private_data,
		struct avalon_dev, misc_dev);
	struct device *dev = &avalon_dev->pci_dev->dev;
	struct iovec iovec[2];
	void __user *buf = NULL, __user *buf_rd = NULL, __user *buf_wr = NULL;
	size_t len = 0, len_rd = 0, len_wr = 0;
	int ret;

	dev_dbg(dev, "%s(%d) { cmd %x", __FUNCTION__, __LINE__, cmd);

	switch (cmd) {
	case IOCTL_AVALON_DMA_READ:
	case IOCTL_AVALON_DMA_WRITE:
	case IOCTL_AVALON_DMA_READ_SG:
	case IOCTL_AVALON_DMA_WRITE_SG:
	case IOCTL_AVALON_DMA_READ_SG_SMP:
	case IOCTL_AVALON_DMA_WRITE_SG_SMP:
		if (copy_from_user(iovec, (void*)arg, sizeof(iovec[0]))) {
			ret = -EFAULT;
			goto done;
		}

		buf = iovec[0].iov_base;
		len = iovec[0].iov_len;

		break;

	case IOCTL_AVALON_DMA_RDWR:
	case IOCTL_AVALON_DMA_RDWR_SG:
		if (copy_from_user(iovec, (void*)arg, sizeof(iovec))) {
			ret = -EFAULT;
			goto done;
		}

		buf_rd = iovec[0].iov_base;
		len_rd = iovec[0].iov_len;

		buf_wr = iovec[1].iov_base;
		len_wr = iovec[1].iov_len;

		break;

	default:
		ret = -ENOSYS;
		goto done;
	};

	dev_dbg(dev,
		 "%s(%d) buf %px len %ld\nbuf_rd %px len_rd %ld\nbuf_wr %px len_wr %ld\n",
		 __FUNCTION__, __LINE__, buf, len, buf_rd, len_rd, buf_wr, len_wr);

	switch (cmd) {
	case IOCTL_AVALON_DMA_READ:
		ret = xfer_rw(&avalon_dev->avalon_dma, DMA_FROM_DEVICE, buf, len);
		break;
	case IOCTL_AVALON_DMA_WRITE:
		ret = xfer_rw(&avalon_dev->avalon_dma, DMA_TO_DEVICE, buf, len);
		break;
	case IOCTL_AVALON_DMA_RDWR:
		ret = xfer_simultaneous(&avalon_dev->avalon_dma,
					buf_rd, len_rd,
					buf_wr, len_wr);
		break;

	case IOCTL_AVALON_DMA_READ_SG:
		ret = xfer_rw_sg(&avalon_dev->avalon_dma, DMA_FROM_DEVICE, buf, len, false);
		break;
	case IOCTL_AVALON_DMA_WRITE_SG:
		ret = xfer_rw_sg(&avalon_dev->avalon_dma, DMA_TO_DEVICE, buf, len, false);
		break;
	case IOCTL_AVALON_DMA_READ_SG_SMP:
		ret = xfer_rw_sg(&avalon_dev->avalon_dma, DMA_FROM_DEVICE, buf, len, true);
		break;
	case IOCTL_AVALON_DMA_WRITE_SG_SMP:
		ret = xfer_rw_sg(&avalon_dev->avalon_dma, DMA_TO_DEVICE, buf, len, true);
		break;
	case IOCTL_AVALON_DMA_RDWR_SG:
		ret = xfer_simultaneous_sg(&avalon_dev->avalon_dma,
					   buf_rd, len_rd,
					   buf_wr, len_wr);
		break;

	default:
		BUG();
		ret = -ENOSYS;
	};

done:
	dev_dbg(dev, "%s(%d) } = %d", __FUNCTION__, __LINE__, ret);

	return ret;
}
