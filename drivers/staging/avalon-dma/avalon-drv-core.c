#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "avalon-drv.h"
#include "avalon-drv-fops.h"
#include "avalon-drv-interrupt.h"
#include "avalon-drv-attr.h"

#define PCI_VENDOR_ID_ALARIC	0x1172

static int avalon_dev_register(struct avalon_dev *avalon_dev,
			       const struct file_operations *fops)
{
	avalon_dev->misc_dev.minor	= MISC_DYNAMIC_MINOR;
	avalon_dev->misc_dev.name	= DRIVER_NAME;
	avalon_dev->misc_dev.nodename	= DRIVER_NAME;
	avalon_dev->misc_dev.fops	= fops;
	avalon_dev->misc_dev.mode	= 0644;

	return misc_register(&avalon_dev->misc_dev);
}

static void avalon_dev_unregister(struct avalon_dev *avalon_dev)
{
	misc_deregister(&avalon_dev->misc_dev);
}

static int __init avalon_pci_probe(struct pci_dev *pci_dev,
				   const struct pci_device_id *id)
{
	struct avalon_dev *avalon_dev;
	int ret;

	avalon_dev = kzalloc(sizeof(*avalon_dev), GFP_KERNEL);
	if (!avalon_dev)
		return -ENOMEM;

	ret = pci_enable_device(pci_dev);
	if (ret)
		goto enable_err;

	ret = pci_request_regions(pci_dev, DRIVER_NAME);
	if (ret)
		goto reg_err;

	ret = init_interrupts(avalon_dev, pci_dev);
	if (ret)
		goto int_err;

	ret = avalon_dma_init(&avalon_dev->avalon_dma, pci_dev);
	if (ret)
		goto dma_err;

	ret = avalon_dev_register(avalon_dev, &avalon_dev_fops);
	if (ret)
		goto dev_reg_err;

	ret = init_attributes(&pci_dev->dev);
	if (ret)
		goto attr_err;

	pci_set_master(pci_dev);
	pci_write_config_byte(pci_dev, PCI_INTERRUPT_LINE, pci_dev->irq);

	avalon_dev->pci_dev = pci_dev;
	pci_set_drvdata(pci_dev, avalon_dev);

	return 0;

attr_err:
	avalon_dev_unregister(avalon_dev);

dev_reg_err:
	avalon_dma_term(&avalon_dev->avalon_dma);

dma_err:
	term_interrupts(avalon_dev);

int_err:
	pci_release_regions(pci_dev);

reg_err:
	pci_disable_device(pci_dev);

enable_err:
	kfree(avalon_dev);

	return ret;
}

static void __exit avalon_pci_remove(struct pci_dev *pci_dev)
{
	struct avalon_dev *avalon_dev = pci_get_drvdata(pci_dev);

	pci_set_drvdata(pci_dev, NULL);

	term_attributes(&pci_dev->dev);
	avalon_dev_unregister(avalon_dev);

	avalon_dma_term(&avalon_dev->avalon_dma);

	term_interrupts(avalon_dev);

	pci_release_regions(pci_dev);
	pci_disable_device(pci_dev);

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
