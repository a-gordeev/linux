/*
 * Percpu tags library, based on percpu IDA library
 *
 * Copyright (C) 2014 RedHat, Inc. Alexander Gordeev
 * Copyright (C) 2013 Datera, Inc. Kent Overstreet
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/hardirq.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/percpu_tags.h>

struct percpu_cache {
	spinlock_t		lock;
	int			cpu;		/* CPU this cache belongs to */
	wait_queue_head_t	wait;		/* tasks waiting for a tag */

	int			nr_alloc;	/* nr of allocated tags */

	int			nr_free;	/* nr of unallocated tags */
	int			freelist[];
};

#define spin_lock_irqsave_cond(lock, cpu, flags)	\
do  {							\
	preempt_disable();				\
							\
	if ((cpu) == smp_processor_id())		\
		local_irq_save(flags);			\
							\
	spin_lock(lock);				\
} while (0)

#define spin_unlock_irqrestore_cond(lock, cpu, flags)	\
do {							\
	int __this_cpu = smp_processor_id();		\
	spin_unlock(lock);				\
							\
	if (cpu == __this_cpu)				\
		local_irq_restore(flags);		\
							\
	preempt_enable();				\
} while (0)

#define double_spin_lock_irqsave_cond(lock1, cpu1, lock2, cpu2, flags)	\
do {									\
	spinlock_t *__lock1 = (lock1);					\
	spinlock_t *__lock2 = (lock2);					\
	int __this_cpu;							\
									\
	if (__lock1 > __lock2)						\
		swap(__lock1, __lock2);					\
									\
	preempt_disable();						\
									\
	__this_cpu = smp_processor_id();				\
	if ((cpu1) == __this_cpu || (cpu2) == __this_cpu)		\
		local_irq_save(flags);					\
									\
	spin_lock(__lock1);						\
	spin_lock_nested(__lock2, SINGLE_DEPTH_NESTING);		\
} while (0)

#define double_spin_unlock_irqrestore_cond(lock1, cpu1,			\
					   lock2, cpu2, flags)		\
do {									\
	spinlock_t *__lock1 = (lock1);					\
	spinlock_t *__lock2 = (lock2);					\
	int __this_cpu = smp_processor_id();				\
									\
	if (__lock1 > __lock2)						\
		swap(__lock1, __lock2);					\
									\
	if ((cpu1) == __this_cpu || (cpu2) == __this_cpu)		\
		local_irq_restore(flags);				\
									\
	spin_unlock(__lock2);						\
	spin_unlock(__lock1);						\
									\
	preempt_enable();						\
} while (0)

static inline void cpu_to_tag(struct percpu_tags *pt, int cpu, int tag)
{
	smp_rmb();
	if (pt->tag_cpu_map[tag] != cpu) {
		pt->tag_cpu_map[tag] = cpu;
		smp_wmb();
	}
}

static inline int cpu_from_tag(struct percpu_tags *pt, int tag)
{
	smp_rmb();
	return pt->tag_cpu_map[tag];
}

static void move_tags(int *dst, int *nr_dst, int *src, int *nr_src, size_t nr)
{
	*nr_src -= nr;
	memcpy(dst + *nr_dst, src + *nr_src, sizeof(*dst) * nr);
	*nr_dst += nr;
}

static int batch_size(int nr_cache)
{
	return max(1, min(PERCPU_TAGS_BATCH_MAX, nr_cache / 4));
}

static int cache_size(struct percpu_tags *pt)
{
	int weight;

	/*
	 * Bitmask percpu_tags::alloc_tags is used to indicate the number
	 * of currently active CPUs, although it is unlikely reflects a
	 * CPU activity reliably - we need a better heuristic here.
	 */
	smp_rmb();
	weight = cpumask_weight(&pt->alloc_tags);

	if (weight)
		return pt->nr_tags / weight;
	else
		return pt->nr_tags;
}

static int alloc_tag_local(struct percpu_tags *pt)
{
	bool scarce = pt->nr_tags < cpumask_weight(cpu_online_mask);
	int nr_cache = cache_size(pt);
	struct percpu_cache *tags = this_cpu_ptr(pt->cache);
	int cpu = tags->cpu;
	unsigned long uninitialized_var(flags);
	int tag;

	spin_lock_irqsave_cond(&tags->lock, cpu, flags);

	if (!tags->nr_free ||
	    (!scarce && (tags->nr_free + tags->nr_alloc > nr_cache))) {
		spin_unlock_irqrestore_cond(&tags->lock, cpu, flags);
		return -ENOSPC;
	}

	tags->nr_alloc++;
	tags->nr_free--;

	tag = tags->freelist[tags->nr_free];

	if (!tags->nr_free)
		cpumask_clear_cpu(cpu, &pt->free_tags);
	cpumask_set_cpu(cpu, &pt->alloc_tags);

	spin_unlock_irqrestore_cond(&tags->lock, cpu, flags);

	cpu_to_tag(pt, cpu, tag);

	return tag;
}

