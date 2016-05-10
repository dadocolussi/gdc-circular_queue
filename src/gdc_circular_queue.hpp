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


#ifndef __gdc__circular_queue__
#define __gdc__circular_queue__


#include <type_traits>
#include <stdexcept>
#include <memory>
#include <string>
#include <atomic>
#include <cstddef>
#include <cassert>
#include <unistd.h>


#ifndef LEVEL1_DCACHE_LINESIZE
#define LEVEL1_DCACHE_LINESIZE 64
#endif


namespace gdc
{

	struct circular_queue_control_block
	{
		
		union
		{
			// Index of the next byte to read in the data buffer.
			// Producer reads, consumer writes.
			std::atomic<size_t> rpos;
			char pad_rpos[LEVEL1_DCACHE_LINESIZE];
			char beginning;
		};
		
		union
		{
			// Index of the next byte to write in the data buffer.
			// Producer writes, consumer reads.
			std::atomic<size_t> wpos;
			char pad_wpos[LEVEL1_DCACHE_LINESIZE];
		};
		
		union
		{
			// Capacity as number of bytes. This is immutable.
			std::atomic<size_t> capacity;
			char pad_capacity[LEVEL1_DCACHE_LINESIZE];
		};
		
		// Optional metadata.
		char metadata;
		
	};

	
	template<typename T>
	class circular_queue
	{
	private:
		
		circular_queue_control_block _q;


	public:
		
		typedef T value_type;
		typedef T* pointer;
		typedef const T* const_pointer;
		typedef T& reference;
		typedef const T& const_reference;
		typedef std::size_t size_type;
		typedef std::ptrdiff_t difference_type;
		
		
		circular_queue()
		{
			static_assert(
				std::is_trivially_copyable<T>::value,
				"T in circular_queue<T> must be trivially copyable");
		}


		~circular_queue() = default;
		circular_queue& operator=(const circular_queue&) = delete;
		circular_queue& operator=(circular_queue&&) = delete;
		circular_queue(circular_queue&&) = delete;
		circular_queue(const circular_queue&) = delete;
		
		
		size_type capacity() const noexcept
		{
			return _q.capacity.load(std::memory_order_relaxed);
		}
		
		
		void* metadata() noexcept
		{
			return &_q.metadata;
		}


		const char* data() const noexcept
		{
			static long page_size = ::sysconf(_SC_PAGESIZE);
			auto p = &_q.beginning + page_size;
			return p;
		}
		
		
		bool empty() const noexcept
		{
			auto rp = _q.rpos.load(std::memory_order_relaxed);
			auto wp = _q.wpos.load(std::memory_order_relaxed);
			return wp == rp;
		}
		
		
		size_type available() const noexcept
		{
			auto rp = _q.rpos.load(std::memory_order_relaxed);
			auto wp = _q.wpos.load(std::memory_order_relaxed);
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
				n = capacity() + wp - rp;
			}
			
			assert(n < capacity());
			
			return n;
		}
		
		
		size_type space() const noexcept
		{
			auto rp = _q.rpos.load(std::memory_order_relaxed);
			auto wp = _q.wpos.load(std::memory_order_relaxed);
			auto c = capacity();
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
				n = c + rp - wp - 1;
			}
			else
			{
				// xxxxx_____xxxxx
				//      ^    ^
				//     wp    rp
				n = rp - wp;
			}
			
			assert(n < c);
			
			return n;
		}


		const_pointer peek() const noexcept
		{
			auto rp = _q.rpos.load(std::memory_order_relaxed);
			auto wp = _q.wpos.load(std::memory_order_relaxed);
			
			if (rp == wp)
			{
				// Queue is empty.
				return nullptr;
			}
			
			// Memory fence after relaxed read.
			std::atomic_thread_fence(std::memory_order_acquire);

			auto d = data();
			auto p = &d[rp];
			return reinterpret_cast<const_pointer>(p);
		}


		void pop(size_type nbytes) noexcept
		{
			auto rp = _q.rpos.load(std::memory_order_relaxed);
			rp = (rp + nbytes) % capacity();
			_q.rpos.store(rp, std::memory_order_relaxed);
		}
		
		
		pointer alloc(size_type nbytes) const noexcept
		{
			assert(nbytes > 0);
			assert(nbytes < capacity());
			
			if (nbytes > space())
			{
				return nullptr;
			}
			
			auto wp = _q.wpos.load(std::memory_order_relaxed);
			auto d = data();
			auto p = &d[wp];
			auto pp = const_cast<char*>(p);
			return reinterpret_cast<pointer>(pp);
		}
		
		
		void commit(size_type nbytes) noexcept
		{
			assert(nbytes > 0);
			assert(nbytes < capacity());
			auto wp = _q.wpos.load(std::memory_order_relaxed);
			wp = (wp + nbytes) % capacity();
			_q.wpos.store(wp, std::memory_order_release);
		}
		
		
		bool push(const_pointer data, size_type nbytes) noexcept
		{
			auto p = alloc(nbytes);
			
			if (p == nullptr)
			{
				return false;
			}
			
			std::memmove(p, data, nbytes);
			commit(nbytes);
			return true;
		}
		
		
		bool push(const_reference data) noexcept
		{
			return push(&data, sizeof data);
		}
		
		
		const_reference front() const noexcept
		{
			auto p = peek();
			return *p;
		}

	};

}


#endif
