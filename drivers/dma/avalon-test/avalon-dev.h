/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Avalon DMA driver
 *
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 */
#ifndef __AVALON_DEV_H__
#define __AVALON_DEV_H__

#include <linux/dmaengine.h>
#include <linux/miscdevice.h>

#include "../avalon/avalon-hw.h"

#define TARGET_MEM_BASE		CONFIG_AVALON_TEST_TARGET_BASE
#define TARGET_MEM_SIZE		CONFIG_AVALON_TEST_TARGET_SIZE

#define TARGET_DMA_SIZE         (2 * AVALON_DMA_MAX_TANSFER_SIZE)
#define TARGET_DMA_SIZE_SG      TARGET_MEM_SIZE

#define DEVICE_NAME		"avalon-dev"

struct avalon_dev {
	struct dma_chan *dma_chan;
	struct miscdevice misc_dev;
};

static inline struct device *chan_to_dev(struct dma_chan *chan)
{
	return chan->device->dev;
}

#endif
