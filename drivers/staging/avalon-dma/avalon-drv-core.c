#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "avalon-drv.h"
#include "avalon-drv-fops.h"
#include "avalon-drv-interrupt.h"
#include "avalon-drv-attr.h"

#define PCI_VENDOR_ID_ALARIC	0x1172

static int __init avalon_pci_probe(struct pci_dev *dev,
				   const struct pci_device_id *id)
{
	struct avalon_dev *avalon_dev;
	int rc;

	avalon_dev = kzalloc(sizeof(*avalon_dev), GFP_KERNEL);
	BUG_ON(!avalon_dev);

	avalon_dev->pci_dev = dev;
	pci_set_drvdata(dev, avalon_dev);

	avalon_dev->misc_dev.minor = MISC_DYNAMIC_MINOR;
	avalon_dev->misc_dev.name = DRIVER_NAME;
	avalon_dev->misc_dev.nodename = DRIVER_NAME;
	avalon_dev->misc_dev.fops = &avalon_dev_fops;
	avalon_dev->misc_dev.mode = 0666;

	rc = misc_register(&avalon_dev->misc_dev);
	BUG_ON(rc);

	rc = pci_enable_device(dev);
	BUG_ON(rc);

	rc = pci_request_regions(dev, DRIVER_NAME);
	BUG_ON(rc);

	pci_set_master(dev);

	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);

	rc = init_interrupts(avalon_dev);
	BUG_ON(rc);

	rc = avalon_dma_init(&avalon_dev->avalon_dma, dev);
	BUG_ON(rc);

	rc = init_attributes(&dev->dev);
	BUG_ON(rc);

	return 0;
}

static void __exit avalon_pci_remove(struct pci_dev *dev)
{
	struct avalon_dev *avalon_dev = pci_get_drvdata(dev);

	term_attributes(&dev->dev);

	avalon_dev = pci_get_drvdata(dev);
	term_interrupts(avalon_dev);

	misc_deregister(&avalon_dev->misc_dev);

	pci_disable_device(dev);
	pci_release_regions(dev);

	avalon_dma_term(&avalon_dev->avalon_dma);

	kfree(avalon_dev);
}

static struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ALARIC, 0xe003) },
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

MODULE_AUTHOR("Alexander Gordeev <alexander.gordeev@daqri.com>");
MODULE_DESCRIPTION("Avalon DMA control driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DEVICE_TABLE(pci, pci_ids);
