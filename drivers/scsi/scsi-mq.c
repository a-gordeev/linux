#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/hrtimer.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_host.h>

#include "scsi_priv.h"
#include "scsi-mq.h"

static int scsi_mq_queue_rq(struct blk_mq_hw_ctx *hctx, struct request *rq)
{
	struct request_queue *q = rq->q;
	struct scsi_device *sdev = q->queuedata;
	struct scsi_cmnd *sc = rq->special;
	struct scatterlist *sg = sc->mq_sgl, *prot_sg = sc->mq_prot_sgl;
	unsigned char *sense_buf = sc->sense_buffer;
	struct scsi_data_buffer *prot_sdb = sc->prot_sdb;
	int rc;
	/*
	 * With blk-mq rq->special pre-allocation, scsi_get_cmd_from_req()
	 * is using the hardware context *sc descriptor
	 */
	if (prot_sdb)
		memset(prot_sdb, 0, sizeof(struct scsi_data_buffer));
	memset(sense_buf, 0, SCSI_SENSE_BUFFERSIZE);
	memset(sc, 0, sizeof(struct scsi_cmnd));
	sc = rq->special;
	sc->request = rq;
	sc->device = sdev;
	sc->prot_sdb = prot_sdb;
	sc->sense_buffer = sense_buf;
	sc->mq_sgl = sg;
	sc->mq_prot_sgl = prot_sg;

	if (q->dma_drain_size && blk_rq_bytes(rq)) {
		/*
		 * make sure space for the drain appears we
		 * know we can do this because max_hw_segments
		 * has been adjusted to be one fewer than the
		 * device can handle
		 */
		rq->nr_phys_segments++;
	}
	/*
	 * Mark the end of the pre-allocated SGL based upon the
	 * incoming number of physical segments from blk-mq
	 */
	if (rq->nr_phys_segments) {
		sc->sdb.table.sgl = sg;
		sc->sdb.table.nents = rq->nr_phys_segments;
		sg_init_table(sc->sdb.table.sgl, rq->nr_phys_segments);
	}

	rc = q->prep_rq_fn(q, rq);
	switch (rc) {
	case BLKPREP_OK:
		break;
	case BLKPREP_DEFER:
		return BLK_MQ_RQ_QUEUE_BUSY;
	case BLKPREP_KILL:
		return BLK_MQ_RQ_QUEUE_ERROR;
	default:
		pr_err("scsi-mq: Unknown q->prep_rq_fn ret: %d\n", rc);
		return BLK_MQ_RQ_QUEUE_ERROR;
	}
	pr_debug("scsi_mq: sc: %p SCSI CDB: : 0x%02x\n", sc, sc->cmnd[0]);
	pr_debug("scsi_mq: scsi_bufflen: %u rq->cmd_len: %u\n",
		 scsi_bufflen(sc), rq->cmd_len);

	sc->cmd_len = COMMAND_SIZE(sc->cmnd[0]);
	/*
	 * Setup optional pre-allocated descriptor pointer if requested by an
	 * scsi-mq enabled LLD
	 */
	if (sdev->host->hostt->cmd_size)
		sc->SCp.ptr = blk_mq_rq_to_pdu(rq) + sizeof(struct scsi_cmnd);

	rc = scsi_dispatch_cmd(sc);
	switch (rc) {
	case 0:
		return BLK_MQ_RQ_QUEUE_OK;
	default:
		pr_err("scsi-mq: Unknown scsi_dispatch_cmd ret: %d\n", rc);
		return BLK_MQ_RQ_QUEUE_ERROR;
	}

	return 0;
}

/*
 * scsi-mq callback from scsi_io_completion -> sc->end_request
 */
struct scsi_cmnd *scsi_mq_end_request(struct scsi_cmnd *sc, int error,
				      int bytes, int requeue)
{
	struct request *rq = sc->request;

	pr_debug("scsi_mq_end_request: sc: %p sc->tag: %d error: %d\n",
		 sc, sc->tag, error);

//FIXME: Add proper blk_mq_end_io residual bytes + requeue
	blk_mq_end_io(rq, error);
//FIXME: Need to do equiv of scsi_next_command to kick hctx..?

	return NULL;
}

void scsi_mq_done(struct scsi_cmnd *sc)
{
	scsi_softirq_done(sc->request);
}

static struct blk_mq_ops scsi_mq_ops = {
	.queue_rq	= scsi_mq_queue_rq,
	.map_queue	= blk_mq_map_queue,
	.alloc_hctx	= blk_mq_alloc_single_hw_queue,
	.free_hctx	= blk_mq_free_single_hw_queue,
};

void scsi_mq_free_sc_map(struct request_queue *q)
{
	struct blk_mq_hw_ctx *hctx;
	struct request *rq;
	struct scsi_cmnd *sc;
	int i, j;

	queue_for_each_hw_ctx(q, hctx, i) {

		for (j = 0; j < hctx->queue_depth; j++) {
			rq = hctx->rqs[j];
			sc = rq->special;

			kfree(sc->mq_sgl);
			kfree(sc->sense_buffer);
			kfree(sc->prot_sdb);
			kfree(sc->mq_prot_sgl);
		}
	}
}

