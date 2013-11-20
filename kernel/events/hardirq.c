/*
 * linux/kernel/events/hardirq.c
 *
 * Copyright (C) 2012-2014 Red Hat, Inc., Alexander Gordeev
 *
 * This file contains the code for h/w interrupt context performance counters
 */

#include <linux/perf_event.h>
#include <linux/irq.h>
#include <linux/percpu.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/sort.h>

struct hardirq_event {
	unsigned long		mask;		/* action numbers to count on */
	struct perf_event	*event;		/* event to count */
};

struct hardirq_events {
	int			nr_events;	/* number of events in array */
	struct hardirq_event	events[0];	/* array of events to count */
};

struct active_events {
	int			nr_events;	/* number of allocated events */
	int			nr_active;	/* number of events to count */
	struct perf_event	*events[0];	/* array of events to count */
};

DEFINE_PER_CPU(struct active_events *, active_events);
DEFINE_PER_CPU(int, total_events);

static struct hardirq_events *alloc_desc_events(int cpu, int count)
{
	struct hardirq_events *events;
	size_t size;

	size = offsetof(typeof(*events), events) +
	       count * sizeof(events->events[0]);
	events = kmalloc_node(size, GFP_KERNEL, cpu_to_node(cpu));
	if (unlikely(!events))
		return NULL;

	events->nr_events = count;

	return events;
}

static void free_desc_events(struct hardirq_events *events)
{
	kfree(events);
}

static struct active_events *alloc_active_events(int cpu, int count)
{
	struct active_events *active;
	size_t size;

	size = offsetof(typeof(*active), events) +
	       count * sizeof(active->events[0]);
	active = kmalloc_node(size, GFP_KERNEL, cpu_to_node(cpu));
	if (unlikely(!active))
		return NULL;

	active->nr_events = count;

	return active;
}

static void free_active_events(struct active_events *active)
{
	kfree(active);
}

static int compare_pmus(const void *event1, const void *event2)
{
	return strcmp(((const struct hardirq_event *)event1)->event->pmu->name,
		      ((const struct hardirq_event *)event2)->event->pmu->name);
}

static int max_active_events(struct hardirq_events *events)
{
	/*
	 * TODO Count number of events per action and return the maximum
	 */
	return events->nr_events;
}

static int add_event(struct perf_event *event, int irq, unsigned long mask)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct hardirq_events __percpu **events_ptr;
	struct hardirq_events *events, *events_tmp = NULL;
	struct active_events __percpu *active;
	struct active_events *active_tmp = NULL;
	int cpu, max_active, nr_events;
	unsigned long flags;
	int ret = 0;

	if (!desc)
		return -ENOENT;

	cpu = get_cpu();
	BUG_ON(cpu != event->cpu);

	events_ptr = this_cpu_ptr(desc->events);
	events = *events_ptr;

	nr_events = events ? events->nr_events : 0;
	events_tmp = alloc_desc_events(cpu, nr_events + 1);
	if (!events_tmp) {
		ret = -ENOMEM;
		goto err;
	}

	memmove(events_tmp->events, events->events,
		nr_events * sizeof(events_tmp->events[0]));

	events_tmp->events[nr_events].event = event;
	events_tmp->events[nr_events].mask = mask;

	/*
	 * Group events that belong to same PMUs in contiguous sub-arrays
	 */
	sort(events_tmp->events, events_tmp->nr_events,
	     sizeof(events_tmp->events[0]), compare_pmus, NULL);

	max_active = max_active_events(events_tmp);
	active = this_cpu_read(active_events);

	if (!active || max_active > active->nr_active) {
		active_tmp = alloc_active_events(cpu, max_active);
		if (!active_tmp) {
			ret = -ENOMEM;
			goto err;
		}
	}

	raw_spin_lock_irqsave(&desc->lock, flags);

	swap(events, events_tmp);
	*events_ptr = events;

	if (active_tmp) {
		swap(active, active_tmp);
		this_cpu_write(active_events, active);
	}

	__this_cpu_inc(total_events);

	raw_spin_unlock_irqrestore(&desc->lock, flags);

err:
	put_cpu();

	free_active_events(active_tmp);
	free_desc_events(events_tmp);

	return ret;
}

