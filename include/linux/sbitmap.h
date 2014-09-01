#ifndef INT_SBITMAP_H
#define INT_SBITMAP_H

#include <linux/wait.h>

enum {
	SBM_WAIT_QUEUES	= 8,
	SBM_WAIT_BATCH	= 8,
};

struct bt_wait_state {
	atomic_t wait_cnt;
	wait_queue_head_t wait;
} ____cacheline_aligned_in_smp;

#define TAG_TO_INDEX(bt, tag)	((tag) >> (bt)->bits_per_word)
#define TAG_TO_BIT(bt, tag)	((tag) & ((1 << (bt)->bits_per_word) - 1))

/*
 * Basic implementation of sparser bitmap, allowing the user to spread
 * the bits over more cachelines.
 */
struct sbitmap_aligned {
	unsigned long		word;
	unsigned long		depth;
} ____cacheline_aligned_in_smp;

struct sbitmap {
	unsigned int		depth;
	unsigned int		wake_cnt;
	unsigned int		bits_per_word;

	unsigned int		nr_maps;
	struct sbitmap_aligned	*map;

	atomic_t		wake_index;
	struct bt_wait_state	*bs;

	int __percpu		*last_tag;
};

extern struct sbitmap *sbitmap_alloc(unsigned int nr_tags, int node);
extern void sbitmap_free(struct sbitmap *tags);

extern int sbitmap_get(struct sbitmap *bt, int gfp);
extern void sbitmap_put(struct sbitmap *sbm, int tag);

#endif
