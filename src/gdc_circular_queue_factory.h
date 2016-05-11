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


#ifndef __gdc_circular_queue_factory__
#define __gdc_circular_queue_factory__


#include <stddef.h>

#include "gdc_circular_queue.h"


#ifdef __cplusplus
extern "C" {
#endif
	
	
int gdc_circular_queue_create_shared(
	const char* name,
	size_t capacity,
	int sync,
	int (*mdinit)(gdc_circular_queue*, void*),
	void* md_context);
int gdc_circular_queue_delete_shared(const char* name);

gdc_circular_queue* gdc_circular_queue_create_private(
	size_t capacity,
	int sync,
	int (*mdinit)(gdc_circular_queue*, void*),
	void* md_context);
int gdc_circular_queue_delete_private(gdc_circular_queue *q);

gdc_circular_queue* gdc_circular_queue_map_shared(const char* name);
int gdc_circular_queue_unmap_shared(gdc_circular_queue *q);
	
	
#ifdef __cplusplus
}


#include <type_traits>
#include <stdexcept>
#include <memory>
#include <string>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <sys/mman.h>
#include <fcntl.h>


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
		
		
		static int default_metadata_init(gdc_circular_queue* q, void* context)
		{
			if (context != NULL)
			{
				auto init = reinterpret_cast<mdinit_type*>(context);
				auto qq = reinterpret_cast<Q*>(q);
				return (*init)(*qq);
			}
			
			return 0;
		}
		
		
		static void null_queue_destroyer(Q* q __attribute__((unused)))
		{
			// no-op
		}

	public:
		
		
		static void create_shared(
			const std::string& name,
			size_type capacity,
			bool sync,
			mdinit_type metadata_initializer)
		{
			void* mdinit_context = &metadata_initializer;
			int status = ::gdc_circular_queue_create_shared(
				name.c_str(),
				capacity,
				sync,
				default_metadata_init,
				mdinit_context);

			if (status != 0)
			{
				std::string what("Failed to create shared memory queue: ");
				what.append(::strerror(errno));
				throw circular_queue_error(what);
			}
		}
		
		
		static void delete_shared(const std::string& name)
		{
			int status = ::gdc_circular_queue_delete_shared(name.c_str());
			
			if (status != 0 && errno != ENOENT)
			{
				std::string what("Failed to delete shared memory queue: ");
				what.append(::strerror(errno));
				throw circular_queue_error(what);
			}
		}
		
		
		static Q* map_shared(const std::string& name)
		{
			gdc_circular_queue* q = ::gdc_circular_queue_map_shared(name.c_str());
			
			if (q == NULL)
			{
				std::string what("Failed to map shared memory queue: ");
				what.append(::strerror(errno));
				throw circular_queue_error(what);
			}
			
			return reinterpret_cast<Q*>(q);
		}
		
		
		static void unmap_shared(Q* q)
		{
			auto qq = reinterpret_cast<gdc_circular_queue*>(q);
			int status = ::gdc_circular_queue_unmap_shared(qq);
			
			if (status != 0)
			{
				std::string what("Failed to unmap shared memory queue: ");
				what.append(::strerror(errno));
				throw circular_queue_error(what);
			}
		}
		
		
		static Q* create_private(
			size_type capacity,
			bool sync,
			mdinit_type metadata_initializer)
		{
			void* mdinit_context = &metadata_initializer;
			gdc_circular_queue* q = ::gdc_circular_queue_create_private(
				capacity,
				sync,
				default_metadata_init,
				mdinit_context);

			if (q == NULL)
			{
				std::string what("Failed to create private queue: ");
				what.append(::strerror(errno));
				throw circular_queue_error(what);
			}
			
			return reinterpret_cast<Q*>(q);
		}
		
		
		static void delete_private(Q* q)
		{
			auto qq = reinterpret_cast<gdc_circular_queue*>(q);
			int status = ::gdc_circular_queue_delete_private(qq);
			
			if (status != 0)
			{
				std::string what("Failed to delete private queue: ");
				what.append(::strerror(errno));
				throw circular_queue_error(what);
			}
		}
		
		
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
			_sync(f._sync),
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
				::gdc_circular_queue_delete_shared(_name.c_str());
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


#endif // __cplusplus


#endif
