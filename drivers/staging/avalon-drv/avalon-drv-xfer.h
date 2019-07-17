
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
#ifndef __AVALON_DRV_XFER_H__
#define __AVALON_DRV_XFER_H__

int xfer_rw(struct avalon_dma *avalon_dma,
	    enum dma_data_direction dir,
	    void __user *user_buf, size_t user_len);
int xfer_simultaneous(struct avalon_dma *avalon_dma,
		      void __user *user_buf_rd, size_t user_len_rd,
		      void __user *user_buf_wr, size_t user_len_wr);
int xfer_rw_sg(struct avalon_dma *avalon_dma,
	       enum dma_data_direction dir,
	       void __user *user_buf, size_t user_len,
	       bool is_smp);
int xfer_simultaneous_sg(struct avalon_dma *avalon_dma,
			 void __user *user_buf_rd, size_t user_len_rd,
			 void __user *user_buf_wr, size_t user_len_wr);

#endif
