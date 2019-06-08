#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/uio.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/sched/signal.h>

#include "avalon-drv.h"
#include "avalon-drv-fops.h"
#include "avalon-drv-ioctl.h"
#include "avalon-drv-util.h"
#include "avalon-drv-memmap.h"

static const gfp_t gfp_flags	= GFP_KERNEL;
static const size_t dma_size	= 2 * AVALON_MM_DMA_MAX_TANSFER_SIZE;
static const size_t dma_size_sg	= TARGET_MEM_SIZE / 2;
static const int nr_dma_reps	= 8;
static const int dmas_per_cpu	= 2;

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

static int avalon_dev_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int avalon_dev_release(struct inode *inode, struct file *file)
{
	return 0;
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

	dev_info(info->dev, "%s_%s(%d) done = %d in %lli us",
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

static int ioctl_xfer_rw(struct avalon_dev *avalon_dev,
			 enum dma_data_direction direction,
			 void __user *user_buf, size_t user_len)
{
	struct device *dev = &avalon_dev->pci_dev->dev;
	dma_addr_t dma_addr;
	void *buf;
	struct xfer_callback_info info;
	void (*xfer_callback)(void *dma_async_param);
	int ret;
	int i;

	const size_t size = dma_size;
	const int nr_reps = nr_dma_reps;

	dev_info(dev, "%s(%d) { dir %d", __FUNCTION__, __LINE__, direction);

	if (user_len < size) {
		ret = -EINVAL;
		goto mem_len_err;
	} else {
		user_len = size;
	}

	switch (direction) {
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

	if (direction == DMA_TO_DEVICE) {
		if (copy_from_user(buf, user_buf, user_len)) {
			ret = -EFAULT;
			goto cp_from_user_err;
		}
	}

	dma_addr = dma_map_single(dev, buf, size, direction);
	if (dma_mapping_error(dev, dma_addr)) {
		ret = -ENOMEM;
		goto dma_alloc_err;
	}

	init_callback_info(&info, dev, nr_reps);

	dev_info(dev, "%s(%d) dma_addr %08llx size %lu dir %d reps = %d",
		 __FUNCTION__, __LINE__, dma_addr, size, direction, nr_reps);

	for (i = 0; i < nr_reps; i++) {
		ret = avalon_dma_submit_xfer(&avalon_dev->avalon_dma,
					     direction,
					     TARGET_MEM_BASE, dma_addr, size,
					     xfer_callback, &info);
		if (ret)
			goto dma_submit_err;
	}

	ret = avalon_dma_issue_pending(&avalon_dev->avalon_dma);
	if (ret)
		goto issue_pending_err;

	ret = wait_for_completion_interruptible(&info.completion);
	if (ret)
		goto wait_err;

	if (direction == DMA_FROM_DEVICE) {
		if (copy_to_user(user_buf, buf, user_len))
			ret = -EFAULT;
	}

wait_err:
issue_pending_err:
dma_submit_err:
	dma_unmap_single(dev, dma_addr, size, direction);

dma_alloc_err:
cp_from_user_err:
	kfree(buf);

mem_alloc_err:
dma_dir_err:
mem_len_err:
	dev_info(dev, "%s(%d) } = %d", __FUNCTION__, __LINE__, ret);

	return ret;
}

static int ioctl_xfer_simultaneous(struct avalon_dev *avalon_dev,
				   void __user *user_buf_rd, size_t user_len_rd,
				   void __user *user_buf_wr, size_t user_len_wr)
{
	struct device *dev = &avalon_dev->pci_dev->dev;
	dma_addr_t dma_addr_rd, dma_addr_wr;
	void *buf_rd, *buf_wr;
	struct xfer_callback_info info;
	int ret;
	int i;

	const size_t size = dma_size;
	const dma_addr_t target_rd = TARGET_MEM_BASE;
	const dma_addr_t target_wr = target_rd + size;
	const int nr_reps = nr_dma_reps;

	dev_info(dev, "%s(%d) {", __FUNCTION__, __LINE__);

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
		ret = avalon_dma_submit_xfer(&avalon_dev->avalon_dma,
					     DMA_TO_DEVICE,
					     target_wr, dma_addr_wr, size,
					     wr_xfer_callback, &info);
		if (ret)
			goto rd_dma_submit_err;
		
		ret = avalon_dma_submit_xfer(&avalon_dev->avalon_dma,
					     DMA_FROM_DEVICE,
					     target_rd, dma_addr_rd, size,
					     rd_xfer_callback, &info);
		BUG_ON(ret);
		if (ret)
			goto wr_dma_submit_err;
	}

	ret = avalon_dma_issue_pending(&avalon_dev->avalon_dma);
	BUG_ON(ret);
	if (ret)
		goto issue_pending_err;

	dev_info(dev, "%s(%d) dma_addr %08llx/%08llx rd_size %lu wr_size %lu",
		 __FUNCTION__, __LINE__, dma_addr_rd, dma_addr_wr, size, size);

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
	dev_info(dev, "%s(%d) } = %d", __FUNCTION__, __LINE__, ret);

	return ret;
}

static int kthread_xfer_rw_sg(struct avalon_dma *avalon_dma,
			      enum dma_data_direction direction,
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
							direction,
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
	dev_err(dev, "%s(%d) cpu %d avalon_dma_submit_xfer_sg() %d",
		 __FUNCTION__, __LINE__, smp_processor_id(), ret);

	while (!kthread_should_stop())
		cond_resched();

	return ret;
}

struct kthread_xfer_rw_sg_data {
	struct avalon_dma *avalon_dma;
	enum dma_data_direction direction;
	dma_addr_t dev_addr;
	struct sg_table *sgt;
	void (*xfer_callback)(void *dma_async_param);
};

static int __kthread_xfer_rw_sg(void *_data)
{
	struct kthread_xfer_rw_sg_data *data = _data;

	return kthread_xfer_rw_sg(data->avalon_dma,
				  data->direction,
				  data->dev_addr, data->sgt,
				  data->xfer_callback);
}

static int xfer_rw_sg_smp(struct avalon_dma *avalon_dma,
			  enum dma_data_direction direction,
			  dma_addr_t dev_addr, struct sg_table *sgt,
			  void (*xfer_callback)(void *dma_async_param))
{
	struct kthread_xfer_rw_sg_data data = {
		avalon_dma,
		direction,
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

static int xfer_rw_sg(struct avalon_dma *avalon_dma,
		      enum dma_data_direction direction,
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
						direction,
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

static int ioctl_xfer_rw_sg(struct avalon_dev *avalon_dev,
			    enum dma_data_direction direction,
			    void __user *user_buf, size_t user_len,
			    bool is_smp)
{
	struct device *dev = &avalon_dev->pci_dev->dev;
	void (*xfer_callback)(void *dma_async_param);
	struct dma_sg_buf *sg_buf;
	int ret;

	const size_t size = dma_size_sg;

	dev_info(dev, "%s(%d) { dir %d smp %d",
		 __FUNCTION__, __LINE__, direction, is_smp);

	if (user_len < size) {
		ret = -EINVAL;
		goto mem_len_err;
	} else {
		user_len = size;
	}

	switch (direction) {
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

	sg_buf = dma_sg_buf_alloc(dev, size, direction, gfp_flags);
	if (IS_ERR(sg_buf)) {
		ret = PTR_ERR(sg_buf);
		goto sg_buf_alloc_err;
	}

	if (direction == DMA_TO_DEVICE) {
		if (copy_from_user(sg_buf->vaddr, user_buf, user_len)) {
			ret = -EFAULT;
			goto cp_from_user_err;
		}
	}

	dma_sync_sg_for_device(dev,
			       sg_buf->dma_sgt->sgl, sg_buf->dma_sgt->nents,
			       sg_buf->dma_dir);

	if (is_smp) {
		ret = xfer_rw_sg_smp(&avalon_dev->avalon_dma,
				     direction,
				     TARGET_MEM_BASE, sg_buf->dma_sgt,
				     xfer_callback);
	} else {
		ret = xfer_rw_sg(&avalon_dev->avalon_dma,
				 direction,
				 TARGET_MEM_BASE, sg_buf->dma_sgt,
				 xfer_callback);
	}
	if (ret)
		goto xfer_err;

	dma_sync_sg_for_cpu(dev,
			    sg_buf->dma_sgt->sgl, sg_buf->dma_sgt->nents,
			    sg_buf->dma_dir);

	if (direction == DMA_FROM_DEVICE) {
		if (copy_to_user(user_buf, sg_buf->vaddr, user_len))
			ret = -EFAULT;
	}

xfer_err:
cp_from_user_err:
	dma_sg_buf_free(sg_buf);

sg_buf_alloc_err:
dma_dir_err:
mem_len_err:
	dev_info(dev, "%s(%d) } = %d", __FUNCTION__, __LINE__, ret);

	return ret;
}

static int
ioctl_xfer_simultaneous_sg(struct avalon_dev *avalon_dev,
			   void __user *user_buf_rd, size_t user_len_rd,
			   void __user *user_buf_wr, size_t user_len_wr)
{
	struct device *dev = &avalon_dev->pci_dev->dev;
	struct xfer_callback_info info;
	struct dma_sg_buf *sg_buf_rd, *sg_buf_wr;
	int ret;
	int i;

	const size_t size = dma_size_sg;
	const dma_addr_t dma_addr_rd = TARGET_MEM_BASE;
	const dma_addr_t dma_addr_wr = dma_addr_rd + size;
	const int nr_reps = nr_dma_reps;

	dev_info(dev, "%s(%d) {", __FUNCTION__, __LINE__);

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

	sg_buf_rd = dma_sg_buf_alloc(dev, size, DMA_FROM_DEVICE, gfp_flags);
	if (IS_ERR(sg_buf_rd)) {
		ret = PTR_ERR(sg_buf_rd);
		goto sg_buf_rd_alloc_err;
	}

	sg_buf_wr = dma_sg_buf_alloc(dev, size, DMA_TO_DEVICE, gfp_flags);
	if (IS_ERR(sg_buf_wr)) {
		ret = PTR_ERR(sg_buf_wr);
		goto sg_buf_wr_alloc_err;
	}

	if (copy_from_user(sg_buf_wr->vaddr, user_buf_wr, user_len_wr)) {
		ret = -EFAULT;
		goto cp_from_user_err;
	}

	BUG_ON(sg_buf_rd->dma_dir != DMA_FROM_DEVICE);
	dma_sync_sg_for_device(dev,
			       sg_buf_rd->dma_sgt->sgl,
			       sg_buf_rd->dma_sgt->nents,
			       sg_buf_rd->dma_dir);
	BUG_ON(sg_buf_wr->dma_dir != DMA_TO_DEVICE);
	dma_sync_sg_for_device(dev,
			       sg_buf_wr->dma_sgt->sgl,
			       sg_buf_wr->dma_sgt->nents,
			       sg_buf_wr->dma_dir);

	init_callback_info(&info, dev, 2 * nr_reps);

	dev_info(dev, "%s(%d) reps = %d", __FUNCTION__, __LINE__, nr_reps);

	for (i = 0; i < nr_reps; i++) {
		ret = avalon_dma_submit_xfer_sg(&avalon_dev->avalon_dma,
						DMA_TO_DEVICE,
						dma_addr_wr, sg_buf_wr->dma_sgt,
						wr_xfer_callback, &info);
		if (ret)
			goto dma_submit_rd_err;
		
		ret = avalon_dma_submit_xfer_sg(&avalon_dev->avalon_dma,
						DMA_FROM_DEVICE,
						dma_addr_rd, sg_buf_rd->dma_sgt,
						rd_xfer_callback, &info);
		BUG_ON(ret);
		if (ret)
			goto dma_submit_wr_err;
	}

	ret = avalon_dma_issue_pending(&avalon_dev->avalon_dma);
	BUG_ON(ret);
	if (ret)
		goto issue_pending_err;

	ret = wait_for_completion_interruptible(&info.completion);
	if (ret)
		goto wait_err;

	dma_sync_sg_for_cpu(dev,
			    sg_buf_rd->dma_sgt->sgl, sg_buf_rd->dma_sgt->nents,
			    sg_buf_rd->dma_dir);
	dma_sync_sg_for_cpu(dev,
			    sg_buf_wr->dma_sgt->sgl, sg_buf_wr->dma_sgt->nents,
			    sg_buf_wr->dma_dir);

	if (copy_to_user(user_buf_rd, sg_buf_rd->vaddr, user_len_rd))
		ret = -EFAULT;

wait_err:
issue_pending_err:
dma_submit_wr_err:
dma_submit_rd_err:
cp_from_user_err:
	dma_sg_buf_free(sg_buf_wr);

sg_buf_wr_alloc_err:
	dma_sg_buf_free(sg_buf_rd);

sg_buf_rd_alloc_err:
mem_len_err:
	dev_info(dev, "%s(%d) } = %d", __FUNCTION__, __LINE__, ret);

	return ret;
}

static long avalon_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct avalon_dev *avalon_dev = container_of(file->private_data,
		struct avalon_dev, misc_dev);
	struct device *dev = &avalon_dev->pci_dev->dev;
	struct iovec iovec[2];
	void __user *buf = NULL, __user *buf_rd = NULL, __user *buf_wr = NULL;
	size_t len = 0, len_rd = 0, len_wr = 0;
	int ret;

	dev_info(dev, "%s(%d) { cmd %d", __FUNCTION__, __LINE__, cmd);

	switch (cmd) {
	case IOCTL_ALARIC_DMA_READ:
	case IOCTL_ALARIC_DMA_WRITE:
	case IOCTL_ALARIC_DMA_READ_SG:
	case IOCTL_ALARIC_DMA_WRITE_SG:
	case IOCTL_ALARIC_DMA_READ_SG_SMP:
	case IOCTL_ALARIC_DMA_WRITE_SG_SMP:
		if (copy_from_user(iovec, (void*)arg, sizeof(iovec[0]))) {
			ret = -EFAULT;
			goto done;
		}

		buf = iovec[0].iov_base;
		len = iovec[0].iov_len;

		dev_info(dev, "%s(%d) buf %p len %ld",
			 __FUNCTION__, __LINE__, buf, len);

		break;

	case IOCTL_ALARIC_DMA_SIMULTANEOUS:
	case IOCTL_ALARIC_DMA_SIMULTANEOUS_SG:
		if (copy_from_user(iovec, (void*)arg, sizeof(iovec))) {
			ret = -EFAULT;
			goto done;
		}

		buf_rd = iovec[0].iov_base;
		len_rd = iovec[0].iov_len;

		buf_wr = iovec[1].iov_base;
		len_wr = iovec[1].iov_len;

		dev_info(dev,
			 "%s(%d) buf_rd %p len_rd %ld buf_wr %p len_wr %ld",
			 __FUNCTION__, __LINE__,
			 buf_rd, len_rd, buf_wr, len_wr);

		break;
	default:
		ret = -ENOSYS;
		goto done;
	};

	switch (cmd) {
	case IOCTL_ALARIC_DMA_READ:
		ret = ioctl_xfer_rw(avalon_dev, DMA_FROM_DEVICE, buf, len);
		break;
	case IOCTL_ALARIC_DMA_WRITE:
		ret = ioctl_xfer_rw(avalon_dev, DMA_TO_DEVICE, buf, len);
		break;
	case IOCTL_ALARIC_DMA_SIMULTANEOUS:
		ret = ioctl_xfer_simultaneous(avalon_dev,
					      buf_rd, len_rd,
					      buf_wr, len_wr);
		break;
	case IOCTL_ALARIC_DMA_READ_SG:
		ret = ioctl_xfer_rw_sg(avalon_dev, DMA_FROM_DEVICE, buf, len, false);
		break;
	case IOCTL_ALARIC_DMA_WRITE_SG:
		ret = ioctl_xfer_rw_sg(avalon_dev, DMA_TO_DEVICE, buf, len, false);
		break;
	case IOCTL_ALARIC_DMA_READ_SG_SMP:
		ret = ioctl_xfer_rw_sg(avalon_dev, DMA_FROM_DEVICE, buf, len, true);
		break;
	case IOCTL_ALARIC_DMA_WRITE_SG_SMP:
		ret = ioctl_xfer_rw_sg(avalon_dev, DMA_TO_DEVICE, buf, len, true);
		break;
	case IOCTL_ALARIC_DMA_SIMULTANEOUS_SG:
		ret = ioctl_xfer_simultaneous_sg(avalon_dev,
						 buf_rd, len_rd,
						 buf_wr, len_wr);
		break;
	default:
		BUG();
		ret = -ENOSYS;
	};

done:
	dev_info(dev, "%s(%d) } = %d", __FUNCTION__, __LINE__, ret);

	return ret;
}

const struct file_operations avalon_dev_fops = {
	.open		= avalon_dev_open,
	.release	= avalon_dev_release,
	.llseek		= generic_file_llseek,
	.unlocked_ioctl	= avalon_dev_ioctl,
};
