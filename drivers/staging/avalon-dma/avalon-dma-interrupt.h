// SPDX-License-Identifier: GPL-2.0
/*
 * Avalon DMA engine
 *
 * Copyright (C) 2018-2019 DAQRI, http://www.daqri.com/
 *
 * Created by Alexander Gordeev <alexander.gordeev@daqri.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of the
 * Linux distribution for more details.
 */
#ifndef __AVALON_DMA_INTERRUPT_H__
#define __AVALON_DMA_INTERRUPT_H__

irqreturn_t avalon_dma_interrupt(int irq, void *dev_id);
void avalon_dma_tasklet(unsigned long);

#endif
