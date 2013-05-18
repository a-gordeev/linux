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

static DEFINE_PER_CPU(struct scsi_mq_ipi, ipi_cq);

static int scsi_mq_queue_rq(struct blk_mq_hw_ctx *hctx, struct request *rq)
{
	struct request_queue *q = rq->q;
	struct scsi_device *sdev = q->queuedata;
	struct scsi_cmnd *sc = rq->special;
	struct scatterlist *sg = sc->mq_sgl;
	unsigned char *sense_buf = sc->sense_buffer;
	int rc;
	/*
	 * With blk-mq rq->special pre-allocation, scsi_get_cmd_from_req()
	 * is using the hardware context *sc descriptor
	 */
	memset(sense_buf, 0, SCSI_SENSE_BUFFERSIZE);
	memset(sc, 0, sizeof(struct scsi_cmnd));
	sc = rq->special;
	sc->request = rq;
	sc->device = sdev;
	sc->sense_buffer = sense_buf;
	sc->mq_sgl = sg;
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
#if 0
	printk("scsi_mq: sc: %p SCSI CDB: : 0x%02x\n", sc, sc->cmnd[0]);
	printk("scsi_mq: rq->cmd_len: %u\n", rq->cmd_len);
#endif
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
#if 0
	printk("scsi_mq_end_request: sc: %p sc->tag: %d error: %d\n", sc, sc->tag, error);
#endif

//FIXME: Add proper blk_mq_end_io residual bytes + requeue
	if (rq->end_io) {
#if 0
		printk("scsi_mq_end_request: Calling rq->end_io BLOCK_PC for"
			" CDB: 0x%02x\n", sc->cmnd[0]);
#endif
		rq->end_io(rq, error);
	} else {
#if 0
		printk("scsi_mq_end_request: Calling blk_mq_end_io for CDB: 0x%02x\n",
				sc->cmnd[0]);
#endif
		blk_mq_end_io(rq, error);
	}
//FIXME: Need to do equiv of scsi_next_command to kick hctx..?

	return NULL;
}

void scsi_mq_end_io(void *data)
{
	struct scsi_mq_ipi *ipi = &per_cpu(ipi_cq, smp_processor_id());
	struct llist_node *entry;
	struct scsi_cmnd *sc;
	/*
	 * Invoke existing scsi_softirq_done callback to complete request
	 * via sc->end_request()
	 */
	while ((entry = llist_del_first(&ipi->list)) != NULL) {
		sc = llist_entry(entry, struct scsi_cmnd, ll_list);
		scsi_softirq_done(sc->request);
	}
}

void scsi_mq_end_ipi(struct scsi_cmnd *sc)
{
	struct call_single_data *data = &sc->csd;
	int cpu = get_cpu();
	struct scsi_mq_ipi *ipi = &per_cpu(ipi_cq, cpu);

	sc->ll_list.next = NULL;

	if (llist_add(&sc->ll_list, &ipi->list)) {
		data->func = scsi_mq_end_io;
		data->flags = 0;
		__smp_call_function_single(cpu, data, 0);
	}

	put_cpu();
}

void scsi_mq_done(struct scsi_cmnd *sc)
{
#if 0
	scsi_mq_end_ipi(sc);
#else
	scsi_softirq_done(sc->request);
#endif
}

static struct blk_mq_ops scsi_mq_ops = {
	.queue_rq	= scsi_mq_queue_rq,
	.map_queue	= blk_mq_map_queue,
	.alloc_hctx	= blk_mq_alloc_single_hw_queue,
	.free_hctx	= blk_mq_free_single_hw_queue,
};
#if 0
static struct blk_mq_reg = scsi_mq_reg = {
	.ops		= &scsi_mq_ops,
	.nr_hw_queues	= 1,
	.queue_depth	= 64,
	.cmd_size		= sizeof(struct scsi_cmnd),
	.numa_node	= NUMA_NO_NODE,
	.flags		= BLK_MQ_F_SHOULD_MERGE,
};
#endif

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

			if (sc->sense_buffer)
				kfree(sc->sense_buffer);
		}
	}
}

int scsi_mq_alloc_queue(struct Scsi_Host *sh, struct scsi_device *sdev)
{
	struct blk_mq_hw_ctx *hctx;
	struct request_queue *q;
	struct request *rq;
	struct scsi_cmnd *sc;
	int i, j;

	sdev->sdev_mq_reg.ops = &scsi_mq_ops;
	sdev->sdev_mq_reg.queue_depth = sdev->queue_depth;
	sdev->sdev_mq_reg.cmd_size = sizeof(struct scsi_cmnd) + sh->hostt->cmd_size;
	sdev->sdev_mq_reg.numa_node = NUMA_NO_NODE;
	sdev->sdev_mq_reg.nr_hw_queues = 1;
	sdev->sdev_mq_reg.queue_depth = 64;
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

	if (elevator_init(q, NULL)) {
		dump_stack();
		return -ENOMEM;
	}
	/*
	 * Set existing Scsi_Host based hardware limits from scsi_lib.c
	 */
	scsi_init_request_queue(q, sh);
	/*
	 * Do remaining setup of pre-allocated scsi_cmnd descriptor map for
	 * each scsi-mq hctx
	 */
//FIXME: Do cmd->prot_sdb setup for DIF from scsi_host_alloc_command
	queue_for_each_hw_ctx(q, hctx, i) {

		printk("Performing sc map setup on q: %p hctx: %p i: %d\n", q, hctx, i);

		for (j = 0; j < hctx->queue_depth; j++) {
			rq = hctx->rqs[j];
			sc = rq->special;
			sc->device = sdev;
			sc->ll_list.next = NULL;

//FIXME: Pre-allocation of sg tables based upon max_sectors
			sc->mq_sgl = kzalloc_node(SCSI_MQ_SGL_SIZE,
					GFP_KERNEL, sdev->sdev_mq_reg.numa_node);
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
		}
	}
//FIXME: Check for !NONROT usage
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, q);

	printk("scsi_mq_alloc_queue() complete !! >>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
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

int scsi_mq_init(void)
{
	unsigned int i;

	for_each_possible_cpu(i) {
		struct scsi_mq_ipi *ipi = &per_cpu(ipi_cq, i);

		init_llist_head(&ipi->list);
	}

	return 0;
}

void scsi_mq_exit(void)
{
	return;
}