static int pull_tag(struct percpu_tags *pt,
		    struct percpu_cache *dtags, struct percpu_cache *stags,
		    bool force)
{
	int nr_cache = cache_size(pt);
	int nr_batch = batch_size(nr_cache);
	int nr_pull;
	unsigned long uninitialized_var(flags);
	int tag;

	double_spin_lock_irqsave_cond(&stags->lock, stags->cpu,
				      &dtags->lock, dtags->cpu, flags);

	if (force) {
		nr_pull = min(nr_batch, stags->nr_free);
	} else {
		nr_pull = stags->nr_free + stags->nr_alloc - nr_cache;
		nr_pull = min(nr_pull, stags->nr_free);
		nr_pull = min(nr_pull, nr_batch);
	}

	if (nr_pull < 1) {
		double_spin_unlock_irqrestore_cond(&dtags->lock, dtags->cpu,
						   &stags->lock, stags->cpu,
						   flags);
		return -ENOSPC;
	}

	move_tags(dtags->freelist, &dtags->nr_free,
		  stags->freelist, &stags->nr_free, nr_pull);

	dtags->nr_alloc++;
	dtags->nr_free--;

	tag = dtags->freelist[dtags->nr_free];

	if (dtags->nr_free)
		cpumask_set_cpu(dtags->cpu, &pt->free_tags);

	if (!stags->nr_free)
		cpumask_clear_cpu(stags->cpu, &pt->free_tags);

	double_spin_unlock_irqrestore_cond(&dtags->lock, dtags->cpu,
					   &stags->lock, stags->cpu, flags);

	cpu_to_tag(pt, dtags->cpu, tag);

	return tag;
}

static wait_queue_head_t *
prepare_to_wait_tag(struct percpu_tags *pt, wait_queue_t *wait, int state)
{
	struct percpu_cache *tags;
	unsigned long flags;

	local_irq_save(flags);
	tags = this_cpu_ptr(pt->cache);
	spin_lock(&tags->lock);

	prepare_to_wait(&tags->wait, wait, state);
	cpumask_set_cpu(tags->cpu, &pt->wait_tags);

	spin_unlock(&tags->lock);
	local_irq_restore(flags);

	return &tags->wait;
}

static int
__alloc_tag_global(struct percpu_tags *pt,
		   int start_cpu, const cpumask_t *cpumask, bool force)
{
	struct percpu_cache *this_tags = per_cpu_ptr(pt->cache, start_cpu);
	struct percpu_cache *remote_tags;
	int cpu = start_cpu;
	int n;
	int tag = -ENOSPC;

	for (n = cpumask_weight(cpumask); n; n--) {
		cpu = cpumask_next(cpu, cpumask);
		if (cpu >= nr_cpu_ids)
			cpu = cpumask_first(cpumask);
		if (cpu >= nr_cpu_ids)
			break;

		if (cpu == start_cpu)
			continue;

		remote_tags = per_cpu_ptr(pt->cache, cpu);
		tag = pull_tag(pt, this_tags, remote_tags, force);
		if (tag >= 0)
			break;
	}

	return tag;
}

static int alloc_tag_global(struct percpu_tags *pt)
{
	int this_cpu = smp_processor_id();
	int tag;

	tag = __alloc_tag_global(pt, this_cpu, &pt->free_tags, false);
	if (tag < 0)
		tag = __alloc_tag_global(pt, this_cpu, &pt->free_tags, true);

	return tag;
}

static int percpu_tags_alloc_nowait(struct percpu_tags *pt)
{
	int tag;

	tag = alloc_tag_local(pt);
	if (tag < 0)
		tag = alloc_tag_global(pt);

	return tag;
}

static int percpu_tags_alloc_wait(struct percpu_tags *pt, int state)
{
	DEFINE_WAIT(wait);
	wait_queue_head_t *wq;
	int tag;

	do {
		wq = prepare_to_wait_tag(pt, &wait, state);

		tag = percpu_tags_alloc_nowait(pt);
		if (tag >= 0)
			break;

		schedule();

		if (signal_pending_state(state, current)) {
			tag = -ERESTARTSYS;
			break;
		}
	} while (1);

	finish_wait(wq, &wait);

	return tag;
}

