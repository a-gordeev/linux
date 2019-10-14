/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Avalon DMA driver
 *
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 */
#ifndef _UAPI_LINUX_AVALON_IOCTL_H__
#define _UAPI_LINUX_AVALON_IOCTL_H__

#include <linux/types.h>

#define AVALON_DEVICE_NAME		"avalon-dev"

struct avalon_dma_info {
	size_t mem_addr;
	size_t mem_size;
	size_t dma_size;
	size_t dma_size_sg;
} __attribute((packed));

#define AVALON_SIG 'V'

#define IOCTL_AVALON_DMA_GET_INFO	_IOR(AVALON_SIG, 0, struct avalon_dma_info)
#define IOCTL_AVALON_DMA_SET_INFO	_IOW(AVALON_SIG, 1, struct avalon_dma_info)
#define IOCTL_AVALON_DMA_READ		_IOR(AVALON_SIG, 2, struct iovec)
#define IOCTL_AVALON_DMA_WRITE		_IOW(AVALON_SIG, 3, struct iovec)
#define IOCTL_AVALON_DMA_RDWR		_IOWR(AVALON_SIG, 4, struct iovec[2])
#define IOCTL_AVALON_DMA_READ_SG	_IOR(AVALON_SIG, 5, struct iovec)
#define IOCTL_AVALON_DMA_WRITE_SG	_IOW(AVALON_SIG, 6, struct iovec)
#define IOCTL_AVALON_DMA_RDWR_SG	_IOWR(AVALON_SIG, 7, struct iovec[2])
#define IOCTL_AVALON_DMA_READ_SG_SMP	_IOR(AVALON_SIG, 8, struct iovec)
#define IOCTL_AVALON_DMA_WRITE_SG_SMP	_IOW(AVALON_SIG, 9, struct iovec)

#endif
