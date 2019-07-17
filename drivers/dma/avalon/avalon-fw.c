// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/property.h>

#include "avalon-fw.h"

struct fw_params fw_params = {
#ifndef CONFIG_AVALON_DMA_FW_PARAMS
	.ctrl_base	= CONFIG_AVALON_DMA_CTRL_BASE,
	.rd_ep_dst_lo	= CONFIG_AVALON_DMA_RD_EP_DST_LO,
	.rd_ep_dst_hi	= CONFIG_AVALON_DMA_RD_EP_DST_HI,
	.wr_ep_dst_lo	= CONFIG_AVALON_DMA_WR_EP_DST_LO,
	.wr_ep_dst_hi	= CONFIG_AVALON_DMA_WR_EP_DST_HI,
	.dma_mask_width	= CONFIG_AVALON_DMA_DMA_MASK_WIDTH,
	.msi_index	= CONFIG_AVALON_DMA_MSI_INDEX,
	.regs_bar	= CONFIG_AVALON_DMA_REGS_BAR,
#endif
};

int fw_params_init(struct device *dev)
{
	int ret;

	if (!IS_ENABLED(CONFIG_AVALON_DMA_FW_PARAMS))
		return 0;

	ret = device_property_read_u64(dev, "controller-base", &fw_params.ctrl_base);
	if (ret)
		return ret;
	ret = device_property_read_u32(dev, "read-status-desc-low", &fw_params.rd_ep_dst_lo);
	if (ret)
		return ret;
	ret = device_property_read_u32(dev, "read-status-desc-high", &fw_params.rd_ep_dst_hi);
	if (ret)
		return ret;
	ret = device_property_read_u32(dev, "write-status-desc-low", &fw_params.wr_ep_dst_lo);
	if (ret)
		return ret;
	ret = device_property_read_u32(dev, "write-status-desc-hi", &fw_params.wr_ep_dst_hi);
	if (ret)
		return ret;
	ret = device_property_read_u32(dev, "dma-mask-width", &fw_params.dma_mask_width);
	if (ret)
		return ret;
	ret = device_property_read_u32(dev, "msi-index", &fw_params.msi_index);
	if (ret)
		return ret;
	ret = device_property_read_u32(dev, "regs-bar", &fw_params.regs_bar);
	if (ret)
		return ret;

	return 0;
}
