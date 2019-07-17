/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 *
 * Avalon DMA engine
 */
#ifndef __AVALON_CORE_H__
#define __AVALON_CORE_H__

#include <linux/interrupt.h>
#include <linux/dma-direction.h>

#include "../virt-dma.h"

#include "avalon-hw.h"

struct avalon_dma_desc {
	struct virt_dma_desc	vdesc;

	enum dma_data_direction	direction;

	dma_addr_t		dev_addr;

	unsigned int		seg_curr;
	unsigned int		seg_off;

	unsigned int		nr_segs;
	struct dma_segment	seg[];
};

struct avalon_dma_hw {
	struct __dma_desc_table {
		struct dma_desc_table *cpu_addr;
		dma_addr_t dma_addr;
	} dma_desc_table_rd, dma_desc_table_wr;

	int			h2d_last_id;
	int			d2h_last_id;

	void __iomem		*regs;
};

struct avalon_dma_chan {
	struct virt_dma_chan	vchan;

	dma_addr_t		src_addr;
	dma_addr_t		dst_addr;

	struct avalon_dma_hw	hw;

	struct avalon_dma_desc	*active_desc;
};

struct avalon_dma {
	struct device		*dev;
	unsigned int		irq;

	struct avalon_dma_chan	chan;
	struct dma_device	dma_dev;
	struct device_dma_parameters dma_parms;
};

static inline
struct avalon_dma_chan *to_avalon_dma_chan(struct dma_chan *dma_chan)
{
	return container_of(dma_chan, struct avalon_dma_chan, vchan.chan);
}

static inline
struct avalon_dma_desc *to_avalon_dma_desc(struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct avalon_dma_desc, vdesc);
}

static inline
struct avalon_dma *chan_to_avalon_dma(struct avalon_dma_chan *chan)
{
	return container_of(chan, struct avalon_dma, chan);
}

static inline
__iomem void *__iomem avalon_dma_mmio(struct avalon_dma *adma)
{
	return adma->chan.hw.regs;
}

struct avalon_dma *avalon_dma_register(struct device *dev,
				       void __iomem *regs,
				       unsigned int irq);
void avalon_dma_unregister(struct avalon_dma *adma);

#endif
