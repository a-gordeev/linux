// SPDX-License-Identifier: GPL-2.0
/*
 * Avalon DMA driver
 *
 * Copyright (C) 2018-2019 DAQRI, http://www.daqri.com/
 *
 * Created by Alexander Gordeev <alexander.gordeev@daqri.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of the
 * Linux distribution for more details.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/sched/signal.h>

#include "avalon-drv.h"
#include "avalon-drv-sg-buf.h"
#include "avalon-drv-util.h"

static const gfp_t gfp_flags	= GFP_KERNEL;
static const size_t dma_size	= 2 * AVALON_DMA_MAX_TANSFER_SIZE;
static const int nr_dma_reps	= 2;
static const int dmas_per_cpu	= 8;

char * __dir_str[] = {
	[DMA_BIDIRECTIONAL]	= "DMA_BIDIRECTIONAL",
	[DMA_TO_DEVICE]		= "DMA_TO_DEVICE",
	[DMA_FROM_DEVICE]	= "DMA_FROM_DEVICE",
	[DMA_NONE]		= "DMA_NONE",
}; 
struct xfer_callback_info {
	struct device *dev;
	struct completion completion;
	atomic_t counter;
	ktime_t kt_start;
};

static void init_callback_info(struct xfer_callback_info *info,
			       struct device *dev,
			       int value)
{
	info->dev = dev;
	init_completion(&info->completion);

	atomic_set(&info->counter, value);
	smp_wmb();

	info->kt_start = ktime_get();
}

static int xfer_callback(struct xfer_callback_info *info, const char* pfx)
{
	s64 time_us = ktime_us_delta(ktime_get(), info->kt_start);
	int ret;

	smp_rmb();
	if (atomic_dec_and_test(&info->counter)) {
		complete(&info->completion);
		ret = 0;
	} else {
		ret = 1;
	}

	dev_dbg(info->dev, "%s_%s(%d) done = %d in %lli us",
		pfx, __FUNCTION__, __LINE__, ret, time_us);

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

int xfer_rw(struct avalon_dma *avalon_dma,
	    enum dma_data_direction dir,
	    void __user *user_buf, size_t user_len)
{
	struct device *dev = avalon_dma->dev;
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

	buf = kmalloc(size, gfp_flags);
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

	init_callback_info(&info, dev, nr_reps);

	dev_dbg(dev, "%s(%d) dma_addr %08llx size %lu dir %d reps = %d",
		__FUNCTION__, __LINE__, dma_addr, size, dir, nr_reps);

	for (i = 0; i < nr_reps; i++) {
		ret = avalon_dma_submit_xfer(avalon_dma,
					     dir,
					     TARGET_MEM_BASE,
					     dma_addr, size,
					     xfer_callback, &info);
		if (ret)
			goto dma_submit_err;
	}

	ret = avalon_dma_issue_pending(avalon_dma);
	if (ret)
		goto issue_pending_err;

	ret = wait_for_completion_interruptible(&info.completion);
	if (ret)
		goto wait_err;

	if (dir == DMA_FROM_DEVICE) {
		if (copy_to_user(user_buf, buf, user_len))
			ret = -EFAULT;
	}

wait_err:
issue_pending_err:
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

int xfer_simultaneous(struct avalon_dma *avalon_dma,
		      void __user *user_buf_rd, size_t user_len_rd,
		      void __user *user_buf_wr, size_t user_len_wr)
{
	struct device *dev = avalon_dma->dev;
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

	buf_rd = kmalloc(size, gfp_flags);
	if (!buf_rd) {
		ret = -ENOMEM;
		goto rd_mem_alloc_err;
	}

	buf_wr = kmalloc(size, gfp_flags);
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

	init_callback_info(&info, dev, 2 * nr_reps);

	for (i = 0; i < nr_reps; i++) {
		ret = avalon_dma_submit_xfer(avalon_dma,
					     DMA_TO_DEVICE,
					     target_wr, dma_addr_wr, size,
					     wr_xfer_callback, &info);
		if (ret)
			goto rd_dma_submit_err;
		
		ret = avalon_dma_submit_xfer(avalon_dma,
					     DMA_FROM_DEVICE,
					     target_rd, dma_addr_rd, size,
					     rd_xfer_callback, &info);
		BUG_ON(ret);
		if (ret)
			goto wr_dma_submit_err;
	}

	ret = avalon_dma_issue_pending(avalon_dma);
	BUG_ON(ret);
	if (ret)
		goto issue_pending_err;

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
issue_pending_err:
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

static int kthread_xfer_rw_sg(struct avalon_dma *avalon_dma,
			      enum dma_data_direction dir,
			      dma_addr_t dev_addr, struct sg_table *sgt,
			      void (*xfer_callback)(void *dma_async_param))
{
	struct device *dev = avalon_dma->dev;
	struct xfer_callback_info info;
	int ret = 0;
	int i;

	const int nr_reps = nr_dma_reps;

	while (!kthread_should_stop()) {
		init_callback_info(&info, dev, nr_reps);

		for (i = 0; i < nr_reps; i++) {
			ret = avalon_dma_submit_xfer_sg(avalon_dma,
							dir,
							dev_addr, sgt,
							xfer_callback, &info);
			if (ret) {
				/*
				 * Ideally, kind of avalon_dma_cancel() should
				 * be called here to avoid running out of descs
				 * (ones stuck in "submitted" list since it
				 * never gets processed)
				 *
				 * However, a call to avalon_dma_issue_pending()
				 * would do the job and let all outstanding
				 * descs processed and moved back to "allocated"
				 * queue in the end of the day.
				 *
				 * Leave it as is as a showcase of "allocated"
				 * list empty and "submitted" list full and
				 * unprocessed.
				 */
				goto err;
			}
		}

		ret = avalon_dma_issue_pending(avalon_dma);
		if (ret)
			goto err;

		ret = wait_for_completion_interruptible(&info.completion);
		if (ret)
			goto err;
	}

	return 0;

