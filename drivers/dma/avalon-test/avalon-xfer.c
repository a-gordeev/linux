// SPDX-License-Identifier: GPL-2.0
/*
 * Avalon DMA driver
 *
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
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
#include "avalon-util.h"

static const size_t dma_size	= TARGET_DMA_SIZE;
static const int nr_dma_reps	= 2;
static const int dmas_per_cpu	= 8;

char *__dir_str[] = {
	[DMA_BIDIRECTIONAL]	= "DMA_BIDIRECTIONAL",
	[DMA_TO_DEVICE]		= "DMA_TO_DEVICE",
	[DMA_FROM_DEVICE]	= "DMA_FROM_DEVICE",
	[DMA_NONE]		= "DMA_NONE",
};

struct xfer_callback_info {
	struct completion completion;
	atomic_t counter;
	ktime_t kt_start;
	ktime_t kt_end;
};

static inline struct dma_async_tx_descriptor *__dmaengine_prep_slave_single(
	struct dma_chan *chan, dma_addr_t buf, size_t len,
	enum dma_transfer_direction dir, unsigned long flags)
{
	struct scatterlist *sg;

	sg = kmalloc(sizeof(*sg), GFP_KERNEL);

	sg_init_table(sg, 1);
	sg_dma_address(sg) = buf;
	sg_dma_len(sg) = len;

	if (!chan || !chan->device || !chan->device->device_prep_slave_sg)
		return NULL;

	return chan->device->device_prep_slave_sg(chan, sg, 1,
						  dir, flags, NULL);
}

static void init_callback_info(struct xfer_callback_info *info, int value)
{
	init_completion(&info->completion);

	atomic_set(&info->counter, value);
	smp_wmb();

	info->kt_start = ktime_get();
}

static int xfer_callback(struct xfer_callback_info *info, const char *pfx)
{
	int ret;

	info->kt_end = ktime_get();

	smp_rmb();
	if (atomic_dec_and_test(&info->counter)) {
		complete(&info->completion);
		ret = 0;
	} else {
		ret = 1;
	}


	return ret;
}

static void rd_xfer_callback(void *dma_async_param)
{
	struct xfer_callback_info *info = dma_async_param;

	xfer_callback(info, "rd");

}

static void wr_xfer_callback(void *dma_async_param)
{
	struct xfer_callback_info *info = dma_async_param;

	xfer_callback(info, "wr");
}

static int
submit_xfer_single(struct dma_chan *chan,
		   enum dma_data_direction dir,
		   dma_addr_t dev_addr,
		   dma_addr_t host_addr, unsigned int size,
		   dma_async_tx_callback callback, void *callback_param)
{
	struct dma_async_tx_descriptor *tx;
	struct dma_slave_config config = {
		.direction	= dir,
		.src_addr	= dev_addr,
		.dst_addr	= dev_addr,
	};
	int ret;

	ret = dmaengine_slave_config(chan, &config);
	if (ret)
		return ret;

	tx = __dmaengine_prep_slave_single(chan, host_addr, size, dir, 0);
	if (!tx)
		return -ENOMEM;

	tx->callback = callback;
	tx->callback_param = callback_param;

	ret = dmaengine_submit(tx);
	if (ret < 0)
		return ret;

	return 0;
}

static int
submit_xfer_sg(struct dma_chan *chan,
	       enum dma_data_direction dir,
	       dma_addr_t dev_addr,
	       struct scatterlist *sg, unsigned int sg_len,
	       dma_async_tx_callback callback, void *callback_param)
{
	struct dma_async_tx_descriptor *tx;
	struct dma_slave_config config = {
		.direction	= dir,
		.src_addr	= dev_addr,
		.dst_addr	= dev_addr,
	};
	int ret;

	ret = dmaengine_slave_config(chan, &config);
	if (ret)
		return ret;

	tx = dmaengine_prep_slave_sg(chan, sg, sg_len, dir, 0);
	if (!tx)
		return -ENOMEM;

	tx->callback = callback;
	tx->callback_param = callback_param;

	ret = dmaengine_submit(tx);
	if (ret < 0)
		return ret;

	return 0;
}

int xfer_rw(struct dma_chan *chan,
	    enum dma_data_direction dir,
	    void __user *user_buf, size_t user_len)
{
	struct device *dev = chan_to_dev(chan);
	dma_addr_t dma_addr;
	void *buf;
	struct xfer_callback_info info;
	void (*xfer_callback)(void *dma_async_param);
	int ret;
	int i;

	const size_t size = dma_size;
	const int nr_reps = nr_dma_reps;

	dev_dbg(dev, "%s(%d) { dir %s",
		__FUNCTION__, __LINE__, __dir_str[dir]);

	if (user_len < size) {
		ret = -EINVAL;
		goto mem_len_err;
	} else {
		user_len = size;
	}

	switch (dir) {
	case DMA_TO_DEVICE:
		xfer_callback = wr_xfer_callback;
		break;
	case DMA_FROM_DEVICE:
		xfer_callback = rd_xfer_callback;
		break;
	default:
		BUG();
		ret = -EINVAL;
		goto dma_dir_err;
	}

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto mem_alloc_err;
	}

	memset(buf, 0, size);

	if (dir == DMA_TO_DEVICE) {
		if (copy_from_user(buf, user_buf, user_len)) {
			ret = -EFAULT;
			goto cp_from_user_err;
		}
	}

	dma_addr = dma_map_single(dev, buf, size, dir);
	if (dma_mapping_error(dev, dma_addr)) {
		ret = -ENOMEM;
		goto dma_alloc_err;
	}

	init_callback_info(&info, nr_reps);

	dev_dbg(dev, "%s(%d) dma_addr %08llx size %lu dir %d reps = %d",
		__FUNCTION__, __LINE__, dma_addr, size, dir, nr_reps);

	for (i = 0; i < nr_reps; i++) {
		ret = submit_xfer_single(chan, dir,
					 TARGET_MEM_BASE, dma_addr, size,
					 xfer_callback, &info);
		if (ret)
			goto dma_submit_err;
	}

	dma_async_issue_pending(chan);

	ret = wait_for_completion_interruptible(&info.completion);
	if (ret)
		goto wait_err;

	if (dir == DMA_FROM_DEVICE) {
		if (copy_to_user(user_buf, buf, user_len))
			ret = -EFAULT;
	}

wait_err:
dma_submit_err:
	dma_unmap_single(dev, dma_addr, size, dir);

dma_alloc_err:
cp_from_user_err:
	kfree(buf);

mem_alloc_err:
dma_dir_err:
mem_len_err:
	dev_dbg(dev, "%s(%d) } = %d", __FUNCTION__, __LINE__, ret);

	return ret;
}

int xfer_simultaneous(struct dma_chan *chan,
		      void __user *user_buf_rd, size_t user_len_rd,
		      void __user *user_buf_wr, size_t user_len_wr)
{
	struct device *dev = chan_to_dev(chan);
	dma_addr_t dma_addr_rd, dma_addr_wr;
	void *buf_rd, *buf_wr;
	struct xfer_callback_info info;
	int ret;
	int i;

	const size_t size = dma_size;
	const dma_addr_t target_rd = TARGET_MEM_BASE;
	const dma_addr_t target_wr = target_rd + size;
	const int nr_reps = nr_dma_reps;

	dev_dbg(dev, "%s(%d) {", __FUNCTION__, __LINE__);

	if (user_len_rd < size) {
		ret = -EINVAL;
		goto mem_len_err;
	} else {
		user_len_rd = size;
	}

	if (user_len_wr < size) {
		ret = -EINVAL;
		goto mem_len_err;
	} else {
		user_len_wr = size;
	}

	buf_rd = kmalloc(size, GFP_KERNEL);
	if (!buf_rd) {
		ret = -ENOMEM;
		goto rd_mem_alloc_err;
	}

	buf_wr = kmalloc(size, GFP_KERNEL);
	if (!buf_wr) {
		ret = -ENOMEM;
		goto wr_mem_alloc_err;
	}

	memset(buf_rd, 0, size);
	memset(buf_wr, 0, size);

	if (copy_from_user(buf_wr, user_buf_wr, user_len_wr)) {
		ret = -EFAULT;
		goto cp_from_user_err;
	}

	dma_addr_rd = dma_map_single(dev, buf_rd, size, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, dma_addr_rd)) {
		ret = -ENOMEM;
		goto rd_dma_map_err;
	}

	dma_addr_wr = dma_map_single(dev, buf_wr, size, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_addr_rd)) {
		ret = -ENOMEM;
		goto wr_dma_map_err;
	}

	init_callback_info(&info, 2 * nr_reps);

	for (i = 0; i < nr_reps; i++) {
		ret = submit_xfer_single(chan, DMA_TO_DEVICE,
					 target_wr, dma_addr_wr, size,
					 wr_xfer_callback, &info);
		if (ret < 0)
			goto rd_dma_submit_err;

		ret = submit_xfer_single(chan, DMA_FROM_DEVICE,
					 target_rd, dma_addr_rd, size,
					 rd_xfer_callback, &info);
		if (ret < 0)
			goto wr_dma_submit_err;
	}

	dma_async_issue_pending(chan);

	dev_dbg(dev,
		"%s(%d) dma_addr %08llx/%08llx rd_size %lu wr_size %lu",
		__FUNCTION__, __LINE__,
		dma_addr_rd, dma_addr_wr, size, size);

	ret = wait_for_completion_interruptible(&info.completion);
	if (ret)
		goto wait_err;

	if (copy_to_user(user_buf_rd, buf_rd, user_len_rd))
		ret = -EFAULT;

wait_err:
wr_dma_submit_err:
rd_dma_submit_err:
	dma_unmap_single(dev, dma_addr_wr, size, DMA_TO_DEVICE);

wr_dma_map_err:
	dma_unmap_single(dev, dma_addr_rd, size, DMA_FROM_DEVICE);

rd_dma_map_err:
cp_from_user_err:
	kfree(buf_wr);

wr_mem_alloc_err:
	kfree(buf_rd);

rd_mem_alloc_err:
mem_len_err:
	dev_dbg(dev, "%s(%d) } = %d", __FUNCTION__, __LINE__, ret);

	return ret;
}

static int kthread_xfer_rw_sg(struct dma_chan *chan,
			      enum dma_data_direction dir,
			      dma_addr_t dev_addr,
			      struct scatterlist *sg, unsigned int sg_len,
			      void (*xfer_callback)(void *dma_async_param))
{
	struct xfer_callback_info info;
	int ret;
	int i;

	const int nr_reps = nr_dma_reps;

	while (!kthread_should_stop()) {
		init_callback_info(&info, nr_reps);

		for (i = 0; i < nr_reps; i++) {
			ret = submit_xfer_sg(chan, dir,
					    dev_addr, sg, sg_len,
					    xfer_callback, &info);
			if (ret < 0)
				goto err;
		}

		dma_async_issue_pending(chan);

		ret = wait_for_completion_interruptible(&info.completion);
		if (ret)
			goto err;
	}

	return 0;

err:
	while (!kthread_should_stop())
		cond_resched();

	return ret;
}

struct kthread_xfer_rw_sg_data {
	struct dma_chan *chan;
	enum dma_data_direction dir;
	dma_addr_t dev_addr;
	struct scatterlist *sg;
	unsigned int sg_len;
	void (*xfer_callback)(void *dma_async_param);
};

static int __kthread_xfer_rw_sg(void *_data)
{
	struct kthread_xfer_rw_sg_data *data = _data;

	return kthread_xfer_rw_sg(data->chan, data->dir,
				  data->dev_addr, data->sg, data->sg_len,
				  data->xfer_callback);
}

static int __xfer_rw_sg_smp(struct dma_chan *chan,
			    enum dma_data_direction dir,
			    dma_addr_t dev_addr,
			    struct scatterlist *sg, unsigned int sg_len,
			    void (*xfer_callback)(void *dma_async_param))
{
	struct kthread_xfer_rw_sg_data data = {
		chan, dir,
		dev_addr, sg, sg_len,
		xfer_callback
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
					      &data, "av-dma-sg-%d-%d", cpu, n);
			if (IS_ERR(task)) {
				ret = PTR_ERR(task);
				goto kthread_err;
			}

			kthread_bind(task, cpu);

			tasks[i] = task;
			i++;
		}
	}

	for (i = 0; i < nr_tasks; i++)
		wake_up_process(tasks[i]);

	/*
	 * Run child kthreads until user sent a signal (i.e Ctrl+C)
	 * and clear the signal to avid user program from being killed.
	 */
	schedule_timeout_interruptible(MAX_SCHEDULE_TIMEOUT);
	flush_signals(current);

