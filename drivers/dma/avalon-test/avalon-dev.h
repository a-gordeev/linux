/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 *
 * Avalon DMA driver
 */
#ifndef __AVALON_DEV_H__
#define __AVALON_DEV_H__

#include <linux/dmaengine.h>
#include <linux/miscdevice.h>

#define DEVICE_NAME		"avalon-dev"

extern unsigned int mem_base;
extern unsigned int mem_size;
extern unsigned int dma_size;
extern unsigned int dma_size_sg;
extern unsigned int nr_dma_reps;
extern unsigned int dmas_per_cpu;

struct avalon_dev {
	struct dma_chan *dma_chan;
	struct miscdevice misc_dev;
};

static inline struct device *chan_to_dev(struct dma_chan *chan)
{
	return chan->device->dev;
}

#endif
