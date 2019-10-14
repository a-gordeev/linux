/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 *
 * Avalon DMA driver
 */
#ifndef __AVALON_MMAP_H__
#define __AVALON_MMAP_H__

int avalon_dev_mmap(struct file *file, struct vm_area_struct *vma);

#endif
