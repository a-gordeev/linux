// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 *
 * Avalon DMA driver
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/sched/signal.h>
#include <linux/dmaengine.h>

#include "avalon-xfer.h"
#include "avalon-sg-buf.h"

struct callback_info {
	struct completion completion;
	atomic_t counter;
	ktime_t kt_start;
	ktime_t kt_end;
};

static void init_callback_info(struct callback_info *info, int value)
{
	init_completion(&info->completion);

	atomic_set(&info->counter, value);
	smp_wmb();

	info->kt_start = ktime_get();
}

static void xfer_callback(void *arg)
{
	struct callback_info *info = arg;
	info->kt_end = ktime_get();

	smp_rmb();
	if (atomic_dec_and_test(&info->counter))
		complete(&info->completion);
}

static int config_chan(struct dma_chan *chan,
		       enum dma_data_direction dir,
		       dma_addr_t dev_addr)
{
	struct dma_slave_config config = {
		.direction	= dir,
		.src_addr	= dev_addr,
		.dst_addr	= dev_addr,
	};

	return dmaengine_slave_config(chan, &config);
}

static int submit_tx(struct dma_chan *chan,
		     struct dma_async_tx_descriptor *tx,
		     dma_async_tx_callback callback, void *callback_param)
{
	dma_cookie_t cookie;

	tx->callback = callback;
	tx->callback_param = callback_param;

	cookie = dmaengine_submit(tx);
	if (cookie < 0) {
		dmaengine_terminate_sync(chan);
		return cookie;
	}

	return 0;
}

static
int submit_xfer_single(struct dma_chan *chan,
		       enum dma_data_direction dir,
		       dma_addr_t dev_addr,
		       dma_addr_t host_addr, unsigned int size,
		       dma_async_tx_callback callback, void *callback_param)
{
	struct dma_async_tx_descriptor *tx;
	int ret;

	ret = config_chan(chan, dir, dev_addr);
	if (ret)
		return ret;

	tx = dmaengine_prep_slave_single(chan, host_addr, size, dir, 0);
	if (!tx)
		return -ENOMEM;

	ret = submit_tx(chan, tx, callback, callback_param);
	if (ret)
		return ret;

	return 0;
}

static
int submit_xfer_sg(struct dma_chan *chan,
		   enum dma_data_direction dir,
		   dma_addr_t dev_addr,
		   struct scatterlist *sg, unsigned int sg_len,
		   dma_async_tx_callback callback, void *callback_param)
{
	struct dma_async_tx_descriptor *tx;
	int ret;

	ret = config_chan(chan, dir, dev_addr);
	if (ret)
		return ret;

	tx = dmaengine_prep_slave_sg(chan, sg, sg_len, dir, 0);
	if (!tx)
		return -ENOMEM;

	ret = submit_tx(chan, tx, callback, callback_param);
	if (ret)
		return ret;

	return 0;
}

