/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Avalon DMA driver
 *
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 */
#ifndef __AVALON_DRV_IOCTL_H__
#define __AVALON_DRV_IOCTL_H__

long avalon_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#endif
