#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "avalon-drv.h"
#include "avalon-drv-fops.h"
#include "avalon-drv-interrupt.h"
#include "avalon-drv-attr.h"

#define PCI_VENDOR_ID_ALARIC	0x1172

static int __init nddc_pci_probe(struct pci_dev *dev,
				 const struct pci_device_id *id)
{
	struct nerdic_device *nddc;
	int rc;

	nddc = kzalloc(sizeof(struct nerdic_device), GFP_KERNEL);
	BUG_ON(!nddc);

	nddc->pci_dev = dev;
	pci_set_drvdata(dev, nddc);

	nddc->nddc_pci_misc.minor = MISC_DYNAMIC_MINOR;
	nddc->nddc_pci_misc.name = NERDIC_DMA_DRIVER_NAME;
	nddc->nddc_pci_misc.nodename = "ddc/" NERDIC_DMA_DRIVER_NAME;
	nddc->nddc_pci_misc.fops = &nddc_pci_channel_fops;
	nddc->nddc_pci_misc.mode = 0666;

	rc = misc_register(&nddc->nddc_pci_misc);
	BUG_ON(rc);

	rc = pci_enable_device(dev);
	BUG_ON(rc);

	rc = pci_request_regions(dev, NERDIC_DMA_DRIVER_NAME);
	BUG_ON(rc);

	pci_set_master(dev);

	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);

	rc = init_interrupts(nddc);
	BUG_ON(rc);

	rc = avalon_dma_init(&nddc->avalon_dma, dev);
	BUG_ON(rc);

	rc = init_attributes(&dev->dev);
	BUG_ON(rc);

	return 0;
}

static void __exit nddc_pci_remove(struct pci_dev *dev)
{
	struct nerdic_device *nddc = pci_get_drvdata(dev);

	term_attributes(&dev->dev);

	nddc = pci_get_drvdata(dev);
	term_interrupts(nddc);

	misc_deregister(&nddc->nddc_pci_misc);

	pci_disable_device(dev);
	pci_release_regions(dev);

	avalon_dma_term(&nddc->avalon_dma);

	kfree(nddc);
}

static struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ALARIC, 0xe003) },
	{ 0 }
};

static struct pci_driver dma_driver_ops = {
	.name		= NERDIC_DMA_DRIVER_NAME,
	.id_table	= pci_ids,
	.probe		= nddc_pci_probe,
	.remove		= nddc_pci_remove,
};

static int __init nddc_drv_init(void)
{
	int rc = pci_register_driver(&dma_driver_ops);
	if (rc) {
		printk(KERN_ERR "PCI driver registration failed\n");
		return rc;
	}

	return 0;
}

static void __exit nddc_drv_exit(void)
{
	pci_unregister_driver(&dma_driver_ops);
}

module_init(nddc_drv_init);
module_exit(nddc_drv_exit);

MODULE_INFO(credit, "Alex Feinman <alex.feinman@daqri.com>");
MODULE_AUTHOR("Alexander Gordeev <alexander.gordeev@daqri.com>");
MODULE_DESCRIPTION("Avalon DMA control driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DEVICE_TABLE(pci, pci_ids);
