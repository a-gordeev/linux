/*
 * Fast and scalable bitmap tagging variant. Uses sparser bitmaps spread
 * over multiple cachelines to avoid ping-pong between multiple submitters
 * or submitter and completer. Uses rolling wakeups to avoid falling of
 * the scaling cliff when we run out of tags and have to start putting
 * submitters to sleep.
 *
 * Uses active queue tracking to support fairer distribution of tags
 * between multiple submitters when a shared tag map is used.
 *
 * Copyright (C) 2013-2014 Jens Axboe
 */
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/sbitmap.h>

static inline int bt_index_inc(int index)
{
	return (index + 1) & (SBM_WAIT_QUEUES - 1);
}

static inline void bt_index_atomic_inc(atomic_t *index)
{
	int old = atomic_read(index);
	int new = bt_index_inc(old);
	atomic_cmpxchg(index, old, new);
}

static int bt_get_word(struct sbitmap_aligned *bm, unsigned int last_tag)
{
	int tag, org_last_tag, end;

	org_last_tag = last_tag;
	end = bm->depth;
	do {
restart:
		tag = find_next_zero_bit(&bm->word, end, last_tag);
		if (unlikely(tag >= end)) {
			/*
			 * We started with an offset, start from 0 to
			 * exhaust the map.
			 */
			if (org_last_tag && last_tag) {
				end = last_tag;
				last_tag = 0;
				goto restart;
			}
			return -1;
		}
		last_tag = tag + 1;
	} while (test_and_set_bit_lock(tag, &bm->word));

	return tag;
}

static int bt_get(struct sbitmap *bt)
{
	unsigned int last_tag, org_last_tag;
	int index, i, tag;

	last_tag = org_last_tag = *this_cpu_ptr(bt->last_tag);
	index = TAG_TO_INDEX(bt, last_tag);

	for (i = 0; i < bt->nr_maps; i++) {
		tag = bt_get_word(&bt->map[index], TAG_TO_BIT(bt, last_tag));
		if (tag != -1) {
			tag += (index << bt->bits_per_word);
			goto done;
		}

		last_tag = 0;
		if (++index >= bt->nr_maps)
			index = 0;
	}

	*this_cpu_ptr(bt->last_tag) = 0;
	return -1;

	/*
	 * Only update the cache from the allocation path, if we ended
	 * up using the specific cached tag.
	 */
done:
	if (tag == org_last_tag) {
		last_tag = tag + 1;
		if (last_tag >= bt->depth - 1)
			last_tag = 0;

		*this_cpu_ptr(bt->last_tag) = last_tag;
	}

	return tag;
}

static struct bt_wait_state *bt_wait_ptr(struct sbitmap *bt)
{
/*
	struct bt_wait_state *bs;
	int wait_index;

	if (!hctx)
		return &bt->bs[0];

	wait_index = atomic_read(&hctx->wait_index);
	bs = &bt->bs[wait_index];
	bt_index_atomic_inc(&hctx->wait_index);
	return bs;
*/
	return &bt->bs[0];
}

int sbitmap_get(struct sbitmap *bt, int state)
{
	struct bt_wait_state *bs;
	DEFINE_WAIT(wait);
	int tag;

	tag = bt_get(bt);
	if (tag != -1)
		return tag;

	if (state != TASK_RUNNING)
		return -ENOSPC;

	bs = bt_wait_ptr(bt);
	do {
		prepare_to_wait(&bs->wait, &wait, state);

		tag = bt_get(bt);
		if (tag != -1)
			break;

		schedule();

		finish_wait(&bs->wait, &wait);
		bs = bt_wait_ptr(bt);
	} while (1);

	finish_wait(&bs->wait, &wait);
	return tag;
}
EXPORT_SYMBOL_GPL(sbitmap_get);

static struct bt_wait_state *bt_wake_ptr(struct sbitmap *bt)
{
	int i, wake_index;

