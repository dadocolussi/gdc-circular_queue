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


#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>


#include "gdc_circular_queue_factory.h"
#include "gdc_circular_queue.h"


static size_t
gdc_circular_queue_footprint(size_t capacity)
{
	long page_size = sysconf(_SC_PAGESIZE);
	if (page_size == -1)
	{
		return 0;
	}
	assert(page_size > 0);
	
	if (capacity == 0)
	{
		return page_size;
	}
	
	// Calculate the smallest multiple of page size that is >= page size + capacity.
	// The first page is for struct gdc_circular_queue. The other pages are for data.
	// Examples:
	// capacity == 0 => footprint = page_size.
	// capacity == 1 => footprint = page_size.
	// capacity == page_size => footprint = page_size.
	// capacity == page_size + 1 => footprint = 2 * page_size.
	long footprint = page_size + (((capacity - 1) / page_size) + 1) * page_size;
	return footprint;
}


// See here how to double mmap a piece of memory:
// https://groups.google.com/d/msg/comp.os.linux.development.system/Prx7ExCzsv4/saKCMIeJHhgJ
int
gdc_circular_queue_create_shared(
	const char* name,
	size_t capacity,
	int (*mdinit)(gdc_circular_queue*, void*),
	void* md_context)
{
	// Unlink any old shared memory object with the same name.
	int status = shm_unlink(name);
	if (status == -1 && errno != ENOENT)
	{
		return -1;
	}
	
	// Create a new shared memory object.
	int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, S_IRWXU);
	if (fd == -1)
	{
		shm_unlink(name);
		return -1;
	}
	
	size_t len = gdc_circular_queue_footprint(capacity) + capacity;
	status = ftruncate(fd, len);
	if (status != 0)
	{
		close(fd);
		shm_unlink(name);
		return -1;
	}
	
	void *p = mmap(
		NULL,
		len,
		PROT_READ | PROT_WRITE,
		MAP_SHARED,
		fd,
		0);
	if (p == MAP_FAILED)
	{
		shm_unlink(name);
		close(fd);
		return -1;
	}
	
	gdc_circular_queue* q = p;
	status = gdc_circular_queue_init(q, capacity, mdinit, md_context);
	
	if (status == -1)
	{
		shm_unlink(name);
		close(fd);
		return status;
	}
	
	if (munmap(p, len) != 0)
	{
		shm_unlink(name);
		close(fd);
		return -1;
	}
	
	if (close(fd) != 0)
	{
		shm_unlink(name);
		return -1;
	}
	
	return 0;
}


int
gdc_circular_queue_delete_shared(const char *name)
{
	return shm_unlink(name);
}


gdc_circular_queue*
gdc_circular_queue_create_private(
	size_t capacity,
	int (*mdinit)(gdc_circular_queue*, void*),
	void* md_context)
{
	static atomic_int seq;
	int unique = atomic_fetch_add_explicit(&seq, 1, memory_order_relaxed);
	pid_t pid = getpid();
	char tmp_name[32];
	sprintf(tmp_name, "/.gdc.%d.%d", pid, unique);
	
	if (gdc_circular_queue_create_shared(tmp_name, capacity, mdinit, md_context) == -1)
	{
		return NULL;
	}
	
	gdc_circular_queue *q = gdc_circular_queue_map_shared(tmp_name);
	gdc_circular_queue_delete_shared(tmp_name);
	return q;
	
}


int
gdc_circular_queue_delete_private(gdc_circular_queue *q)
{
	return gdc_circular_queue_unmap_shared(q);
}


gdc_circular_queue*
gdc_circular_queue_map_shared(const char *name)
{
	// Ignore size. The true size is defined in the mapped memory.
	long page_size = sysconf(_SC_PAGESIZE);
	if (page_size == -1)
	{
		return NULL;
	}
	assert(page_size > 0);
	
	int fd = shm_open(name, O_RDWR, S_IRWXU);
	if (fd == -1)
	{
		return NULL;
	}
	
	struct stat st;
	int status = fstat(fd, &st);
	if (status != 0 || st.st_size <= page_size)
	{
		// Not fully initialized yet.
		close(fd);
		return NULL;
	}
	
	size_t init_size = gdc_circular_queue_footprint(0);
	void *p = mmap(
		NULL,
		init_size,
		PROT_READ | PROT_WRITE,
		MAP_SHARED,
		fd,
		0);
	if (p == MAP_FAILED)
	{
		close(fd);
		return NULL;
	}
	
	gdc_circular_queue *q = p;
	atomic_thread_fence(memory_order_acquire);
	size_t capacity = gdc_circular_queue_capacity(q);
	
	if (capacity == 0)
	{
		munmap(p, init_size);
		close(fd);
		errno = EAGAIN;
		return NULL;
	}
	
	size_t footprint = gdc_circular_queue_footprint(capacity);
	
	if (munmap(p, init_size) != 0)
	{
		close(fd);
		return NULL;
	}
	
	p = mmap(
		NULL,
		footprint + capacity,
		PROT_READ | PROT_WRITE,
		MAP_SHARED,
		fd,
		0);
	if (p == MAP_FAILED)
	{
		close(fd);
		return NULL;
	}
	
	q = p;
	void *pp = (char*)p + footprint;
	void *p2 = mmap(
		pp,
		capacity,
		PROT_READ | PROT_WRITE, 
		MAP_SHARED | MAP_FIXED,
		fd,
		page_size);
	if (p2 == MAP_FAILED)
	{
		munmap(p, footprint + capacity);
		close(fd);
		return NULL;
	}
	
	if (close(fd) != 0)
	{
		munmap(p, footprint + capacity);
		close(fd);
		return NULL;
	}
	
	return q;
}


int
gdc_circular_queue_unmap_shared(gdc_circular_queue *q)
{
	if (q != NULL)
	{
		size_t capacity = gdc_circular_queue_capacity(q);
		size_t footprint = gdc_circular_queue_footprint(capacity);
		return munmap(q, footprint + capacity);
	}
	
	return 0;
}

