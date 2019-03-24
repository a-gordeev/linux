#ifndef __AVALON_INTERNAL_DMA_H__
#define __AVALON_INTERNAL_DMA_H__

int avalon_dma_start_xfer(struct avalon_dma *avalon_dma,
			  struct avalon_dma_tx_descriptor *desc);
void avalon_dma_tasklet(unsigned long);

#endif