kthread_err:
	for (i = 0; i < nr_tasks; i++)
		kthread_stop(tasks[i]);

	kfree(tasks);

	return ret;
}

static int __xfer_rw_sg(struct dma_chan *chan,
			enum dma_data_direction dir,
			dma_addr_t dev_addr,
			struct scatterlist *sg, unsigned int sg_len,
			void (*xfer_callback)(void *dma_async_param))
{
	struct xfer_callback_info info;
	int ret;
	int i;

	const int nr_reps = nr_dma_reps;

	init_callback_info(&info, nr_reps);

	for (i = 0; i < nr_reps; i++) {
		ret = submit_xfer_sg(chan, dir,
				     dev_addr, sg, sg_len,
				     xfer_callback, &info);
		if (ret < 0)
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

int xfer_rw_sg(struct dma_chan *chan,
	       enum dma_data_direction dir,
	       void __user *user_buf, size_t user_len,
	       bool is_smp)
{
	struct device *dev = chan_to_dev(chan);
	int (*xfer)(struct dma_chan *chan,
		    enum dma_data_direction dir,
		    dma_addr_t dev_addr,
		    struct scatterlist *sg, unsigned int sg_len,
		    void (*xfer_callback)(void *dma_async_param));
	void (*xfer_callback)(void *dma_async_param);
	struct vm_area_struct *vma;
	struct dma_sg_buf *sg_buf;
	dma_addr_t dma_addr;
	int ret;

	dev_dbg(dev, "%s(%d) { dir %s smp %d",
		__FUNCTION__, __LINE__, __dir_str[dir], is_smp);

	vma = get_vma((unsigned long)user_buf, user_len);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	sg_buf = vma->vm_private_data;
	if (dir != sg_buf->dma_dir)
		return -EINVAL;

	if (is_smp)
		xfer = __xfer_rw_sg_smp;
	else
		xfer = __xfer_rw_sg;

	if (dir == DMA_FROM_DEVICE)
		xfer_callback = rd_xfer_callback;
	else
		xfer_callback = wr_xfer_callback;

	dma_addr = TARGET_MEM_BASE + vma->vm_pgoff * PAGE_SIZE;

	if (dir == DMA_TO_DEVICE)
		dump_mem(dev, sg_buf->vaddr, 16);

	dma_sync_sg_for_device(dev,
			       sg_buf->sgt.sgl, sg_buf->sgt.nents,
			       sg_buf->dma_dir);

	ret = xfer(chan, dir,
		   dma_addr, sg_buf->sgt.sgl, sg_buf->sgt.nents,
		   xfer_callback);
	if (ret)
		goto xfer_err;

	dma_sync_sg_for_cpu(dev,
			    sg_buf->sgt.sgl, sg_buf->sgt.nents,
			    sg_buf->dma_dir);

	if (dir == DMA_FROM_DEVICE)
		dump_mem(dev, sg_buf->vaddr, 16);

xfer_err:
	dev_dbg(dev, "%s(%d) } = %d", __FUNCTION__, __LINE__, ret);

	return ret;
}

int xfer_simultaneous_sg(struct dma_chan *chan,
			 void __user *user_buf_rd, size_t user_len_rd,
			 void __user *user_buf_wr, size_t user_len_wr)
{
	struct device *dev = chan_to_dev(chan);
	dma_addr_t dma_addr_rd, dma_addr_wr;
	struct xfer_callback_info info;
	struct vm_area_struct *vma_rd, *vma_wr;
	struct dma_sg_buf *sg_buf_rd, *sg_buf_wr;
	int ret;
	int i;

	const int nr_reps = nr_dma_reps;

	dev_dbg(dev, "%s(%d) {", __FUNCTION__, __LINE__);

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

	dma_addr_rd = TARGET_MEM_BASE + vma_rd->vm_pgoff * PAGE_SIZE;
	dma_addr_wr = TARGET_MEM_BASE + vma_wr->vm_pgoff * PAGE_SIZE;

	init_callback_info(&info, 2 * nr_reps);

	dma_sync_sg_for_device(dev,
			       sg_buf_rd->sgt.sgl,
			       sg_buf_rd->sgt.nents,
			       DMA_FROM_DEVICE);
	dma_sync_sg_for_device(dev,
			       sg_buf_wr->sgt.sgl,
			       sg_buf_wr->sgt.nents,
			       DMA_TO_DEVICE);

	for (i = 0; i < nr_reps; i++) {
		ret = submit_xfer_sg(chan, DMA_TO_DEVICE,
				     dma_addr_wr,
				     sg_buf_wr->sgt.sgl,
				     sg_buf_wr->sgt.nents,
				     wr_xfer_callback, &info);
		if (ret < 0)
			goto dma_submit_rd_err;

		ret = submit_xfer_sg(chan, DMA_FROM_DEVICE,
				     dma_addr_rd,
				     sg_buf_wr->sgt.sgl,
				     sg_buf_wr->sgt.nents,
				     rd_xfer_callback, &info);
		if (ret < 0)
			goto dma_submit_wr_err;
	}

	dma_async_issue_pending(chan);

	ret = wait_for_completion_interruptible(&info.completion);
	if (ret)
		goto wait_err;

	dma_sync_sg_for_cpu(dev,
			    sg_buf_rd->sgt.sgl,
			    sg_buf_rd->sgt.nents,
			    DMA_FROM_DEVICE);
	dma_sync_sg_for_cpu(dev,
			    sg_buf_wr->sgt.sgl,
			    sg_buf_wr->sgt.nents,
			    DMA_TO_DEVICE);

wait_err:
dma_submit_wr_err:
dma_submit_rd_err:
	dev_dbg(dev, "%s(%d) } = %d", __FUNCTION__, __LINE__, ret);

	return ret;
}
