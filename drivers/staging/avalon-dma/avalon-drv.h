#ifndef __NERDIC_DRV_H__
#define __NERDIC_DRV_H__

#include <linux/miscdevice.h>

#include "avalon-dma.h"

#define DRIVER_VERSION "0.0.2"

#define NERDIC_DMA_DRIVER_NAME "nddc"

struct nerdic_device {
	struct pci_dev *pci_dev;
	struct avalon_dma avalon_dma;
	struct miscdevice nddc_pci_misc;
};

#endif