int xfer_single(struct dma_chan *chan,
		enum dma_data_direction dir,
		void __user *user_buf, size_t user_len)
{
	struct device *dev = chan_to_dev(chan);
	dma_addr_t dma_addr;
	void *buf;
	struct callback_info info;
	int ret;
	int i;

	if (user_len < dma_size) {
		return -EINVAL;
	} else {
		user_len = dma_size;
	}

	if ((dir != DMA_TO_DEVICE) && (dir != DMA_FROM_DEVICE))
		return -EINVAL;

	buf = kzalloc(dma_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (dir == DMA_TO_DEVICE) {
		if (copy_from_user(buf, user_buf, user_len)) {
			ret = -EFAULT;
			goto free_buf;
		}
	}

	dma_addr = dma_map_single(dev, buf, dma_size, dir);
	if (dma_mapping_error(dev, dma_addr)) {
		ret = -ENOMEM;
		goto free_buf;
	}

	init_callback_info(&info, nr_dma_reps);

	for (i = 0; i < nr_dma_reps; i++) {
		ret = submit_xfer_single(chan, dir,
					 mem_base, dma_addr, dma_size,
					 xfer_callback, &info);
		if (ret)
			goto unmap_buf;
	}

	dma_async_issue_pending(chan);

	ret = wait_for_completion_interruptible(&info.completion);
	if (ret)
		goto unmap_buf;

	if (dir == DMA_FROM_DEVICE) {
		if (copy_to_user(user_buf, buf, user_len))
			ret = -EFAULT;
	}

unmap_buf:
	dma_unmap_single(dev, dma_addr, dma_size, dir);

free_buf:
	kfree(buf);

	return ret;
}

int xfer_rw_single(struct dma_chan *chan,
		   void __user *user_buf_rd, size_t user_len_rd,
		   void __user *user_buf_wr, size_t user_len_wr)
{
	struct device *dev = chan_to_dev(chan);
	dma_addr_t target_rd = mem_base;
	dma_addr_t target_wr = target_rd + dma_size;
	dma_addr_t dma_addr_rd, dma_addr_wr;
	void *buf_rd, *buf_wr;
	struct callback_info info;
	int ret;
	int i;

	if (user_len_rd < dma_size)
		return -EINVAL;
	else
		user_len_rd = dma_size;

	if (user_len_wr < dma_size)
		return -EINVAL;
	else
		user_len_wr = dma_size;

	buf_rd = kzalloc(dma_size, GFP_KERNEL);
	if (!buf_rd) {
		ret = -ENOMEM;
		goto alloc_err;
	}

	buf_wr = kzalloc(dma_size, GFP_KERNEL);
	if (!buf_wr) {
		ret = -ENOMEM;
		goto free_buf_rd;
	}

	if (copy_from_user(buf_wr, user_buf_wr, user_len_wr)) {
		ret = -EFAULT;
		goto free_buf_wr;
	}

	dma_addr_rd = dma_map_single(dev, buf_rd, dma_size, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, dma_addr_rd)) {
		ret = -ENOMEM;
		goto free_buf_wr;
	}

	dma_addr_wr = dma_map_single(dev, buf_wr, dma_size, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_addr_rd)) {
		ret = -ENOMEM;
		goto unmap_buf_rd;
	}

	init_callback_info(&info, 2 * nr_dma_reps);

	for (i = 0; i < nr_dma_reps; i++) {
		ret = submit_xfer_single(chan, DMA_TO_DEVICE,
					 target_wr, dma_addr_wr, dma_size,
					 xfer_callback, &info);
		if (ret)
			goto unmap_buf_wr;

		ret = submit_xfer_single(chan, DMA_FROM_DEVICE,
					 target_rd, dma_addr_rd, dma_size,
					 xfer_callback, &info);
		if (ret)
			goto unmap_buf_wr;
	}

	dma_async_issue_pending(chan);

	ret = wait_for_completion_interruptible(&info.completion);
	if (ret)
		goto unmap_buf_wr;

	if (copy_to_user(user_buf_rd, buf_rd, user_len_rd))
		ret = -EFAULT;

unmap_buf_wr:
	dma_unmap_single(dev, dma_addr_wr, dma_size, DMA_TO_DEVICE);

unmap_buf_rd:
	dma_unmap_single(dev, dma_addr_rd, dma_size, DMA_FROM_DEVICE);

free_buf_wr:
	kfree(buf_wr);

free_buf_rd:
	kfree(buf_rd);

alloc_err:
	return ret;
}

static int kthread_xfer_rw_sg(struct dma_chan *chan,
			      enum dma_data_direction dir,
			      dma_addr_t dev_addr,
			      struct scatterlist *sg, unsigned int sg_len,
			      dma_async_tx_callback callback)
{
	struct callback_info info;
	int ret;
	int i;

	while (!kthread_should_stop()) {
		init_callback_info(&info, nr_dma_reps);

		for (i = 0; i < nr_dma_reps; i++) {
			ret = submit_xfer_sg(chan, dir,
					     dev_addr, sg, sg_len,
					     callback, &info);
			if (ret)
				return ret;
		}

		dma_async_issue_pending(chan);

		ret = wait_for_completion_interruptible(&info.completion);
		if (ret)
			return ret;
	}

	return 0;
}

