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


#ifndef __gdc__circular_queue_factory__
#define __gdc__circular_queue_factory__

#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include <type_traits>
#include <stdexcept>
#include <memory>
#include <string>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cassert>
#include <cstring>

#include "gdc_circular_queue.hpp"


namespace gdc
{
	
	class circular_queue_error : public virtual std::runtime_error
	{
	public:
		circular_queue_error(const std::string& what) : std::runtime_error(what) {}
		circular_queue_error(const char* what) : std::runtime_error(what) {}
	};
	
	
	template<typename T, typename Q = circular_queue<T>>
	class circular_queue_factory
	{
	public:
		typedef Q value_type;

	private:
		
		typedef value_type* pointer;
		typedef typename Q::size_type size_type;
		typedef std::unique_ptr<Q, void(*)(Q*)> unique_ptr;
		typedef std::function<int(Q&)> mdinit_type;

		
		std::string _name;
		size_type _capacity;
		bool _sync;
		mdinit_type _metadata_initializer;
		unique_ptr _q;
		
		
		static void null_queue_destroyer(Q* q __attribute__((unused)))
		{
			// no-op
		}
		
		
		static size_type footprint(size_type capacity)
		{
			static long page_size = ::sysconf(_SC_PAGESIZE);
			if (page_size == -1)
			{
				return 0;
			}
			assert(page_size > 0);
			
			if (capacity == 0)
			{
				return page_size;
			}
			
			// Calculate the smallest multiple of page size such that
			// size >= page size + capacity.
			// The first page is for control data.
			// The other pages are for data.
			//
			// Examples:
			// capacity == 0 => footprint = page_size.
			// capacity == 1 => footprint = page_size.
			// capacity == page_size => footprint = page_size.
			// capacity == page_size + 1 => footprint = 2 * page_size.
			long footprint = page_size + (((capacity - 1) / page_size) + 1) * page_size;
			return footprint;
		}


	public:

		static void create_shared(
			const std::string& name,
			size_type capacity,
			bool sync,
			mdinit_type metadata_initializer)
		{
			// Unlink any old shared memory object with the same name.
			int status = ::shm_unlink(name.c_str());
			if (status == -1 && errno != ENOENT)
			{
				std::string what("shm_unlink: ");
				what.append(::strerror(errno));
				throw circular_queue_error(what);
			}
			
			// Create a new shared memory object.
			int fd = ::shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRWXU);
			if (fd == -1)
			{
				std::string what("shm_open: ");
				what.append(::strerror(errno));
				::shm_unlink(name.c_str());
				throw circular_queue_error(what);
			}
			
			size_t len = footprint(capacity) + capacity;
			status = ::ftruncate(fd, len);
			if (status != 0)
			{
				std::string what("ftruncate: ");
				what.append(::strerror(errno));
				::close(fd);
				::shm_unlink(name.c_str());
				throw circular_queue_error(what);
			}
			
			void* p = ::mmap(
				NULL,
				len,
				PROT_READ | PROT_WRITE,
				MAP_SHARED,
				fd,
				0);
			if (p == MAP_FAILED)
			{
				std::string what("mmap: ");
				what.append(::strerror(errno));
				::close(fd);
				::shm_unlink(name.c_str());
				throw circular_queue_error(what);
			}
			
			try
			{
				auto q = new (p) Q();
				metadata_initializer(*q);
				auto qq = reinterpret_cast<circular_queue_control_block*>(p);
				qq->properties.sync = sync;
				qq->properties.capacity.store(capacity, std::memory_order_release);
			}
			catch (const std::exception& ex)
			{
				::close(fd);
				::shm_unlink(name.c_str());
				throw;
			}
			
