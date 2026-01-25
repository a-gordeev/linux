// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Alexander Gordeev <a.gordeev.box@gmail.com>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/dmaengine.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <uapi/linux/dma-slave.h>

static bool filter(struct dma_chan *chan, void *filter_param)
{
	const char *name = dma_chan_name(chan);

	if (!name)
		return false;
	return !strcmp(name, (char *)filter_param);
}

static struct dma_chan *dma_slave_request_chan(char *channel_name)
{
	dma_filter_fn filter_fn = NULL;
	struct dma_chan *chan;
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	if (channel_name[0])
		filter_fn = filter;
	chan = dma_request_channel(mask, filter_fn, channel_name);
	if (!chan)
		return ERR_PTR(-ENODEV);

	return chan;
}

static void dma_slave_release_chan(struct dma_chan *chan)
{
	dma_release_channel(chan);
}

static int dma_slave_setup_config(unsigned int cmd,
				  struct dma_slave_config *cfg,
				  struct dma_slave_config_uapi *ucfg,
				  enum dma_data_direction *dir)
{
	if (!IS_ALIGNED((unsigned long)ucfg->data.iov_base, PAGE_SIZE))
		return -EINVAL;
	if (!ucfg->data.iov_len)
		return -EINVAL;
	if (strnlen(ucfg->channel_name, sizeof(ucfg->channel_name)) >= sizeof(ucfg->channel_name))
		return -EINVAL;

	if (ucfg->peripheral_config.iov_len) {
		cfg->peripheral_config = kmalloc(ucfg->peripheral_config.iov_len, GFP_KERNEL);
		if (!cfg->peripheral_config)
			return -ENOMEM;
		if (copy_from_user(cfg->peripheral_config,
				   (void __user *)ucfg->peripheral_config.iov_base,
				   ucfg->peripheral_config.iov_len)) {
			kfree(cfg->peripheral_config);
			return -EFAULT;
		}
		cfg->peripheral_size = ucfg->peripheral_config.iov_len;
	}
	if (cmd == IOCTL_DMA_SLAVE_READ) {
		cfg->direction		= DMA_DEV_TO_MEM;
		*dir			= DMA_FROM_DEVICE;
	} else {
		cfg->direction		= DMA_MEM_TO_DEV;
		*dir			= DMA_TO_DEVICE;
	}
	cfg->src_addr			= ucfg->src_addr;
	cfg->dst_addr			= ucfg->dst_addr;
	cfg->src_addr_width		= ucfg->src_addr_width;
	cfg->dst_addr_width		= ucfg->dst_addr_width;
	cfg->src_maxburst		= ucfg->src_maxburst;
	cfg->dst_maxburst		= ucfg->dst_maxburst;
	cfg->src_port_window_size	= ucfg->src_port_window_size;
	cfg->dst_port_window_size	= ucfg->dst_port_window_size;
	cfg->device_fc			= ucfg->device_fc;

	return 0;
}

static void dma_slave_teardown_config(struct dma_slave_config *cfg)
{
	kfree(cfg->peripheral_config);
}

static void dma_slave_callback(void *callback_param)
{
	complete((struct completion *)callback_param);
}

static long dma_slave_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct dma_async_tx_descriptor *desc;
	struct dma_slave_config_uapi ucfg;
	struct dma_slave_config cfg = {};
	enum dma_data_direction dir;
	struct page **pages;
	unsigned long nr_pages;
	struct dma_chan *chan;
	struct sg_table sgt;
	unsigned int foll;
	dma_cookie_t tx;
	struct completion completion;
	long ret;
	int i;

	switch (cmd) {
	case IOCTL_DMA_SLAVE_READ:
	case IOCTL_DMA_SLAVE_WRITE:
		if (copy_from_user(&ucfg, (void __user *)arg, sizeof(ucfg)))
			return -EFAULT;

		ret = dma_slave_setup_config(cmd, &cfg, &ucfg, &dir);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	};

	chan = dma_slave_request_chan(ucfg.channel_name);
	if (IS_ERR(chan)) {
		ret = PTR_ERR(chan);
		goto err_teardown_config;
	}

	ret = dmaengine_slave_config(chan, &cfg);
	if (ret)
		goto err_release_chan;

	nr_pages = DIV_ROUND_UP(ucfg.data.iov_len, PAGE_SIZE);
	pages = kmalloc_array(nr_pages, sizeof(pages[0]), GFP_KERNEL);
	if (!pages) {
		ret = -ENOMEM;
		goto err_release_chan;
	}

	foll = 0;
	if (cmd == IOCTL_DMA_SLAVE_READ)
		foll |= FOLL_WRITE;
	mmap_read_lock(current->mm);
	ret = pin_user_pages((unsigned long)ucfg.data.iov_base, nr_pages, foll, pages);
	if (ret < 0)
		goto err_mmap_unlock;
	if (ret != nr_pages) {
		nr_pages = ret;
		ret = -EFAULT;
		goto err_unpin_pages;
	}

	ret = sg_alloc_table_from_pages(&sgt, pages, nr_pages, 0, ucfg.data.iov_len, GFP_KERNEL);
	if (ret)
		goto err_unpin_pages;

	ret = dma_map_sgtable(chan->device->dev, &sgt, dir, 0);
	if (ret)
		goto err_free_sgt;

	desc = dmaengine_prep_slave_sg(chan, sgt.sgl, sgt.nents, cfg.direction,
				       DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		ret = -ENOMEM;
		goto err_unmap_sgt;
	}
	init_completion(&completion);
	desc->callback = dma_slave_callback;
	desc->callback_param = &completion;

	tx = dmaengine_submit(desc);
	ret = dma_submit_error(tx);
	if (ret < 0)
		goto err_unmap_sgt;

	dma_async_issue_pending(chan);

	ret = wait_for_completion_interruptible(&completion);
	if (ret)
		goto err_term_sync;

	if (dma_async_is_tx_complete(chan, tx, NULL, NULL) != DMA_COMPLETE) {
		ret = -EIO;
		goto err_term_sync;
	}

	if (cmd == IOCTL_DMA_SLAVE_READ) {
		for (i = 0; i < nr_pages; i++)
			set_page_dirty_lock(pages[i]);
	}

	goto err_unmap_sgt;

err_term_sync:
	dmaengine_terminate_sync(chan);
err_unmap_sgt:
	dma_unmap_sgtable(chan->device->dev, &sgt, dir, 0);
err_free_sgt:
	sg_free_table(&sgt);
err_unpin_pages:
	unpin_user_pages(pages, nr_pages);
err_mmap_unlock:
	mmap_read_unlock(current->mm);
	kfree(pages);
err_release_chan:
	dma_slave_release_chan(chan);
err_teardown_config:
	dma_slave_teardown_config(&cfg);

	return ret;
}

static const struct file_operations dma_slave_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= dma_slave_ioctl,
};

struct miscdevice misc_dev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= DMA_SLAVE_DEVICE,
	.nodename	= DMA_SLAVE_DEVICE,
	.fops		= &dma_slave_fops,
	.mode		= 0600,
};

static int __init dma_slave_init(void)
{
	return misc_register(&misc_dev);
}
late_initcall(dma_slave_init);

static void __exit dma_slave_exit(void)
{
	misc_deregister(&misc_dev);
}
module_exit(dma_slave_exit);

MODULE_AUTHOR("Alexander Gordeev <a.gordeev.box@gmail.com>");
MODULE_DESCRIPTION("DMA slave passthrough driver");
MODULE_LICENSE("GPL");
