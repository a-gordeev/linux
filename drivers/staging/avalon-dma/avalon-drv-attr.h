#ifndef __AVALON_DRV_ATTRS_H__

#include <linux/device.h>

int init_attributes(struct device *dev);
void term_attributes(struct device *dev);

#endif