			if (::close(fd) != 0)
			{
				std::string what("close: ");
				what.append(::strerror(errno));
				::shm_unlink(name.c_str());
				throw circular_queue_error(what);
			}
		}
		
		
		static void delete_shared(const std::string& name)
		{
			int status = ::shm_unlink(name.c_str());
			if (status == -1 && errno != ENOENT)
			{
				std::string what("shm_unlink: ");
				what.append(::strerror(errno));
				throw circular_queue_error(what);
			}
		}
		
		
		static Q* map_shared(const std::string& name)
		{
			// Ignore size. The true size is defined in the mapped memory.
			static long page_size = ::sysconf(_SC_PAGESIZE);
			if (page_size == -1)
			{
				std::string what("sysconf: ");
				what.append(::strerror(errno));
				throw circular_queue_error(what);
			}
			assert(page_size > 0);
			
			int fd = ::shm_open(name.c_str(), O_RDWR, S_IRWXU);
			if (fd == -1)
			{
				std::string what("shm_open: ");
				what.append(::strerror(errno));
				throw circular_queue_error(what);
			}
			
			size_type init_size = footprint(0);
			auto p = ::mmap(
				NULL,
				init_size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED,
				fd,
				0);
			if (p == MAP_FAILED)
			{
				std::string what("mmap: ");
				what.append(::strerror(errno));
				::close(fd);
				throw circular_queue_error(what);
			}
			
			Q* q = new (p) Q();
			size_type capacity = q->capacity();
			
			if (capacity == 0)
			{
				// Not fully initialized yet.
				::munmap(p, init_size);
				::close(fd);
				throw circular_queue_error("Not fully initialized yet.");
			}
			
			if (::munmap(p, init_size) != 0)
			{
				std::string what("munmap: ");
				what.append(::strerror(errno));
				::close(fd);
				throw circular_queue_error(what);
			}
			
			size_type fp = footprint(capacity);
			p = ::mmap(
				NULL,
				fp + capacity,
				PROT_READ | PROT_WRITE,
				MAP_SHARED,
				fd,
				0);
			if (p == MAP_FAILED)
			{
				std::string what("mmap: ");
				what.append(::strerror(errno));
				::close(fd);
				throw circular_queue_error(what);
			}
			
			q = new (p) Q();
			void* pp = reinterpret_cast<char*>(p) + fp;
			void* p2 = ::mmap(
				pp,
				capacity,
				PROT_READ | PROT_WRITE, 
				MAP_SHARED | MAP_FIXED,
				fd,
				page_size);
			if (p2 == MAP_FAILED)
			{
				std::string what("mmap: ");
				what.append(::strerror(errno));
				::munmap(p, fp + capacity);
				::close(fd);
				throw circular_queue_error(what);
			}
			
			if (::close(fd) != 0)
			{
				std::string what("close: ");
				what.append(::strerror(errno));
				::munmap(p, fp + capacity);
				::close(fd);
				throw circular_queue_error(what);
			}
			
			return q;
		}
		
		
		static void unmap_shared(Q* q)
		{
			if (q == nullptr)
			{
				return;
			}

			size_type capacity = q->capacity();
			size_type fp = footprint(capacity);

			if (::munmap(q, fp + capacity) == -1)
			{
				std::string what("munmap: ");
				what.append(::strerror(errno));
				throw circular_queue_error(what);
			}
		}
		
		
		static Q* create_private(
			size_type capacity,
			bool sync,
			mdinit_type metadata_initializer)
		{
			static std::atomic<int> seq(0);
			int unique = seq.fetch_add(1, std::memory_order_relaxed);
			pid_t pid = ::getpid();
			char tmp_name[32];
			std::sprintf(tmp_name, "/.gdcq.%d.%d", pid, unique);
			create_shared(tmp_name, capacity, sync, metadata_initializer);
			Q* q = nullptr;

			try
			{
				q = map_shared(tmp_name);
			}
			catch (const circular_queue_error& ex)
			{
				delete_shared(tmp_name);
				throw;
			}

			try
			{
				delete_shared(tmp_name);
			}
			catch (const circular_queue_error& ex)
			{
				unmap_shared(q);
				delete_shared(tmp_name);
				throw;
			}

			return q;
		}
		
		
		static void delete_private(Q* q)
		{
			unmap_shared(q);
		}
		
		
	private:

		void create()
		{
			static_assert(
				std::is_trivially_copyable<Q>::value,
				"Q in circular_queue_factory<T,Q> must be trivially copyable");

			if (_q)
			{
				// Already created.
				return;
			}
			
			if (!_name.empty())
			{
				// We're dealing with a shared memory queue.
				
				if (_capacity > 0)
				{
					// We set the capacity, hence we create the queue.
					create_shared(_name, _capacity, _sync, _metadata_initializer);
				}
				
				_q = unique_ptr(map_shared(_name), unmap_shared);
			}
			else if (_capacity > 0)
			{
				_q = unique_ptr(
					create_private(
						_capacity,
						_sync,
						_metadata_initializer),
					delete_private);
			}
			
			assert(_q);
		}
		
		
	public:
		
		// For creating a new shared memory queue.
		circular_queue_factory(
			const std::string& name,
			size_type capacity,
			bool sync = true,
			mdinit_type metadata_initializer = [](Q&) -> int { return 0; }) :
			_name(name),
			_capacity(capacity),
			_sync(sync),
			_metadata_initializer(metadata_initializer),
			_q(nullptr, null_queue_destroyer)
		{
			assert(!name.empty());
			assert(capacity >= 0);
		}
		
		
		// For mapping an existing shared memory queue.
		circular_queue_factory(const std::string& name) :
			_name(name),
			_capacity(0),
		    _sync(false),
			_q(nullptr, null_queue_destroyer)
		{
			assert(!name.empty());
		}
		
		
		// For creating a new in-process queue.
		circular_queue_factory(
			size_type capacity,
			bool sync = true,
			mdinit_type metadata_initializer = [](Q&) -> int { return 0; }) :
			_capacity(capacity),
			_sync(sync),
			_metadata_initializer(metadata_initializer),
			_q(nullptr, null_queue_destroyer)
		{
			assert(capacity >= 0);
		}
		
		
		circular_queue_factory(circular_queue_factory&& f) :
			_name(std::move(f._name)),
			_capacity(f._capacity),
			_metadata_initializer(std::move(f._metadata_initializer)),
			_q(std::move(f._q))
		{
			f._capacity = 0;
		}
		
		
		circular_queue_factory(const circular_queue_factory&) = delete;
		circular_queue_factory& operator=(const circular_queue_factory&) = delete;
		
		
		~circular_queue_factory()
		{
			if (!_name.empty() && _capacity > 0)
			{
				delete_shared(_name);
			}
			
			// std::unique_ptr handles unmapping the queue.
		}
		
		
		bool can_get() const
		{
			if (_q || _name.empty())
			{
				return true;
			}

			int fd = ::shm_open(_name.c_str(), O_RDWR, S_IRWXU);
			if (fd == -1)
			{
				return false;
			}

			::close(fd);
			return true;
		}


		Q& get()
		{
			create();
			return *(_q.get());
		}
		
		
		operator bool() const
		{
			return _q.get() != nullptr;
		}
		
	};
	
}


#endif

