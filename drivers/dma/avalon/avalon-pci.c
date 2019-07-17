// SPDX-License-Identifier: GPL-2.0
/*
 * Avalon DMA driver
 *
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "avalon-core.h"

#define DRIVER_NAME		"avalon-dma"

#define PCI_BAR			CONFIG_AVALON_DMA_PCI_BAR
#define PCI_MSI_VECTOR		CONFIG_AVALON_DMA_PCI_MSI_VECTOR
#define PCI_MSI_COUNT		BIT(CONFIG_AVALON_DMA_PCI_MSI_COUNT_ORDER)

#define AVALON_DMA_PCI_VENDOR_ID	CONFIG_AVALON_DMA_PCI_VENDOR_ID
#define AVALON_DMA_PCI_DEVICE_ID	CONFIG_AVALON_DMA_PCI_DEVICE_ID

static int init_interrupts(struct pci_dev *pci_dev)
{
	int ret;

	ret = pci_alloc_irq_vectors(pci_dev,
				    PCI_MSI_COUNT, PCI_MSI_COUNT,
				    PCI_IRQ_MSI);
	if (ret < 0) {
		goto msi_err;
	} else if (ret != PCI_MSI_COUNT) {
		ret = -ENOSPC;
		goto nr_msi_err;
	}

	ret = pci_irq_vector(pci_dev, PCI_MSI_VECTOR);
	if (ret < 0)
		goto vec_err;

	return ret;

vec_err:
nr_msi_err:
	pci_disable_msi(pci_dev);

msi_err:
	return ret;
}

static void term_interrupts(struct pci_dev *pci_dev)
{
	pci_disable_msi(pci_dev);
}

static int __init avalon_pci_probe(struct pci_dev *pci_dev,
				   const struct pci_device_id *id)
{
	void *adma;
	void __iomem *regs;
	int ret;

	ret = pci_enable_device(pci_dev);
	if (ret)
		goto enable_err;

	ret = pci_request_regions(pci_dev, DRIVER_NAME);
	if (ret)
		goto reg_err;

	regs = pci_ioremap_bar(pci_dev, PCI_BAR);
	if (!regs) {
		ret = -ENOMEM;
		goto ioremap_err;
	}

	ret = init_interrupts(pci_dev);
	if (ret < 0)
		goto int_err;

	adma = avalon_dma_register(&pci_dev->dev, regs, ret);
	if (IS_ERR(adma)) {
		ret = PTR_ERR(adma);
		goto dma_err;
	}

	pci_set_master(pci_dev);
	pci_set_drvdata(pci_dev, adma);

	return 0;

dma_err:
	term_interrupts(pci_dev);

int_err:
	pci_iounmap(pci_dev, regs);

ioremap_err:
	pci_release_regions(pci_dev);

reg_err:
	pci_disable_device(pci_dev);

enable_err:
	return ret;
}

static void __exit avalon_pci_remove(struct pci_dev *pci_dev)
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

static struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(AVALON_DMA_PCI_VENDOR_ID, AVALON_DMA_PCI_DEVICE_ID) },
	{ 0 }
};

static struct pci_driver dma_driver_ops = {
	.name		= DRIVER_NAME,
	.id_table	= pci_ids,
	.probe		= avalon_pci_probe,
	.remove		= avalon_pci_remove,
};

static int __init avalon_drv_init(void)
{
	return pci_register_driver(&dma_driver_ops);
}

static void __exit avalon_drv_exit(void)
{
	pci_unregister_driver(&dma_driver_ops);
}

module_init(avalon_drv_init);
module_exit(avalon_drv_exit);

MODULE_AUTHOR("Alexander Gordeev <a.gordeev.box@gmail.com>");
MODULE_DESCRIPTION("Avalon-MM DMA Interface for PCIe");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(pci, pci_ids);
