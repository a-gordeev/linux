#ifndef __AVALON_DRV_INTERRUPT_H__
#define __AVALON_DRV_INTERRUPT_H__

int init_interrupts(struct avalon_dev *avalon_dev, struct pci_dev *pci_dev);
void term_interrupts(struct avalon_dev *avalon_dev);

#endif
