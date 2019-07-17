/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Avalon DMA driver
 *
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 */
#ifndef __AVALON_DRV_MMAP_H__
#define __AVALON_DRV_MMAP_H__

int avalon_dev_mmap(struct file *file, struct vm_area_struct *vma);

#endif
