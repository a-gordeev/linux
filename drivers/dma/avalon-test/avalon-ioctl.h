/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 *
 * Avalon DMA driver
 */
#ifndef __AVALON_IOCTL_H__
#define __AVALON_IOCTL_H__

long avalon_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#endif
