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
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/device.h>

#if defined(CONFIG_DYNAMIC_DEBUG)
static int print_mem(char *buf, size_t buf_len,
		     const void *mem, size_t mem_len)
{
	int ret, i, total = 0;

	if (buf_len < 3)
		return -EINVAL;

	mem_len = min_t(size_t, mem_len, buf_len / 3);
	for (i = 0; i < mem_len; i++) {
		ret = snprintf(buf + total, buf_len - total,
			 "%02X ", ((const unsigned char*)mem)[i]);
		if (ret < 0) {
			strcpy(buf, "--");
			return ret;
		}
		total += ret;
	}

	buf[total] = 0;

	return total;
}

void dump_mem(struct device *dev, void *data, size_t len)
{
	char buf[256];
	int n;

	n = snprintf(buf, sizeof(buf), "%s(%d): %px [ ", __FUNCTION__, __LINE__, data);

	print_mem(buf + n, sizeof(buf) - n, data, len);

	dev_dbg(dev, "%s(%d): %s]\n", __FUNCTION__, __LINE__, buf);
}
#else
void dump_mem(struct device *dev, void *data, size_t len)
{
}
#endif