struct kthread_xfer_rw_sg_data {
	struct dma_chan *chan;
	enum dma_data_direction direction;
	dma_addr_t dev_addr;
	struct scatterlist *sg;
	unsigned int sg_len;
	dma_async_tx_callback callback;
};

static int __kthread_xfer_rw_sg(void *__data)
{
	struct kthread_xfer_rw_sg_data *data = __data;

	return kthread_xfer_rw_sg(data->chan, data->direction,
				  data->dev_addr, data->sg, data->sg_len,
				  data->callback);
}

static int __xfer_sg_smp(struct dma_chan *chan,
			 enum dma_data_direction dir,
			 dma_addr_t dev_addr,
			 struct scatterlist *sg, unsigned int sg_len,
			 dma_async_tx_callback callback)
{
	struct kthread_xfer_rw_sg_data data = {
		chan, dir,
		dev_addr, sg, sg_len,
		callback
	};
	struct task_struct *task;
	struct task_struct **tasks;
	int nr_tasks = dmas_per_cpu * num_online_cpus();
	int n, cpu;
	int ret = 0;
	int i = 0;

	tasks = kmalloc(sizeof(tasks[0]) * nr_tasks, GFP_KERNEL);
	if (!tasks)
		return -ENOMEM;

	for (n = 0; n < dmas_per_cpu; n++) {
		for_each_online_cpu(cpu) {
			if (i >= nr_tasks) {
				ret = -ENOMEM;
				goto kthread_err;
			}

			task = kthread_create(__kthread_xfer_rw_sg,
					      &data, "avalon-dma-%d-%d",
					      cpu, n);
			if (IS_ERR(task)) {
				ret = PTR_ERR(task);
				goto kthread_err;
			}

			kthread_bind(task, cpu);

			tasks[i] = task;
			i++;
		}
	}

	for (n = 0; n < i; n++)
		wake_up_process(tasks[n]);

	/*
	 * Run child kthreads until user sent a signal (i.e Ctrl+C)
	 * and clear the signal to avid user program from being killed.
	 */
	schedule_timeout_interruptible(MAX_SCHEDULE_TIMEOUT);
	flush_signals(current);

kthread_err:
	while (--i >= 0)
		kthread_stop(tasks[i]);

	kfree(tasks);

	return ret;
}

static int __xfer_sg(struct dma_chan *chan,
		     enum dma_data_direction dir,
		     dma_addr_t dev_addr,
		     struct scatterlist *sg, unsigned int sg_len,
		     dma_async_tx_callback callback)
{
	struct callback_info info;
	int ret;
	int i;

	init_callback_info(&info, nr_dma_reps);

	for (i = 0; i < nr_dma_reps; i++) {
		ret = submit_xfer_sg(chan, dir, dev_addr, sg, sg_len,
				     callback, &info);
		if (ret)
			return ret;
	}

	dma_async_issue_pending(chan);

	ret = wait_for_completion_interruptible(&info.completion);
	if (ret)
		return ret;

	return 0;
}

static struct vm_area_struct *get_vma(unsigned long addr,
				      unsigned long size)
{
	struct vm_area_struct *vma;
	unsigned long vm_size;

	vma = find_vma(current->mm, addr);
	if (!vma || (vma->vm_start != addr))
		return ERR_PTR(-ENXIO);

	vm_size = vma->vm_end - vma->vm_start;
	if (size > vm_size)
		return ERR_PTR(-EINVAL);

	return vma;
}