int percpu_tags_alloc(struct percpu_tags *pt, int state)
{
	int tag;

	if (state == TASK_RUNNING)
		tag = percpu_tags_alloc_nowait(pt);
	else
		tag = percpu_tags_alloc_wait(pt, state);

	return tag;
}
EXPORT_SYMBOL_GPL(percpu_tags_alloc);

static bool __free_tag(struct percpu_tags *pt,
		       struct percpu_cache *tags, int tag, bool force)
{
	int nr_cache = cache_size(pt);
	int cpu = tags->cpu;
	unsigned long uninitialized_var(flags);
	bool ret;

	spin_lock_irqsave_cond(&tags->lock, cpu, flags);

	BUG_ON(tags->nr_free >= pt->nr_tags);
	if (force || (tags->nr_free + tags->nr_alloc < nr_cache)) {
		tags->freelist[tags->nr_free] = tag;
		tags->nr_free++;
		cpumask_set_cpu(cpu, &pt->free_tags);
		ret = true;
	} else {
		ret = false;
	}

	spin_unlock_irqrestore_cond(&tags->lock, cpu, flags);

	return ret;
}

static int free_tag(struct percpu_tags *pt, struct percpu_cache *tags, int tag)
{
	return __free_tag(pt, tags, tag, false);
}

static int push_tag(struct percpu_tags *pt, struct percpu_cache *tags, int tag)
{
	return __free_tag(pt, tags, tag, true);
}

static void dealloc_tag(struct percpu_tags *pt, int cpu, int tag)
{
	struct percpu_cache *tags = per_cpu_ptr(pt->cache, cpu);
	unsigned long uninitialized_var(flags);

	spin_lock_irqsave_cond(&tags->lock, cpu, flags);

	BUG_ON(tags->nr_alloc < 1);
	tags->nr_alloc--;
	if (!tags->nr_alloc)
		cpumask_clear_cpu(tags->cpu, &pt->alloc_tags);

	spin_unlock_irqrestore_cond(&tags->lock, cpu, flags);
}

static int free_tag_scarce(struct percpu_tags *pt, int start_cpu, int tag)
{
	struct percpu_cache *tags;
	int cpu;

	cpu = cpumask_next(start_cpu, &pt->wait_tags);
	if (cpu >= nr_cpu_ids)
		cpu = cpumask_first(&pt->wait_tags);

	if (cpu >= nr_cpu_ids)
		cpu = cpumask_next(start_cpu, &pt->alloc_tags);
	if (cpu >= nr_cpu_ids)
		cpu = cpumask_first(&pt->alloc_tags);

	if (cpu >= nr_cpu_ids)
		cpu = start_cpu;

	tags = per_cpu_ptr(pt->cache, cpu);
	push_tag(pt, tags, tag);

	return cpu;
}

static int __free_tag_normal(struct percpu_tags *pt,
			     int start_cpu, int tag, const cpumask_t *cpumask)
{
	struct percpu_cache *tags;
	int cpu;
	int n;

	tags = per_cpu_ptr(pt->cache, start_cpu);
	if (free_tag(pt, tags, tag))
		return start_cpu;

	cpu = nr_cpu_ids;

	for (n = cpumask_weight(cpumask); n; n--) {
		cpu = cpumask_next(cpu, cpumask);
		if (cpu >= nr_cpu_ids)
			cpu = cpumask_first(cpumask);
		if (cpu >= nr_cpu_ids)
			break;

		if (cpu == start_cpu)
			continue;

		tags = per_cpu_ptr(pt->cache, cpu);
		if (free_tag(pt, tags, tag))
			break;
	}

	return cpu;
}

static int free_tag_normal(struct percpu_tags *pt, int start_cpu, int tag)
{
	struct percpu_cache *tags;
	int cpu;

	cpu = __free_tag_normal(pt, start_cpu, tag, &pt->alloc_tags);

	if (cpu >= nr_cpu_ids)
		cpu = __free_tag_normal(pt, start_cpu, tag, &pt->wait_tags);

	if (cpu >= nr_cpu_ids) {
		cpu = start_cpu;
		tags = per_cpu_ptr(pt->cache, cpu);
		push_tag(pt, tags, tag);
	}

	return cpu;
}