	wake_index = atomic_read(&bt->wake_index);
	for (i = 0; i < SBM_WAIT_QUEUES; i++) {
		struct bt_wait_state *bs = &bt->bs[wake_index];

		if (waitqueue_active(&bs->wait)) {
			int o = atomic_read(&bt->wake_index);
			if (wake_index != o)
				atomic_cmpxchg(&bt->wake_index, o, wake_index);

			return bs;
		}

		wake_index = bt_index_inc(wake_index);
	}

	return NULL;
}

void sbitmap_put(struct sbitmap *bt, int tag)
{
	const int index = TAG_TO_INDEX(bt, tag);
	struct bt_wait_state *bs;
	int wait_cnt;

	BUG_ON(tag >= bt->depth);

	/*
	 * The unlock memory barrier need to order access to req in free
	 * path and clearing tag bit
	 */
	clear_bit_unlock(TAG_TO_BIT(bt, tag), &bt->map[index].word);

	bs = bt_wake_ptr(bt);
	if (!bs)
		return;

	wait_cnt = atomic_dec_return(&bs->wait_cnt);
	if (wait_cnt == 0) {
wake:
		atomic_add(bt->wake_cnt, &bs->wait_cnt);
		bt_index_atomic_inc(&bt->wake_index);
		wake_up(&bs->wait);
	} else if (wait_cnt < 0) {
		wait_cnt = atomic_inc_return(&bs->wait_cnt);
		if (!wait_cnt)
			goto wake;
	}
}
EXPORT_SYMBOL_GPL(sbitmap_put);

static void bt_update_count(struct sbitmap *bt, unsigned int depth)
{
	unsigned int tags_per_word = 1U << bt->bits_per_word;
	unsigned int map_depth = depth;
	int i;

	for (i = 0; i < bt->nr_maps; i++) {
		bt->map[i].depth = min(map_depth, tags_per_word);
		map_depth -= bt->map[i].depth;
	}

	bt->wake_cnt = SBM_WAIT_BATCH;
	if (bt->wake_cnt > depth / 4)
		bt->wake_cnt = max(1U, depth / 4);

	bt->depth = depth;
}

struct sbitmap *sbitmap_alloc(unsigned int depth, int node)
{
	unsigned int nr, tags_per_word;
	struct sbitmap *bt;
	int i;

	BUG_ON(!depth);

	bt = kzalloc_node(sizeof(*bt), GFP_KERNEL, node);
	if (!bt)
		goto err_sbm;

	bt->bits_per_word = ilog2(BITS_PER_LONG);

	tags_per_word = (1 << bt->bits_per_word);

	/*
	 * If the tag space is small, shrink the number of tags
	 * per word so we spread over a few cachelines, at least.
	 * If less than 4 tags, just forget about it, it's not
	 * going to work optimally anyway.
	 */
	if (depth >= 4) {
		while (tags_per_word * 4 > depth) {
			bt->bits_per_word--;
			tags_per_word = (1 << bt->bits_per_word);
		}
	}

	nr = ALIGN(depth, tags_per_word) / tags_per_word;
	bt->map = kzalloc_node(nr * sizeof(bt->map[0]), GFP_KERNEL, node);
	if (!bt->map)
		goto err_map;

	bt->nr_maps = nr;

	bt->bs = kzalloc(SBM_WAIT_QUEUES * sizeof(*bt->bs), GFP_KERNEL);
	if (!bt->bs)
		goto err_bs;

	bt->last_tag = alloc_percpu(int);
	if (!bt->last_tag)
		goto err_last_tag;

	bt_update_count(bt, depth);

	for (i = 0; i < SBM_WAIT_QUEUES; i++) {
		init_waitqueue_head(&bt->bs[i].wait);
		atomic_set(&bt->bs[i].wait_cnt, bt->wake_cnt);
	}

	return bt;

err_last_tag:
	kfree(bt->bs);
err_bs:
	kfree(bt->map);
err_map:
	kfree(bt);
err_sbm:
	return NULL;
}
EXPORT_SYMBOL_GPL(sbitmap_alloc);

void sbitmap_free(struct sbitmap *sbm)
{
	free_percpu(sbm->last_tag);
	kfree(sbm->bs);
	kfree(sbm->map);
	kfree(sbm);
}
EXPORT_SYMBOL_GPL(sbitmap_free);
