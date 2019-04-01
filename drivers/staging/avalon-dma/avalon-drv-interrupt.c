#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include "avalon-drv-interrupt.h"

#define AVALON_IRQ_DMA_STATUS 0
#define AVALON_IRQ_VSYNC      1
#define AVALON_IRQ_ARDC       2
#define AVALON_IRQ_HPS        3
#define AVALON_IRQ_VIO 		AVALON_IRQ_HPS        
#define AVALON_MSI_COUNT	4

#define VSYNC_TIMEOUT	11

static irqreturn_t dma_interrupt(int irq, void *dev_id)
{
	struct avalon_dev *avalon_dev = (struct avalon_dev*)dev_id;

	return avalon_dma_interrupt(&avalon_dev->avalon_dma);
}

static irqreturn_t vio_interrupt(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

static irqreturn_t vsync_interrupt(int irq, void *dev_id)
{
	struct avalon_dev *avalon_dev = (struct avalon_dev*)dev_id;
	struct device *dev = &avalon_dev->pci_dev->dev;

		static ktime_t kt_prev;
		static int cpu_prev;
		int cpu = smp_processor_id();
		ktime_t kt = ktime_get();
		ktime_t kt_diff = ktime_sub(kt, kt_prev);
		s64 ms_diff = ktime_to_ms(kt_diff);

	BUG_ON(irq - avalon_dev->pci_dev->irq != AVALON_IRQ_VSYNC);

		kt_prev = kt;
		cpu_prev = cpu;

		if (ms_diff < VSYNC_TIMEOUT || ms_diff > VSYNC_TIMEOUT) {
/*
			char *s = ms_diff < VSYNC_TIMEOUT ? "early" : "late";

			dev_warn(&nddc->pci_dev->dev,
				 "Spurious %s VSYNC on CPU%d after %lld us on CPU%d",
				 s, cpu, ktime_to_us(kt_diff), cpu_prev);
*/

			return IRQ_NONE;
		}

	return IRQ_HANDLED;
}

static irqreturn_t ardc_interrupt(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

int init_interrupts(struct avalon_dev *avalon_dev)
{
	struct pci_dev *dev = avalon_dev->pci_dev;
	int rc;
	int i;

	rc = pci_alloc_irq_vectors(dev, AVALON_MSI_COUNT, AVALON_MSI_COUNT, PCI_IRQ_MSI);
	BUG_ON(rc != AVALON_MSI_COUNT);
	dev_info(&dev->dev, "pci_enable_msi() successful. Allocated %d msi interrupts\n", AVALON_MSI_COUNT);

		for (i = 0; i < AVALON_MSI_COUNT; i++) {
			int vec = pci_irq_vector(dev, i);
			dev_info(&dev->dev, "requesting irq for vector: %d\n", vec);

 			switch (i) {
 			case AVALON_IRQ_DMA_STATUS:
	 			rc = request_irq(vec, dma_interrupt, IRQF_SHARED, DRIVER_NAME, (void *)avalon_dev);
 				break;
 			case AVALON_IRQ_VSYNC:
 				rc = request_irq(vec, vsync_interrupt, IRQF_SHARED, DRIVER_NAME, (void *)avalon_dev);
 				break;
 			case AVALON_IRQ_ARDC:
 				rc = request_irq(vec, ardc_interrupt, IRQF_SHARED, DRIVER_NAME, (void *)avalon_dev);
	 			break;
 			case AVALON_IRQ_VIO:
 				rc = request_irq(vec, vio_interrupt, IRQF_SHARED, DRIVER_NAME, (void*)avalon_dev);
				break;
			default:
				BUG();
			}

			if (rc) {
				dev_err(&dev->dev, "Failed to request IRQ for vector %d\n", vec);
				break;
			}
		}

	return rc;
}

void term_interrupts(struct avalon_dev *avalon_dev)
{
	struct pci_dev *dev = avalon_dev->pci_dev;
	int i;

	for (i = 0; i < AVALON_MSI_COUNT; i++) {
		int vec = pci_irq_vector(dev, i);
		dev_dbg(&dev->dev, "Disabling IRQ #%d", vec);
		free_irq(vec, (void*)avalon_dev);
	}

	pci_disable_msi(dev);
}
