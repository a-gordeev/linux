#ifndef __AVALON_DMA_STATS_H__
#define __AVALON_DMA_STATS_H__

#undef AVALON_DEBUG_STATS
#ifdef AVALON_DEBUG_STATS

#include <linux/sched/clock.h>

struct stats_time {
	u64 min;
	u64 max;
	u64 total;
	unsigned long nr;
};

struct stats_int {
	unsigned int max;
	unsigned long total;
	unsigned int nr;
};

static inline void st_lt_inc(struct stats_time *st, u64 time)
{
	if (time > st->max)
		st->max = time;

	if (time < st->min || !st->nr)
		st->min = time;

	st->total += time;
	st->nr++;
}

static inline void st_int_inc(struct stats_int *st, unsigned int n)
{
	if (n > st->max)
		st->max = n;

	st->total += n;
	st->nr++;
}

struct avalon_dma;

ssize_t avalon_dma_print_stats(char *buf, size_t size,
			       struct avalon_dma *avalon_dma);
ssize_t avalon_dma_print_lists(char *buf, size_t size,
			       struct avalon_dma *avalon_dma);

#endif

#endif
