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
#ifndef __AVALON_DRV_H__
#define __AVALON_DRV_H__

#include <linux/miscdevice.h>

#include <linux/avalon-dma.h>

#define DRIVER_NAME "avalon-drv"

struct avalon_dev {
	struct pci_dev *pci_dev;
	struct avalon_dma avalon_dma;
	struct miscdevice misc_dev;
};

#endif