static int del_event(struct perf_event *event, int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct hardirq_events __percpu **events_ptr;
	struct hardirq_events *events, *events_tmp = NULL;
	struct active_events __percpu *active;
	struct active_events *active_tmp = NULL;
	int cpu, i, nr_events;
	unsigned long flags;
	int ret = 0;

	if (!desc)
		return -ENOENT;

	cpu = get_cpu();
	BUG_ON(cpu != event->cpu);

	events_ptr = this_cpu_ptr(desc->events);
	events = *events_ptr;

	nr_events = events->nr_events;
	for (i = 0; i < nr_events; i++) {
		if (events->events[i].event == event)
			break;
	}

	if (i >= nr_events) {
		ret = -ENOENT;
		goto err;
	}

	if (nr_events > 1) {
		events_tmp = alloc_desc_events(cpu, nr_events - 1);
		if (!events_tmp) {
			ret = -ENOMEM;
			goto err;
		}

		memmove(&events_tmp->events[0], &events->events[0],
			i * sizeof(events->events[0]));
		memmove(&events_tmp->events[i], &events->events[i + 1],
			(nr_events - i - 1) * sizeof(events->events[0]));
	}

	active = this_cpu_read(active_events);

	raw_spin_lock_irqsave(&desc->lock, flags);

	if (!__this_cpu_dec_return(total_events)) {
		swap(active, active_tmp);
		this_cpu_write(active_events, active);
	}

	swap(events, events_tmp);
	*events_ptr = events;

	raw_spin_unlock_irqrestore(&desc->lock, flags);

err:
	put_cpu();

	free_active_events(active_tmp);
	free_desc_events(events_tmp);

	return ret;
}

int perf_event_init_hardirq(void *info)
{
	struct perf_event *event = info;
	struct perf_hardirq_param *param, *param_tmp;
	int ret = 0;

	list_for_each_entry(param, &event->hardirq_list, list) {
		ret = add_event(event, param->irq, param->mask);
		if (ret)
			break;
	}

	if (ret) {
		list_for_each_entry(param_tmp, &event->hardirq_list, list) {
			if (param == param_tmp)
				break;
			del_event(event, param_tmp->irq);
		}
	}

	WARN_ON(ret);
	return ret;
}

int perf_event_term_hardirq(void *info)
{
	struct perf_event *event = info;
	struct perf_hardirq_param *param;
	int ret_tmp, ret = 0;

	list_for_each_entry(param, &event->hardirq_list, list) {
		ret_tmp = del_event(event, param->irq);
		if (!ret)
			ret = ret_tmp;
	}

	WARN_ON(ret);
	return ret;
}

static void update_active_events(struct active_events *active,
				 struct hardirq_events *events,
				 int action_nr)
{
	int i, nr_active = 0;

	for (i = 0; i < events->nr_events; i++) {
		struct hardirq_event *event = &events->events[i];

		if (test_bit(action_nr, &event->mask)) {
			active->events[nr_active] = event->event;
			nr_active++;
		}
	}

	active->nr_active = nr_active;
}

int perf_alloc_hardirq_events(struct irq_desc *desc)
{
	desc->events = alloc_percpu(struct hardirq_events*);
	if (!desc->events)
		return -ENOMEM;
	return 0;
}

void perf_free_hardirq_events(struct irq_desc *desc)
{
	int cpu;

	for_each_possible_cpu(cpu)
		BUG_ON(*per_cpu_ptr(desc->events, cpu));

	free_percpu(desc->events);
}

static void start_stop_events(struct perf_event *events[], int count, bool start)
{
	/*
	 * All events in the list must belong to the same PMU
	 */
	struct pmu *pmu = events[0]->pmu;

	if (start)
		pmu->start_hardirq(events, count);
	else
		pmu->stop_hardirq(events, count);
}

static void start_stop_active(struct active_events *active, bool start)
{
	struct perf_event **first, **last;
	int i;

	first = last = active->events;

	for (i = 0; i < active->nr_active; i++) {
		if ((*last)->pmu != (*first)->pmu) {
			start_stop_events(first, last - first, start);
			first = last;
		}
		last++;
	}

	start_stop_events(first, last - first, start);
}

static void start_stop_desc(struct irq_desc *desc, int action_nr, bool start)
{
	struct hardirq_events __percpu *events;
	struct active_events __percpu *active;

	events = *__this_cpu_ptr(desc->events);
	if (likely(!events))
		return;

	active = __this_cpu_read(active_events);

	/*
	 * Assume events to run do not change between start and stop,
	 * thus no reason to update active events when stopping.
	 */
	if (start)
		update_active_events(active, events, action_nr);

	if (!active->nr_active)
		return;

	start_stop_active(active, start);
}

void perf_start_hardirq_events(struct irq_desc *desc, int action_nr)
{
	start_stop_desc(desc, action_nr, true);
}

void perf_stop_hardirq_events(struct irq_desc *desc, int action_nr)
{
	start_stop_desc(desc, action_nr, false);
}
