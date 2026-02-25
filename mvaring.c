// mvaring_ring.c
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mvaring.h"

struct mvaring * ring_init(void *buff, size_t bufsize)
{
	struct mvaring *r = buff;

	if (!buff)
		return NULL;

	if (bufsize < sizeof(struct mvaring))
		return NULL;

	/* Clean the headroom */
	memset(r, 0, offsetof(struct mvaring, buf) );

	r->version = MVARING_VERSION;
	r->size = sizeof(struct mvaring);

	/* atomic indexes init */
	atomic_init(&r->rindex, 0);
	atomic_init(&r->windex, 0);
	r->writing = 0;
	r->dropped = 0;

	return r;
}

bool ring_is_ok(struct mvaring *r)
{
	if (!r)
		return false;

	if (r->size < sizeof(struct mvaring))
		return false;

	if (r->version != MVARING_VERSION)
		return false;

	return true;
}

bool ring_full(struct mvaring *r)
{
	unsigned w = atomic_load_explicit(&r->windex, memory_order_relaxed);
	unsigned rd = atomic_load_explicit(&r->rindex, memory_order_acquire);

	return (w -1 == rd);
}

bool ring_empty(struct mvaring *r)
{
	unsigned w = atomic_load_explicit(&r->windex, memory_order_relaxed);
	unsigned rd = atomic_load_explicit(&r->rindex, memory_order_acquire);

	return (w == rd);
}

int ring_add(struct mvaring *r, const struct adc_data *data, bool dropfull)
{
	unsigned w = atomic_load_explicit(&r->windex, memory_order_relaxed);
	unsigned rd = atomic_load_explicit(&r->rindex, memory_order_acquire);

	unsigned next_w = w + 1;
	unsigned buf_idx = w & BUFF_MASK;
	int ret = 0;

	/*
	 * TODO: Drop the seqlock as it is not really usefull in our use-case.
	 * My original idea was to avoid atomic write/read indexes and to use
	 * seqlock to protect the data integrity. This, however, feels now like
	 * a bad idea.
	 *
	 * 1. Implementing the read-index update by both writer (when ring is
	 *    full) and by reader (when entries are read) without locks/atomics
	 *    gets tricky. I am not in a mood for tricky it seems.
	 *
	 * 2. We have very frequent writer, writing smallish chunks of data,
	 *    and probably a slower reader (reading larger chunks of data).
	 *    This means our writer is frequently reserving the seqlock for a
	 *    short period at a time. This increases chances that a slower
	 *    reader will need to perfor re-read(s). seqlock would work better
	 *    for writer adding large chuncks - spending longer time fetching
	 *    the data - and fast reader finishing the reads quickly enough to
	 *    keep chances of seqlock to be updated during the read low.
	 */

	/*
	 * Seqlock implementation:
	 * - Increment before write (makes counter ODD = write in progress)
	 * - Increment after write (makes counter EVEN = stable)
	 * - Counter always increases, never decrements
	 *
	 * Reader checks:
	 * 1. If counter is ODD, a write is in progress -> retry
	 * 2. Save counter value before reading
	 * 3. After reading, compare counter value:
	 *    - If changed (even by 2), writer updated during read -> retry
	 *    - If same, read was consistent -> success
	 *
	 * This allows detecting concurrent writes without requiring the writer
	 * to decrement, which would make detection unreliable if a full write
	 * cycle completed during the read.
	 */
	atomic_fetch_add_explicit(&r->writing, 1, memory_order_release);  /* Start write: make counter ODD */

	if (next_w == rd + NUM_DATA_CHUNKS) {
		/* buffer full -> drop oldest */
		r->dropped++;
		ret = ENOSPC;
		if (dropfull)
			goto end;
		atomic_store_explicit(&r->rindex, rd + 1, memory_order_release);
		rd = rd + 1;
	}

	memcpy(&r->buf[buf_idx], data, sizeof(*data));
	atomic_thread_fence(memory_order_release);

	/* publish new writer index */
	atomic_store_explicit(&r->windex, next_w, memory_order_release);
end:
	atomic_fetch_add_explicit(&r->writing, 1, memory_order_release);  /* End write: make counter EVEN (allows reader to detect intervening writes) */

	return ret;
}

int ring_read(struct mvaring *r, struct adc_data *buf, unsigned int num_chunks)
{
	unsigned int tries = 0;
	unsigned int w, rd, available, start, max_contig;
	uint8_t seq1;

	if (!r || !buf || num_chunks == 0)
		return -EINVAL;

retry:
	seq1 = atomic_load_explicit(&r->writing, memory_order_acquire);
	if (seq1 & 1) {
		if (++tries < 1000) {
			/*
			 * TODO: Check if we need to spin here. Other option is
			 * to just keep trying and forget the 'tries' -counter.
			 *
			 * I tried experimenting using sched_yield() and
			 * SCHED_FIFO scheduled processes - and ended up to
			 * deadlocking the system when I forced both processes to
			 * the same core. I need to revise the scheduling if we
			 * want to go that route.
			 */
			SPINAWHILE();
			goto retry;
		}

		return -EAGAIN;
	}

	w = atomic_load_explicit(&r->windex, memory_order_acquire);
	rd = atomic_load_explicit(&r->rindex, memory_order_relaxed);

	available = w - rd;
	if (available == 0)
		return -EAGAIN;

	if (num_chunks > available)
		num_chunks = available;

	start = rd & BUFF_MASK;
	max_contig = NUM_DATA_CHUNKS - start;

	if (max_contig >= num_chunks) {
		memcpy(buf, &r->buf[start], num_chunks * sizeof(*buf));
	} else {
		/* wrap-around copy */
		memcpy(buf, &r->buf[start], max_contig * sizeof(*buf));
		memcpy(&buf[max_contig], &r->buf[0], (num_chunks - max_contig) * sizeof(*buf));
	}

	/* re-check sequence */
	uint8_t seq2 = atomic_load_explicit(&r->writing, memory_order_acquire);
	if (seq1 != seq2) {
		/* writer updated while we read -> retry */
		if (++tries < 1000) {
			SPINAWHILE();
			goto retry;
		}
		return -EAGAIN;
	}

	/* Commit consume by advancing rindex */
	atomic_store_explicit(&r->rindex, rd + num_chunks, memory_order_release);

	return (int)num_chunks;
}

