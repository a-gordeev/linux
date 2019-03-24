#include <linux/kernel.h>

#include "avalon-dma.h"

#ifdef AVALON_DEBUG_STATS

#define __PRINT_SAFE(ret, buf, size, format, args...)	\
do {							\
	int __ret = snprintf(buf, size, format, args);	\
	if (WARN_ON(__ret < 0))				\
		return __ret;				\
							\
	ret += __ret;					\
	buf += __ret;					\
	size -= __ret;					\
} while (0)

#define __PRINT_LIST(ret, buf, size, list, name)	\
do {							\
	int __ret = __print_list(buf, size, list, name);\
	if (__ret < 0)					\
		return __ret;				\
							\
	ret += __ret;					\
	buf += __ret;					\
	size -= __ret;					\
} while (0)

#define __PRINT_TIME(ret, buf, size, st, name)		\
do {							\
	int __ret = __print_time(buf, size, st, name);	\
	buf += __ret;					\
	size -= __ret;					\
	ret += __ret;					\
} while (0)

static ssize_t __print_list(char *buf, size_t size,
			    struct list_head *descs,
			    const char* name)
{
	struct avalon_dma_tx_descriptor *desc;
	struct list_head *node, *tmp;
	int i = 0;
	ssize_t ret = 0;

	__PRINT_SAFE(ret, buf, size, "%s {\n", name);

	list_for_each_safe(node, tmp, descs) {
		desc = list_entry(node, struct avalon_dma_tx_descriptor, node);

		__PRINT_SAFE(ret, buf, size,
			    "\t%03d: desc %p type %d dir %d\n",
			    i, desc, desc->type, desc->direction);
		i++;
	}

	__PRINT_SAFE(ret, buf, size, "} %s\n", name);

	return ret;
}

ssize_t avalon_dma_print_lists(char *buf, size_t size,
			       struct avalon_dma *avalon_dma)
{
	unsigned long flags;
	ssize_t ret = 0;

	spin_lock_irqsave(&avalon_dma->lock, flags);

	__PRINT_LIST(ret, buf, size, &avalon_dma->desc_allocated, "allocated");
	__PRINT_LIST(ret, buf, size, &avalon_dma->desc_submitted, "submitted");
	__PRINT_LIST(ret, buf, size, &avalon_dma->desc_issued,    "issued"   );
	__PRINT_LIST(ret, buf, size, &avalon_dma->desc_completed, "completed");

	spin_unlock_irqrestore(&avalon_dma->lock, flags);

	return ret;
}

static ssize_t
__print_time(char *buf, size_t size, struct stats_time *st, char *name)
{
	ssize_t ret = 0;
	s64 div;
	s32 rem;

	__PRINT_SAFE(ret, buf, size, "{ %s\n", name);
	if (likely(st->nr)) {
		div = div_u64_rem(st->min, 1000, &rem);
		__PRINT_SAFE(ret, buf, size, "\tmin %lld.%02d\n", div, rem/10);
		div = div_u64_rem(st->max, 1000, &rem);
		__PRINT_SAFE(ret, buf, size, "\tmax %lld.%02d\n", div, rem/10);
//		div = div_u64_rem(st->total, 1000, &rem);
//		__PRINT_SAFE(ret, buf, size, "\ttot %lld.%02d\n", div, rem/10);
		div = div_u64(st->total, st->nr);
		div = div_u64_rem(div, 1000, &rem);
		__PRINT_SAFE(ret, buf, size, "\tavg %lld.%02d\n", div, rem/10);
		
//		__PRINT_SAFE(ret, buf, size, "\tnum %ld\n", st->nr);
	}
	__PRINT_SAFE(ret, buf, size, "} %s\n", name);

	return ret;
}

static ssize_t
__print_int(char *buf, size_t size, struct stats_int *st, char *name)
{
	ssize_t ret = 0;

	__PRINT_SAFE(ret, buf, size, "{ %s\n", name);
	__PRINT_SAFE(ret, buf, size, "\tmax %d\n", st->max);
	__PRINT_SAFE(ret, buf, size, "\tavg %ld\n",
		     st->nr ? st->total / st->nr : 0);
	__PRINT_SAFE(ret, buf, size, "} %s\n", name);

	return ret;
}

ssize_t avalon_dma_print_stats(char *buf, size_t size,
			       struct avalon_dma *avalon_dma)
{
	ssize_t ret = 0;

	__PRINT_TIME(ret, buf, size, &avalon_dma->st_polling, "poll-flags");
	__PRINT_TIME(ret, buf, size, &avalon_dma->st_start_tx, "start-xfers");
	__PRINT_TIME(ret, buf, size, &avalon_dma->st_tasklet, "start-tasklet");
	__PRINT_TIME(ret, buf, size, &avalon_dma->st_hardirq, "do-hardirq");

	ret += __print_int(buf, size, &avalon_dma->st_nr_polls, "max-nr-polls");

	return ret;
}

#undef __PRINT_SAFE
#undef __PRINT_LIST
#undef __PRINT_TIME

#endif
