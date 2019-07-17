// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 *
 * Avalon DMA driver
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "avalon-core.h"

#define DRIVER_NAME "avalon-dma"

static unsigned int pci_bar = 0;
module_param(pci_bar, uint, 0644);
MODULE_PARM_DESC(pci_bar,
		 "PCI BAR number the controller is mapped to (default: 0)");

static unsigned int pci_msi_vector = 0;
module_param(pci_msi_vector, uint, 0644);
MODULE_PARM_DESC(pci_msi_vector,
		 "MSI vector number used for the controller (default: 0)");

static unsigned int pci_msi_count_order = 5;
module_param(pci_msi_count_order, uint, 0644);
MODULE_PARM_DESC(pci_msi_count_order,
		 "Number of MSI vectors (order) device uses (default: 5)");

static int init_interrupts(struct pci_dev *pci_dev)
{
	unsigned int nr_vecs = BIT(pci_msi_count_order);
	int ret;

	ret = pci_alloc_irq_vectors(pci_dev, nr_vecs, nr_vecs, PCI_IRQ_MSI);
	if (ret < 0) {
		return ret;

	} else if (ret != nr_vecs) {
		ret = -ENOSPC;
		goto disable_msi;
	}

	ret = pci_irq_vector(pci_dev, pci_msi_vector);
	if (ret < 0)
		goto disable_msi;

	return ret;

disable_msi:
	pci_disable_msi(pci_dev);

	return ret;
}

static void term_interrupts(struct pci_dev *pci_dev)
{
	pci_disable_msi(pci_dev);
}

static int avalon_pci_probe(struct pci_dev *pci_dev,
			    const struct pci_device_id *id)
{
	void *adma;
	void __iomem *regs;
	int ret;

	ret = pci_enable_device(pci_dev);
	if (ret)
		return ret;

	ret = pci_request_regions(pci_dev, DRIVER_NAME);
	if (ret)
		goto disable_device;

	regs = pci_ioremap_bar(pci_dev, pci_bar);
	if (!regs) {
		ret = -ENOMEM;
		goto release_regions;
	}

	ret = init_interrupts(pci_dev);
	if (ret < 0)
		goto unmap_bars;

	adma = avalon_dma_register(&pci_dev->dev, regs, ret);
	if (IS_ERR(adma)) {
		ret = PTR_ERR(adma);
		goto terminate_interrupts;
	}

	pci_set_master(pci_dev);
	pci_set_drvdata(pci_dev, adma);

	return 0;

terminate_interrupts:
	term_interrupts(pci_dev);

unmap_bars:
	pci_iounmap(pci_dev, regs);

release_regions:
	pci_release_regions(pci_dev);

disable_device:
	pci_disable_device(pci_dev);

	return ret;
}

static void avalon_pci_remove(struct pci_dev *pci_dev)
{
	void *adma = pci_get_drvdata(pci_dev);
	void __iomem *regs = avalon_dma_mmio(adma);

	pci_set_drvdata(pci_dev, NULL);

	avalon_dma_unregister(adma);
	term_interrupts(pci_dev);

	pci_iounmap(pci_dev, regs);
	pci_release_regions(pci_dev);
	pci_disable_device(pci_dev);
}

static struct pci_device_id avalon_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ALTERA, 0xe003) },
	{ 0 }
};

static struct pci_driver avalon_pci_driver = {
	.name		= DRIVER_NAME,
	.id_table	= avalon_pci_ids,
	.probe		= avalon_pci_probe,
	.remove		= avalon_pci_remove,
};

module_pci_driver(avalon_pci_driver);

MODULE_AUTHOR("Alexander Gordeev <a.gordeev.box@gmail.com>");
MODULE_DESCRIPTION("Avalon-MM DMA Interface for PCIe");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(pci, avalon_pci_ids);
