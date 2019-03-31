#ifndef __AVALON_DRV_INTERRUPT_H__
#define __AVALON_DRV_INTERRUPT_H__

#include "avalon-drv.h"

int init_interrupts(struct avalon_dev *avalon_dev);
void term_interrupts(struct avalon_dev *avalon_dev);

#endif
