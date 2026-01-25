/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2026 Alexander Gordeev <a.gordeev.box@gmail.com>
 */
#ifndef _UAPI_LINUX_DMA_SLAVE_H__
#define _UAPI_LINUX_DMA_SLAVE_H__

#define DMA_SLAVE_DEVICE		"dma-slave"

struct dma_slave_config_uapi {
	struct iovec data;
	struct iovec peripheral_config;
	__u64 src_addr;
	__u64 dst_addr;
	__u32 src_addr_width;
	__u32 dst_addr_width;
	__u32 src_maxburst;
	__u32 dst_maxburst;
	__u32 src_port_window_size;
	__u32 dst_port_window_size;
	bool device_fc;
	char channel_name[32];
};

#define DMA_SLAVE_SIG 'S'

#define IOCTL_DMA_SLAVE_READ		_IOR(DMA_SLAVE_SIG, 0, struct dma_slave_config_uapi)
#define IOCTL_DMA_SLAVE_WRITE		_IOW(DMA_SLAVE_SIG, 1, struct dma_slave_config_uapi)

#endif
