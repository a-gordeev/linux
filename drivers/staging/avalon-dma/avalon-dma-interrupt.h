#ifndef __AVALON_DMA_INTERRUPT_H__
#define __AVALON_DMA_INTERRUPT_H__

irqreturn_t avalon_dma_interrupt(int irq, void *dev_id);
void avalon_dma_tasklet(unsigned long);

#endif
