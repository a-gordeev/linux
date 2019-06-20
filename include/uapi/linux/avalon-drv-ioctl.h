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

#define AVALON_IOC 'V'

#define IOCTL_ALARIC_DMA_READ		_IOR(AVALON_IOC, 0, struct iovec)
#define IOCTL_ALARIC_DMA_WRITE		_IOW(AVALON_IOC, 1, struct iovec)
#define IOCTL_ALARIC_DMA_SIMULTANEOUS	_IOWR(AVALON_IOC, 2, struct iovec[2])
#define IOCTL_ALARIC_DMA_READ_SG	_IOR(AVALON_IOC, 3, struct iovec)
#define IOCTL_ALARIC_DMA_WRITE_SG	_IOW(AVALON_IOC, 4, struct iovec)
#define IOCTL_ALARIC_DMA_SIMULTANEOUS_SG	_IOWR(AVALON_IOC, 5, struct iovec[2])
#define IOCTL_ALARIC_DMA_READ_SG_SMP	_IOR(AVALON_IOC, 6, struct iovec)
#define IOCTL_ALARIC_DMA_WRITE_SG_SMP	_IOW(AVALON_IOC, 7, struct iovec)

#endif
