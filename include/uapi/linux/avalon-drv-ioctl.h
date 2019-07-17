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
#ifndef _UAPI_LINUX_AVALON_DRV_IOCTL_H__
#define _UAPI_LINUX_AVALON_DRV_IOCTL_H__

struct avalon_ioc_info {
	size_t mem_addr;
	size_t mem_size;
	size_t dma_size;
	size_t dma_size_sg;
} __attribute((packed));

#define AVALON_IOC 'V'

#define IOCTL_AVALON_GET_INFO		_IOR(AVALON_IOC, 0, struct avalon_ioc_info)
#define IOCTL_AVALON_SET_INFO		_IOR(AVALON_IOC, 1, struct avalon_ioc_info)
#define IOCTL_AVALON_DMA_READ		_IOR(AVALON_IOC, 2, struct iovec)
#define IOCTL_AVALON_DMA_WRITE		_IOW(AVALON_IOC, 3, struct iovec)
#define IOCTL_AVALON_DMA_RDWR		_IOWR(AVALON_IOC, 4, struct iovec[2])
#define IOCTL_AVALON_DMA_READ_SG	_IOR(AVALON_IOC, 5, struct iovec)
#define IOCTL_AVALON_DMA_WRITE_SG	_IOW(AVALON_IOC, 6, struct iovec)
#define IOCTL_AVALON_DMA_RDWR_SG	_IOWR(AVALON_IOC, 7, struct iovec[2])
#define IOCTL_AVALON_DMA_READ_SG_SMP	_IOR(AVALON_IOC, 8, struct iovec)
#define IOCTL_AVALON_DMA_WRITE_SG_SMP	_IOW(AVALON_IOC, 9, struct iovec)

#endif