int scsi_mq_alloc_queue(struct Scsi_Host *sh, struct scsi_device *sdev)
{
	struct blk_mq_hw_ctx *hctx;
	struct request_queue *q;
	struct request *rq;
	struct scsi_cmnd *sc;
	int i, j, rc, sgl_size;
	bool prot = true;

	sdev->sdev_mq_reg.ops = &scsi_mq_ops;
	sdev->sdev_mq_reg.queue_depth = min((short)sh->can_queue,
					    sh->cmd_per_lun);
	if (!sdev->sdev_mq_reg.queue_depth) {
		pr_warn("scsi-mq: Got queue_depth=0, defaulting to 1\n");
		sdev->sdev_mq_reg.queue_depth = 1;
	}
	sdev->sdev_mq_reg.cmd_size = sizeof(struct scsi_cmnd) + sh->hostt->cmd_size;
	sdev->sdev_mq_reg.numa_node = NUMA_NO_NODE;
	sdev->sdev_mq_reg.nr_hw_queues = 1;
	sdev->sdev_mq_reg.flags = BLK_MQ_F_SHOULD_MERGE;

	printk("Calling blk_mq_init_queue: scsi_mq_ops: %p, queue_depth: %d,"
		" cmd_size: %d SCSI cmd_size: %u\n", sdev->sdev_mq_reg.ops,
		sdev->sdev_mq_reg.queue_depth, sdev->sdev_mq_reg.cmd_size,
		sh->hostt->cmd_size);

	q = blk_mq_init_queue(&sdev->sdev_mq_reg, sdev);
	if (!q)
		return -ENOMEM;

	blk_queue_prep_rq(q, scsi_prep_fn);
	sdev->request_queue = q;
	q->queuedata = sdev;

	q->sg_reserved_size = INT_MAX;

	mutex_lock(&q->sysfs_lock);
	rc = elevator_init(q, NULL);
	mutex_unlock(&q->sysfs_lock);
	if (rc) {
		pr_err("elevator_init failed for scsi-mq: %d\n", rc);
		return -ENOMEM;
	}
	/*
	 * Determine if pre-allocation for DIF prot_sdb + mq_prot_sgl is
	 * not enabled by default.
	 */
	if (!scsi_host_get_prot(sh))
		prot = false;

	/*
	 * Set existing Scsi_Host based hardware limits from scsi_lib.c
	 */
	scsi_init_request_queue(q, sh);

	/*
	 * Calculate the minimum number of pre-allocated SGLs needed for sc->mq_sgl
	 */
	if (sh->hostt->sg_tablesize > SCSI_MQ_MAX_SG_SEGMENTS) {
		pr_warn("Host%d sg_tablesize larger than SCSI_MQ_MAX_SG_SEGMENTS %d\n",
			sh->host_no, SCSI_MQ_MAX_SG_SEGMENTS);
		sh->hostt->sg_tablesize = SCSI_MQ_MAX_SG_SEGMENTS;
	}
	sgl_size = sh->hostt->sg_tablesize * sizeof(struct scatterlist);

	/*
	 * Do remaining setup of pre-allocated scsi_cmnd descriptor map for
	 * each scsi-mq hctx
	 */
	queue_for_each_hw_ctx(q, hctx, i) {

		printk("Performing sc map setup on q: %p hctx: %p i: %d\n", q, hctx, i);

		for (j = 0; j < hctx->queue_depth; j++) {
			rq = hctx->rqs[j];
			sc = rq->special;
			memset(sc, 0, sizeof(struct scsi_cmnd));
			sc->device = sdev;

			sc->mq_sgl = kzalloc_node(sgl_size, GFP_KERNEL,
						  sdev->sdev_mq_reg.numa_node);
			if (!sc->mq_sgl) {
				pr_err("Unable to pre-allocate sc->sdb\n");
				goto out;
			}
			sc->sense_buffer = kzalloc_node(SCSI_SENSE_BUFFERSIZE,
					   GFP_KERNEL, sdev->sdev_mq_reg.numa_node);
			if (!sc->sense_buffer) {
				pr_err("Unable to pre-allocate sc->sense_buffer\n");
				goto out;
			}

			if (!prot)
				continue;

			sc->prot_sdb = kzalloc_node(sizeof(struct scsi_data_buffer),
						    GFP_KERNEL, sdev->sdev_mq_reg.numa_node);
			if (!sc->prot_sdb) {
				pr_err("Unable to pre-allocate sc->prot_sdb\n");
				goto out;
			}

			sc->mq_prot_sgl = kzalloc_node(sgl_size, GFP_KERNEL,
						       sdev->sdev_mq_reg.numa_node);
			if (!sc->mq_prot_sgl) {
				pr_err("Unable to pre-allocate sc->mq_prot_sgl\n");
				goto out;
			}
		}
	}

	printk("scsi_mq_alloc_queue() complete >>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	return 0;

out:
	scsi_mq_free_sc_map(q);
	blk_mq_free_queue(q);
	return -ENOMEM;
}

void scsi_mq_free_queue(struct Scsi_Host *h, struct scsi_device *sdev)
{
	struct request_queue *q = sdev->request_queue;

	scsi_mq_free_sc_map(q);
	blk_mq_free_queue(q);
}
