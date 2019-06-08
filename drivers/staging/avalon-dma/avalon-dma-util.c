#include <linux/kernel.h>
#include <linux/scatterlist.h>

#include <linux/avalon-dma-hw.h>

#include "avalon-dma-util.h"

void setup_desc(struct dma_descriptor *desc, u32 desc_id,
		u64 destination, u64 source, u32 size)
{
	BUG_ON(!size);
	WARN_ON(!IS_ALIGNED(size, sizeof(u32)));
	BUG_ON(desc_id > (DMA_DESCRIPTOR_MAX - 1));

	desc->src_addr_ldw = cpu_to_le32(source & 0xffffffffUL);
	desc->src_addr_udw = cpu_to_le32((source >> 32));
	desc->dest_addr_ldw = cpu_to_le32(destination & 0xffffffffUL);
	desc->dest_addr_udw = cpu_to_le32((destination >> 32));
	desc->ctl_dma_len = cpu_to_le32((size >> 2) | (desc_id << 18));
	desc->reserved[0] = cpu_to_le32(0x0);
	desc->reserved[1] = cpu_to_le32(0x0);
	desc->reserved[2] = cpu_to_le32(0x0);
}

int setup_descs(struct dma_descriptor *descs, unsigned int desc_id,
		enum dma_data_direction direction,
		dma_addr_t dev_addr, dma_addr_t host_addr, unsigned int len,
		unsigned int *_set)
{
	int nr_descs = 0;
	unsigned int set = 0;
	dma_addr_t src;
	dma_addr_t dest;

	if (direction == DMA_TO_DEVICE) {
		src = host_addr;
		dest = dev_addr;
	} else if (direction == DMA_FROM_DEVICE) {
		src = dev_addr;
		dest = host_addr;
	} else {
		BUG();
		return -EINVAL;
	}

	if (unlikely(desc_id > DMA_DESCRIPTOR_MAX - 1)) {
		BUG();
		return -EINVAL;
	}

	if (WARN_ON(!len))
		return -EINVAL;

	while (len) {
		unsigned int xfer_len = min_t(unsigned int, len, AVALON_MM_DMA_MAX_TANSFER_SIZE);

		setup_desc(descs, desc_id, dest, src, xfer_len);

		set += xfer_len;

		nr_descs++;
		if (nr_descs >= DMA_DESCRIPTOR_MAX)
			break;

		desc_id++;
		if (desc_id >= DMA_DESCRIPTOR_MAX)
			break;

		descs++;

		dest += xfer_len;
		src += xfer_len;

		len -= xfer_len;
	}

	*_set = set;

	return nr_descs;
}

int setup_descs_sg(struct dma_descriptor *descs, unsigned int desc_id,
		   enum dma_data_direction direction,
		   dma_addr_t dev_addr, struct sg_table* sg_table,
		   struct scatterlist *sg_start, unsigned int sg_offset,
		   struct scatterlist **_sg_stop, unsigned int *_sg_set)
{
	struct scatterlist *sg;
	dma_addr_t sg_addr;
	unsigned int sg_len;
	unsigned int sg_set;
	int nr_descs = 0;
	int ret;
	int i;

	/*
	 * Find the SGE that the previous xfer has stopped on - it should exist.
	 */
	for_each_sg(sg_table->sgl, sg, sg_table->nents, i) {
		if (sg == sg_start)
			break;

		dev_addr += sg_dma_len(sg);
	}

	if (WARN_ON(i >= sg_table->nents))
		return -EINVAL;

	/*
	 * The offset can not be longer than the SGE length.
	 */
	sg_len = sg_dma_len(sg);
	if (WARN_ON(sg_len < sg_offset))
		return -EINVAL;

	/*
	 * Skip the starting SGE if it has been fully transmitted.
	 */
	if (sg_offset == sg_len) {
		if (WARN_ON(sg_is_last(sg)))
			return -EINVAL;

		dev_addr += sg_len;
		sg_offset = 0;

		i++;
		sg = sg_next(sg);
	}

	/*
	 * Setup as many SGEs as the controller is able to transmit.
	 */
	BUG_ON(i >= sg_table->nents);
	for (; i < sg_table->nents; i++) {
		sg_addr = sg_dma_address(sg);
		sg_len = sg_dma_len(sg);

		if (sg_offset) {
			if (unlikely(sg_len <= sg_offset)) {
				BUG();
				return -EINVAL;
			}

			dev_addr += sg_offset;
			sg_addr += sg_offset;
			sg_len -= sg_offset;

			sg_offset = 0;
		}

		ret = setup_descs(descs, desc_id, direction,
				  dev_addr, sg_addr, sg_len, &sg_set);
		if (ret < 0)
			return ret;

		if (unlikely((desc_id + ret > DMA_DESCRIPTOR_MAX) ||
			     (nr_descs + ret > DMA_DESCRIPTOR_MAX))) {
			BUG();
			return -ENOMEM;
		}

		nr_descs += ret;
		desc_id += ret;

		if (desc_id >= DMA_DESCRIPTOR_MAX)
			break;

		if (unlikely(sg_len != sg_set)) {
			BUG();
			return -EINVAL;
		}

		if (sg_is_last(sg))
			break;

		descs += ret;
		dev_addr += sg_len;

		sg = sg_next(sg);
	}

	/*
	 * Remember the SGE that next transmission should be started from.
	 */
	BUG_ON(!sg);
	*_sg_stop = sg;
	*_sg_set = sg_set;

	return nr_descs;
}
