#include <linux/kernel.h>

#include "avalon-drv.h"

static ssize_t avalon_dma_stats_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
#ifdef AVALON_DEBUG_STATS
	struct nerdic_device *nddc = dev_get_drvdata(dev);

        return avalon_dma_print_stats(buf, PAGE_SIZE, &nddc->avalon_dma);
#else
	return -ENOSYS;
#endif
}
static DEVICE_ATTR_RO(avalon_dma_stats);

static ssize_t avalon_dma_lists_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
#ifdef AVALON_DEBUG_STATS
	struct nerdic_device *nddc = dev_get_drvdata(dev);

        return avalon_dma_print_lists(buf, PAGE_SIZE, &nddc->avalon_dma);
#else
	return -ENOSYS;
#endif
}
static DEVICE_ATTR_RO(avalon_dma_lists);

int init_attributes(struct device *dev)
{
	device_create_file(dev, &dev_attr_avalon_dma_stats);
	device_create_file(dev, &dev_attr_avalon_dma_lists);

	return 0;
}

void term_attributes(struct device *dev)
{
	device_remove_file(dev, &dev_attr_avalon_dma_lists);
	device_remove_file(dev, &dev_attr_avalon_dma_stats);
}