err:
	dev_dbg(dev, "%s(%d) cpu %d avalon_dma_submit_xfer_sg() %d",
		__FUNCTION__, __LINE__, smp_processor_id(), ret);

	while (!kthread_should_stop())
		cond_resched();

	return ret;
}

struct kthread_xfer_rw_sg_data {
	struct avalon_dma *avalon_dma;
	enum dma_data_direction dir;
	dma_addr_t dev_addr;
	struct sg_table *sgt;
	void (*xfer_callback)(void *dma_async_param);
};

static int __kthread_xfer_rw_sg(void *_data)
{
	struct kthread_xfer_rw_sg_data *data = _data;

	return kthread_xfer_rw_sg(data->avalon_dma,
				  data->dir,
				  data->dev_addr, data->sgt,
				  data->xfer_callback);
}

static int __xfer_rw_sg_smp(struct avalon_dma *avalon_dma,
			    enum dma_data_direction dir,
			    dma_addr_t dev_addr, struct sg_table *sgt,
			    void (*xfer_callback)(void *dma_async_param))
{
	struct kthread_xfer_rw_sg_data data = {
		avalon_dma,
		dir,
		dev_addr,
		sgt,
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

static int __xfer_rw_sg(struct avalon_dma *avalon_dma,
			enum dma_data_direction dir,
			dma_addr_t dev_addr, struct sg_table *sgt,
			void (*xfer_callback)(void *dma_async_param))
{
	struct device *dev = avalon_dma->dev;
	struct xfer_callback_info info;
	int ret = 0;
	int i;

	const int nr_reps = nr_dma_reps;

	init_callback_info(&info, dev, nr_reps);

	for (i = 0; i < nr_reps; i++) {
		ret = avalon_dma_submit_xfer_sg(avalon_dma,
						dir,
						dev_addr, sgt,
						xfer_callback, &info);
		if (ret)
			return ret;
	}


	ret = avalon_dma_issue_pending(avalon_dma);
	if (ret)
		return ret;

	ret = wait_for_completion_interruptible(&info.completion);
	if (ret)
		return ret;

	return 0;
}

static struct vm_area_struct *check_vma(unsigned long addr,
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

int xfer_rw_sg(struct avalon_dma *avalon_dma,
	       enum dma_data_direction dir,
	       void __user *user_buf, size_t user_len,
	       bool is_smp)
{
	struct device *dev = avalon_dma->dev;
	int (*xfer)(struct avalon_dma *avalon_dma,
		    enum dma_data_direction dir,
		    dma_addr_t dev_addr,
		    struct sg_table *sgt,
		    void (*xfer_callback)(void *dma_async_param));
	void (*xfer_callback)(void *dma_async_param);
	struct vm_area_struct *vma;
	struct dma_sg_buf *sg_buf;
	dma_addr_t dma_addr;
	int ret;

	dev_dbg(dev, "%s(%d) { dir %s smp %d",
		__FUNCTION__, __LINE__, __dir_str[dir], is_smp);

	vma = check_vma((unsigned long)user_buf, user_len);
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

	ret = xfer(avalon_dma, dir, dma_addr, &sg_buf->sgt, xfer_callback);
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

int xfer_simultaneous_sg(struct avalon_dma *avalon_dma,
			 void __user *user_buf_rd, size_t user_len_rd,
			 void __user *user_buf_wr, size_t user_len_wr)
{
	struct device *dev = avalon_dma->dev;
	dma_addr_t dma_addr_rd, dma_addr_wr;
	struct xfer_callback_info info;
	struct vm_area_struct *vma_rd, *vma_wr;
	struct dma_sg_buf *sg_buf_rd, *sg_buf_wr;
	int ret;
	int i;

	const int nr_reps = nr_dma_reps;

	dev_dbg(dev, "%s(%d) {", __FUNCTION__, __LINE__);

	vma_rd = check_vma((unsigned long)user_buf_rd, user_len_rd);
	if (IS_ERR(vma_rd))
		return PTR_ERR(vma_rd);

	vma_wr = check_vma((unsigned long)user_buf_wr, user_len_wr);
	if (IS_ERR(vma_wr))
		return PTR_ERR(vma_wr);

	sg_buf_rd = vma_rd->vm_private_data;
	sg_buf_wr = vma_wr->vm_private_data;

	if ((sg_buf_rd->dma_dir != DMA_FROM_DEVICE) ||
	    (sg_buf_wr->dma_dir != DMA_TO_DEVICE))
		return -EINVAL;

	dma_addr_rd = TARGET_MEM_BASE + vma_rd->vm_pgoff * PAGE_SIZE;
	dma_addr_wr = TARGET_MEM_BASE + vma_wr->vm_pgoff * PAGE_SIZE;

	init_callback_info(&info, dev, 2 * nr_reps);

	dma_sync_sg_for_device(dev,
			       sg_buf_rd->sgt.sgl,
			       sg_buf_rd->sgt.nents,
			       DMA_FROM_DEVICE);
	dma_sync_sg_for_device(dev,
			       sg_buf_wr->sgt.sgl,
			       sg_buf_wr->sgt.nents,
			       DMA_TO_DEVICE);

	for (i = 0; i < nr_reps; i++) {
		ret = avalon_dma_submit_xfer_sg(avalon_dma,
						DMA_TO_DEVICE,
						dma_addr_wr,
						&sg_buf_wr->sgt,
						wr_xfer_callback, &info);
		if (ret)
			goto dma_submit_rd_err;
		
		ret = avalon_dma_submit_xfer_sg(avalon_dma,
						DMA_FROM_DEVICE,
						dma_addr_rd,
						&sg_buf_rd->sgt,
						rd_xfer_callback, &info);
		if (ret)
			goto dma_submit_wr_err;
	}

	ret = avalon_dma_issue_pending(avalon_dma);
	if (ret)
		goto issue_pending_err;

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
issue_pending_err:
dma_submit_wr_err:
dma_submit_rd_err:
	dev_dbg(dev, "%s(%d) } = %d", __FUNCTION__, __LINE__, ret);

	return ret;
}
