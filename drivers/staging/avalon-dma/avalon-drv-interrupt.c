#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include "avalon-drv.h"
#include "avalon-drv-interrupt.h"

#define DMA_STATUS_INT		0
#define AVALON_MSI_COUNT	4

static irqreturn_t dma_interrupt(int irq, void *dev_id)
{
	struct avalon_dev *avalon_dev = (struct avalon_dev*)dev_id;

	return avalon_dma_interrupt(&avalon_dev->avalon_dma);
}

int init_interrupts(struct avalon_dev *avalon_dev, struct pci_dev *pci_dev)
{
	int vec;
	int ret;

	ret = pci_alloc_irq_vectors(pci_dev,
				    AVALON_MSI_COUNT, AVALON_MSI_COUNT,
				    PCI_IRQ_MSI);
	if (ret < 0)
		goto msi_err;
	else if (ret != AVALON_MSI_COUNT)
		goto nr_msi_err;

	vec = pci_irq_vector(pci_dev, DMA_STATUS_INT);
	ret = request_irq(vec, dma_interrupt, IRQF_SHARED,
			  DRIVER_NAME, avalon_dev);
	if (ret)
		goto req_irq_err;

	return 0;

req_irq_err:
nr_msi_err:
	pci_disable_msi(pci_dev);

msi_err:
	return ret;
}

void term_interrupts(struct avalon_dev *avalon_dev)
{
	struct pci_dev *pci_dev = avalon_dev->pci_dev;
	int vec = pci_irq_vector(pci_dev, DMA_STATUS_INT);

	free_irq(vec, (void*)avalon_dev);
	pci_disable_msi(pci_dev);
}
