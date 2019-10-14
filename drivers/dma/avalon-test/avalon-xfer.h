/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Avalon DMA driver
 *
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 */
#ifndef __AVALON_XFER_H__
#define __AVALON_XFER_H__

#include <linux/dma-direction.h>

#include "avalon-dev.h"

int xfer_rw(struct dma_chan *chan,
	    enum dma_data_direction dir,
	    void __user *user_buf, size_t user_len);
int xfer_simultaneous(struct dma_chan *chan,
		      void __user *user_buf_rd, size_t user_len_rd,
		      void __user *user_buf_wr, size_t user_len_wr);
int xfer_rw_sg(struct dma_chan *chan,
	       enum dma_data_direction dir,
	       void __user *user_buf, size_t user_len,
	       bool is_smp);
int xfer_simultaneous_sg(struct dma_chan *chan,
			 void __user *user_buf_rd, size_t user_len_rd,
			 void __user *user_buf_wr, size_t user_len_wr);

#endif
