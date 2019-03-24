#ifndef __NERDIC_DRV_INTERRUPT_H__
#define __NERDIC_DRV_INTERRUPT_H__

#include "avalon-drv.h"

int init_interrupts(struct nerdic_device *nddc);
void term_interrupts(struct nerdic_device *nddc);

#endif
