//
// Copyright (c) 2016 Dado Colussi
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>


#ifndef LEVEL1_DCACHE_LINESIZE
#define LEVEL1_DCACHE_LINESIZE 64
#endif


struct gdc_circular_queue_properties
{
	atomic_size_t capacity;
	int sync;
};


typedef struct gdc_circular_queue
{
	
	union
	{
		// Index of the next byte to read in the data buffer.
		// Producer reads, consumer writes.
		atomic_size_t rpos;
		char pad_rpos[LEVEL1_DCACHE_LINESIZE];
		char beginning;
	};
	
	union
	{
		// Index of the next byte to write in the data buffer.
		// Producer writes, consumer reads.
		atomic_size_t wpos;
		char pad_wpos[LEVEL1_DCACHE_LINESIZE];
	};
	
	union
	{
		// Capacity as number of bytes. This is immutable.
		struct gdc_circular_queue_properties properties;
		char pad_capacity[LEVEL1_DCACHE_LINESIZE];
	};
	
	// Optional metadata.
	char metadata;
	
} gdc_circular_queue;



// Returns the number of bytes available for reading. Values range is [0,capacity).
static inline size_t available(gdc_circular_queue *q, size_t rp, size_t wp);

// Returns the number of bytes available for writing. Value range is [0,capacity).
static inline size_t space(gdc_circular_queue *q, size_t rp, size_t wp);

// Moves the write index forward len bytes.
static inline void advance_wpos(gdc_circular_queue *q, size_t len);

// Moves the read index forward len bytes.
static inline void advance_rpos(gdc_circular_queue *q, size_t len);


int
gdc_circular_queue_init(
	gdc_circular_queue *q,
	size_t capacity,
	int sync,
	int (*mdinit)(gdc_circular_queue*, void*),
	void* md_context)
{
	if (mdinit != NULL && mdinit(q, md_context) != 0)
	{
		return -1;
	}
	
	q->properties.sync = sync;
	atomic_store_explicit(&q->properties.capacity, capacity, memory_order_release);
	
	return 0;
}


void*
gdc_circular_queue_metadata(gdc_circular_queue *q)
{
	return &q->metadata;
}


void*
gdc_circular_queue_data(gdc_circular_queue *q)
{
	static long page_size = -1;
	
	if (page_size == -1)
	{
		page_size = sysconf(_SC_PAGESIZE);
	}
	
	char *p = (char*)q + page_size;
	return p;
}


size_t
gdc_circular_queue_capacity(gdc_circular_queue *q)
{
	return atomic_load_explicit(&q->properties.capacity, memory_order_relaxed);
}


int
gdc_circular_queue_empty(gdc_circular_queue *q)
{
	size_t rp = atomic_load_explicit(&q->rpos, memory_order_relaxed);
	size_t wp = atomic_load_explicit(&q->wpos, memory_order_relaxed);
	return wp == rp;
}


size_t
gdc_circular_queue_available(gdc_circular_queue *q)
{
	size_t rp = atomic_load_explicit(&q->rpos, memory_order_relaxed);
	size_t wp = atomic_load_explicit(&q->wpos, memory_order_relaxed);
	size_t n = available(q, rp, wp);
	return n;
}


size_t
gdc_circular_queue_space(gdc_circular_queue *q)
{
	size_t rp = atomic_load_explicit(&q->rpos, memory_order_relaxed);
	size_t wp = atomic_load_explicit(&q->wpos, memory_order_relaxed);
	size_t n = space(q, rp, wp);
	return n;
}


void*
gdc_circular_queue_peek(gdc_circular_queue *q)
{
	size_t rp = atomic_load_explicit(&q->rpos, memory_order_relaxed);
	size_t wp = atomic_load_explicit(&q->wpos, memory_order_relaxed);
	
	if (rp == wp)
	{
		// Queue is empty.
		return NULL;
	}
	
	if (q->properties.sync)
	{
		// Memory fence after relaxed read of wpos.
		atomic_thread_fence(memory_order_acquire);
	}
	
	char *d = gdc_circular_queue_data(q);
	char *p = &d[rp];
	return p;
}


void
gdc_circular_queue_pop(gdc_circular_queue *q, size_t n)
{
	assert(n <= gdc_circular_queue_available(q));
	advance_rpos(q, n);
}


void*
gdc_circular_queue_alloc(gdc_circular_queue *q, size_t len)
{
	assert(len > 0);
	assert(len < gdc_circular_queue_capacity(q));
	
	size_t rp = atomic_load_explicit(&q->rpos, memory_order_relaxed);
	size_t wp = atomic_load_explicit(&q->wpos, memory_order_relaxed);
	
	if (len > space(q, rp, wp))
	{
		return NULL;
	}
	
	char* d = gdc_circular_queue_data(q);
	char* p = &d[wp];
	return p;
}


void
gdc_circular_queue_commit(gdc_circular_queue *q, size_t len)
{
	assert(len > 0);
	assert(len < gdc_circular_queue_capacity(q));
	assert(len <= gdc_circular_queue_space(q));
	advance_wpos(q, len);
}



////////////////////////////////////////////////////////////////
//
// Private functions
//


size_t
available(gdc_circular_queue *q, size_t rp, size_t wp)
{
	size_t capacity = gdc_circular_queue_capacity(q);
	size_t n;
	
	if (wp >= rp)
	{
		// _____xxxxx_____
		//      ^    ^
		//     rp    wp
		//
		// Scenario 2 (empty):
		// _______________
		//      ^
		//    wp==rp
		n = wp - rp;
	}
	else
	{
		// xxxxx_____xxxxx
		//      ^    ^
		//     wp    rp
		n = capacity + wp - rp;
	}
	
	assert(n < capacity);
	
	return n;
}


size_t
space(gdc_circular_queue *q, size_t rp, size_t wp)
{
	size_t capacity = gdc_circular_queue_capacity(q);
	size_t n;
	
	if (wp >= rp)
	{
		// _____xxxxx_____
		//      ^    ^
		//     rp    wp
		//
		// Scenario 2 (empty):
		// _______________
		//      ^
		//    wp==rp
		n = capacity + rp - wp - 1;
	}
	else
	{
		// xxxxx_____xxxxx
		//      ^    ^
		//     wp    rp
		n = rp - wp;
	}
	
	assert(n < capacity);
	
	return n;
}


void
advance_wpos(gdc_circular_queue *q, size_t len)
{
	size_t wp = atomic_load_explicit(&q->wpos, memory_order_relaxed);
	wp = (wp + len) % gdc_circular_queue_capacity(q);
	memory_order mo = q->properties.sync ? memory_order_release : memory_order_relaxed;
	atomic_store_explicit(&q->wpos, wp, mo);
}


void
advance_rpos(gdc_circular_queue *q, size_t len)
{
	size_t capacity = gdc_circular_queue_capacity(q);
	size_t rp = atomic_load_explicit(&q->rpos, memory_order_relaxed);
	rp = (rp + len) % capacity;
	assert(rp <= 2 * capacity);
	atomic_store_explicit(&q->rpos, rp, memory_order_relaxed);
}

