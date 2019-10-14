/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 *
 * Avalon DMA driver
 */
#ifndef __AVALON_XFER_H__
#define __AVALON_XFER_H__

#include <linux/dmaengine.h>

#include "avalon-dev.h"

int xfer_single(struct dma_chan *chan,
		enum dma_transfer_direction direction,
		void __user *user_buf, size_t user_len);
int xfer_rw_single(struct dma_chan *chan,
		   void __user *user_buf_rd, size_t user_len_rd,
		   void __user *user_buf_wr, size_t user_len_wr);
int xfer_sg(struct dma_chan *chan,
	    enum dma_transfer_direction direction,
	    void __user *user_buf, size_t user_len,
	    bool is_smp);
int xfer_rw_sg(struct dma_chan *chan,
	       void __user *user_buf_rd, size_t user_len_rd,
	       void __user *user_buf_wr, size_t user_len_wr);

#endif
