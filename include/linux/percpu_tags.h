#ifndef __PERCPU_TAGS_H__
#define __PERCPU_TAGS_H__

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/spinlock_types.h>
#include <linux/wait.h>
#include <linux/cpumask.h>

struct percpu_cache;

struct percpu_tags {
	int				nr_tags;
	struct percpu_cache __percpu	*cache;

	int				*tag_cpu_map;

	cpumask_t			alloc_tags;
	cpumask_t			free_tags;
	cpumask_t			wait_tags;
};

int percpu_tags_alloc(struct percpu_tags *pt, int state);
void percpu_tags_free(struct percpu_tags *pt, int tag);

int percpu_tags_init(struct percpu_tags *pt, int nr_tags);
void percpu_tags_destroy(struct percpu_tags *pt);

int percpu_tags_for_each_free(struct percpu_tags *pt,
			      int (*fn)(unsigned, void *), void *data);
int percpu_tags_free_tags(struct percpu_tags *pt, int cpu);

#define PERCPU_TAGS_BATCH_MAX	4

#endif /* __PERCPU_TAGS_H__ */
