#ifndef __AVALON_DRV_H__
#define __AVALON_DRV_H__

#include <linux/miscdevice.h>

#include <linux/avalon-dma.h>

#define DRIVER_VERSION "0.0.0"

#define DRIVER_NAME "avalon-drv"

struct avalon_dev {
	struct pci_dev *pci_dev;
	struct avalon_dma avalon_dma;
	struct miscdevice misc_dev;
};

#endif
