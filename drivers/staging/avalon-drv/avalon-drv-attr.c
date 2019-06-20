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

#include "avalon-drv.h"

#ifdef AVALON_DEBUG_STATS
static ssize_t avalon_dma_stats_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct avalon_dev *avalon_dev = dev_get_drvdata(dev);

        return avalon_dma_print_stats(buf, PAGE_SIZE, &avalon_dev->avalon_dma);
}
static DEVICE_ATTR_RO(avalon_dma_stats);

static ssize_t avalon_dma_lists_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct avalon_dev *avalon_dev = dev_get_drvdata(dev);

        return avalon_dma_print_lists(buf, PAGE_SIZE, &avalon_dev->avalon_dma);
}
static DEVICE_ATTR_RO(avalon_dma_lists);

int init_attributes(struct device *dev)
{
	device_create_file(dev, &dev_attr_avalon_dma_stats);
	device_create_file(dev, &dev_attr_avalon_dma_lists);

	return 0;
}

void term_attributes(struct device *dev)
{
	device_remove_file(dev, &dev_attr_avalon_dma_lists);
	device_remove_file(dev, &dev_attr_avalon_dma_stats);
}
#else
int init_attributes(struct device *dev)
{
	return 0;
}

void term_attributes(struct device *dev)
{
}
#endif
