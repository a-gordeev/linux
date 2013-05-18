#include <linux/llist.h>

/*
 * Follows drivers/scsi/scsi_lib.c:SCSI_MAX_SG_SEGMENTS upper limit
 */
#define SCSI_MQ_MAX_SG_SEGMENTS	256

extern struct scsi_cmnd *scsi_mq_end_request(struct scsi_cmnd *, int, int, int);
extern void scsi_mq_done(struct scsi_cmnd *);
extern int scsi_mq_alloc_queue(struct Scsi_Host *, struct scsi_device *);
extern void scsi_mq_free_queue(struct Scsi_Host *, struct scsi_device *);
