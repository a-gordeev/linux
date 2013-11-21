/*
 * linux/kernel/events/softirq.c
 *
 * Copyright (C) 2013 Red Hat, Inc., Alexander Gordeev
 *
 * This file contains the code for softirq context performance counters
 */

#include <linux/irq.h>
#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/perf_event.h>

struct softirq_event_list {
	struct spinlock		lock;
	struct list_head	events[NR_SOFTIRQS];
};
static DEFINE_PER_CPU(struct softirq_event_list, softirq_event_list);

void __init perf_init_softirq_events(int cpu)
{
	struct softirq_event_list __percpu *list =
		per_cpu_ptr(&softirq_event_list, cpu);
	int i;

	spin_lock_init(&list->lock);

	for (i = 0; i < ARRAY_SIZE(list->events); i++)
		INIT_LIST_HEAD(&list->events[i]);
}

int perf_event_softirq_add(struct perf_event *event)
{
	struct softirq_event_list __percpu *list =
		this_cpu_ptr(&softirq_event_list);
	struct list_head *head = &list->events[event->softirq];

	spin_lock(&list->lock);
	list_add(&event->softirq_list, head);
	spin_unlock(&list->lock);

	return 0;
}

int perf_event_softirq_del(struct perf_event *event)
{
	struct softirq_event_list __percpu *list =
		this_cpu_ptr(&softirq_event_list);

	spin_lock(&list->lock);
	list_del(&event->softirq_list);
	spin_unlock(&list->lock);

	return 0;
}

static void __perf_enable_softirq_events(unsigned int vector, bool enable)
{
	struct perf_event *event;
	struct softirq_event_list __percpu *list =
		this_cpu_ptr(&softirq_event_list);
	struct list_head *head = &list->events[vector];
	unsigned long flags;

	list_for_each_entry(event, head, softirq_list) {
		struct pmu *pmu = event->pmu;
		void (*func)(struct pmu *, int) = enable ?
			perf_pmu_enable_softirq : perf_pmu_disable_softirq;

		/*
		 * PMU expects local interrupts are disabled when
		 * enabling/disabling performance counters.
		 */
		local_irq_save(flags);
		func(pmu, vector);
		local_irq_restore(flags);
	}
}

void perf_enable_softirq_events(int vector)
{
	__perf_enable_softirq_events(vector, true);
}

void perf_disable_softirq_events(int vector)
{
	__perf_enable_softirq_events(vector, false);
}
