/*
 * linux/kernel/events/hardirq.c
 *
 * Copyright (C) 2012-2013 Red Hat, Inc., Alexander Gordeev
 *
 * This file contains the code for h/w interrupt context performance counters
 */

#include <linux/irq.h>
#include <linux/cpumask.h>
#include <linux/perf_event.h>

int alloc_hardirq_perf_events(struct irq_desc *desc)
{
	struct list_head __percpu *head;
	int cpu;

	desc->event_list = alloc_percpu(struct list_head);
	if (!desc->event_list)
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		head = per_cpu_ptr(desc->event_list, cpu);
		INIT_LIST_HEAD(head);
	}

	return 0;
}

void free_hardirq_perf_events(struct irq_desc *desc)
{
	struct list_head __percpu *head;
	int cpu;

	for_each_possible_cpu(cpu) {
		head = per_cpu_ptr(desc->event_list, cpu);
		while (!list_empty(head))
			list_del(head->next);
	}

	free_percpu(desc->event_list);
}

int perf_event_hardirq_add(struct perf_event *event)
{
	struct irq_desc *desc = irq_to_desc(event->hardirq);
	struct list_head __percpu *head;

	WARN_ON(event->cpu != smp_processor_id());

	if (!desc)
		return -ENOENT;

	head = per_cpu_ptr(desc->event_list, event->cpu);

	raw_spin_lock(&desc->lock);
	list_add(&event->hardirq_list, head);
	raw_spin_unlock(&desc->lock);

	return 0;
}

int perf_event_hardirq_del(struct perf_event *event)
{
	struct irq_desc *desc = irq_to_desc(event->hardirq);

	if (!desc)
		return -ENOENT;

	WARN_ON(event->cpu != smp_processor_id());

	raw_spin_lock(&desc->lock);
	list_del(&event->hardirq_list);
	raw_spin_unlock(&desc->lock);

	return 0;
}

static void __enable_hardirq_events(struct irq_desc *desc, bool enable)
{
	struct perf_event *event;
	struct list_head __percpu *head = this_cpu_ptr(desc->event_list);

	list_for_each_entry(event, head, hardirq_list) {
		struct pmu *pmu = event->pmu;
		void (*func)(struct pmu *, int) = enable ?
			perf_pmu_enable_hardirq : perf_pmu_disable_hardirq;
		func(pmu, desc->irq_data.irq);
	}
}

void perf_enable_hardirq_events(struct irq_desc *desc)
{
	__enable_hardirq_events(desc, true);
}

void perf_disable_hardirq_events(struct irq_desc *desc)
{
	__enable_hardirq_events(desc, false);
}