static bool wake_on_cpu(struct percpu_tags *pt, int cpu)
{
	struct percpu_cache *tags = per_cpu_ptr(pt->cache, cpu);
	int wq_active;
	unsigned long uninitialized_var(flags);

	spin_lock_irqsave_cond(&tags->lock, cpu, flags);
	wq_active = waitqueue_active(&tags->wait);
	if (!wq_active)
		cpumask_clear_cpu(cpu, &pt->wait_tags);
	spin_unlock_irqrestore_cond(&tags->lock, cpu, flags);

	if (wq_active)
		wake_up(&tags->wait);

	return wq_active;
}

void percpu_tags_free(struct percpu_tags *pt, int tag)
{
	bool scarce = pt->nr_tags < cpumask_weight(cpu_online_mask);
	int cpu, alloc_cpu;
	int n;

	alloc_cpu = cpu_from_tag(pt, tag);
	dealloc_tag(pt, alloc_cpu, tag);

	if (scarce)
		cpu = free_tag_scarce(pt, alloc_cpu, tag);
	else
		cpu = free_tag_normal(pt, alloc_cpu, tag);

	if (wake_on_cpu(pt, cpu))
		return;

	for (n = cpumask_weight(&pt->wait_tags); n; n--) {
		cpu = cpumask_next(cpu, &pt->wait_tags);
		if (cpu >= nr_cpu_ids)
			cpu = cpumask_first(&pt->wait_tags);
		if (cpu >= nr_cpu_ids)
			break;

		if (wake_on_cpu(pt, cpu))
			break;
	}
}
EXPORT_SYMBOL_GPL(percpu_tags_free);

int percpu_tags_init(struct percpu_tags *pt, int nr_tags)
{
	struct percpu_cache *tags;
	int order;
	int cpu;
	int i;

	order = get_order(nr_tags * sizeof(pt->tag_cpu_map[0]));
	pt->tag_cpu_map = (int*)__get_free_pages(GFP_KERNEL, order);
	if (!pt->tag_cpu_map)
		return -ENOMEM;

	pt->cache = __alloc_percpu(sizeof(*pt->cache) +
				   nr_tags * sizeof(pt->cache->freelist[0]),
				   sizeof(pt->cache->freelist[0]));
	if (!pt->cache) {
		free_pages((unsigned long)pt->tag_cpu_map, order);
		return -ENOMEM;
	}

	pt->nr_tags = nr_tags;

	for_each_possible_cpu(cpu) {
		tags = per_cpu_ptr(pt->cache, cpu);
		tags->cpu = cpu;
		spin_lock_init(&tags->lock);
		init_waitqueue_head(&tags->wait);
	}

	tags = this_cpu_ptr(pt->cache);
	for (i = 0; i < nr_tags; i++)
		tags->freelist[i] = i;
	tags->nr_free = nr_tags;
	cpumask_set_cpu(tags->cpu, &pt->free_tags);

	return 0;
}
EXPORT_SYMBOL_GPL(percpu_tags_init);

void percpu_tags_destroy(struct percpu_tags *pt)
{
	int order = get_order(pt->nr_tags * sizeof(pt->tag_cpu_map[0]));
	free_pages((unsigned long)pt->tag_cpu_map, order);
	free_percpu(pt->cache);
}
EXPORT_SYMBOL_GPL(percpu_tags_destroy);

int percpu_tags_for_each_free(struct percpu_tags *pt,
			      int (*fn)(unsigned, void *), void *data)
{
	unsigned long flags;
	struct percpu_cache *remote;
	unsigned cpu, i, err = 0;

	local_irq_save(flags);
	for_each_possible_cpu(cpu) {
		remote = per_cpu_ptr(pt->cache, cpu);
		spin_lock(&remote->lock);
		for (i = 0; i < remote->nr_free; i++) {
			err = fn(remote->freelist[i], data);
			if (err)
				break;
		}
		spin_unlock(&remote->lock);
		if (err)
			goto out;
	}

out:
	local_irq_restore(flags);
	return err;
}
EXPORT_SYMBOL_GPL(percpu_tags_for_each_free);

int percpu_tags_free_tags(struct percpu_tags *pt, int cpu)
{
	struct percpu_cache *remote;
	if (cpu >= nr_cpu_ids)
		return 0;
	remote = per_cpu_ptr(pt->cache, cpu);
	return remote->nr_free;
}
EXPORT_SYMBOL_GPL(percpu_tags_free_tags);