int xfer_sg(struct dma_chan *chan,
	    enum dma_data_direction dir,
	    void __user *user_buf, size_t user_len,
	    bool is_smp)
{
	struct device *dev = chan_to_dev(chan);
	int (*xfer)(struct dma_chan *chan,
		    enum dma_data_direction dir,
		    dma_addr_t dev_addr,
		    struct scatterlist *sg, unsigned int sg_len,
		    dma_async_tx_callback callback);
	struct vm_area_struct *vma;
	struct dma_sg_buf *sg_buf;
	dma_addr_t dma_addr;
	int ret;

	vma = get_vma((unsigned long)user_buf, user_len);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	sg_buf = vma->vm_private_data;
	if (dir != sg_buf->dma_dir)
		return -EINVAL;

	if (is_smp)
		xfer = __xfer_sg_smp;
	else
		xfer = __xfer_sg;

	dma_addr = mem_base + vma->vm_pgoff * PAGE_SIZE;

	dma_sync_sg_for_device(dev,
			       sg_buf->sgt.sgl, sg_buf->sgt.nents,
			       sg_buf->dma_dir);

	ret = xfer(chan, dir,
		   dma_addr, sg_buf->sgt.sgl, sg_buf->sgt.nents,
		   xfer_callback);

	dma_sync_sg_for_cpu(dev,
			    sg_buf->sgt.sgl, sg_buf->sgt.nents,
			    sg_buf->dma_dir);

	return ret;
}

int xfer_rw_sg(struct dma_chan *chan,
	       void __user *user_buf_rd, size_t user_len_rd,
	       void __user *user_buf_wr, size_t user_len_wr)
{
	struct device *dev = chan_to_dev(chan);
	dma_addr_t dma_addr_rd, dma_addr_wr;
	struct callback_info info;
	struct vm_area_struct *vma_rd, *vma_wr;
	struct dma_sg_buf *sg_buf_rd, *sg_buf_wr;
	int ret;
	int i;

	vma_rd = get_vma((unsigned long)user_buf_rd, user_len_rd);
	if (IS_ERR(vma_rd))
		return PTR_ERR(vma_rd);

	vma_wr = get_vma((unsigned long)user_buf_wr, user_len_wr);
	if (IS_ERR(vma_wr))
		return PTR_ERR(vma_wr);

	sg_buf_rd = vma_rd->vm_private_data;
	sg_buf_wr = vma_wr->vm_private_data;

	if ((sg_buf_rd->dma_dir != DMA_FROM_DEVICE) ||
	    (sg_buf_wr->dma_dir != DMA_TO_DEVICE))
		return -EINVAL;

	dma_addr_rd = mem_base + vma_rd->vm_pgoff * PAGE_SIZE;
	dma_addr_wr = mem_base + vma_wr->vm_pgoff * PAGE_SIZE;

	init_callback_info(&info, 2 * nr_dma_reps);

	dma_sync_sg_for_device(dev,
			       sg_buf_rd->sgt.sgl,
			       sg_buf_rd->sgt.nents,
			       DMA_FROM_DEVICE);
	dma_sync_sg_for_device(dev,
			       sg_buf_wr->sgt.sgl,
			       sg_buf_wr->sgt.nents,
			       DMA_TO_DEVICE);

	for (i = 0; i < nr_dma_reps; i++) {
		ret = submit_xfer_sg(chan, DMA_TO_DEVICE,
				     dma_addr_wr,
				     sg_buf_wr->sgt.sgl,
				     sg_buf_wr->sgt.nents,
				     xfer_callback, &info);
		if (ret)
			goto submit_err;

		ret = submit_xfer_sg(chan, DMA_FROM_DEVICE,
				     dma_addr_rd,
				     sg_buf_rd->sgt.sgl,
				     sg_buf_rd->sgt.nents,
				     xfer_callback, &info);
		if (ret)
			goto submit_err;
	}

	dma_async_issue_pending(chan);

	ret = wait_for_completion_interruptible(&info.completion);

submit_err:
	dma_sync_sg_for_cpu(dev,
			    sg_buf_rd->sgt.sgl,
			    sg_buf_rd->sgt.nents,
			    DMA_FROM_DEVICE);
	dma_sync_sg_for_cpu(dev,
			    sg_buf_wr->sgt.sgl,
			    sg_buf_wr->sgt.nents,
			    DMA_TO_DEVICE);

	return ret;
}
