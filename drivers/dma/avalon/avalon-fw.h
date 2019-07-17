/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 *
 * Avalon DMA engine
 */
#ifndef __AVALON_FW_H__
#define __AVALON_FW_H__

#include <linux/io.h>
#include <linux/dmaengine.h>

struct fw_params {
	u64 ctrl_base;
	u32 rd_ep_dst_lo;
	u32 rd_ep_dst_hi;
	u32 wr_ep_dst_lo;
	u32 wr_ep_dst_hi;
	u32 dma_mask_width;
	u32 msi_index;
	u32 regs_bar;
};

extern struct fw_params fw_params;

int fw_params_init(struct device *dev);

#endif
