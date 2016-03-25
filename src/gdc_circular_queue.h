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


#ifndef __gdc_circular_queue__
#define __gdc_circular_queue__


#ifdef GDC_EXPORT_API
#define GDC_API __attribute__ ((visibility ("default")))
#else
#define GDC_API
#endif


#include <stddef.h>


#ifndef LEVEL1_DCACHE_LINESIZE
#define LEVEL1_DCACHE_LINESIZE 64
#endif


#ifdef __cplusplus
extern "C" {
#endif
	
	
typedef struct gdc_circular_queue gdc_circular_queue;


int gdc_circular_queue_init(
	gdc_circular_queue *q,
	size_t capacity,
	int (*mdinit)(gdc_circular_queue*, void*),
	void* md_context);
void* gdc_circular_queue_metadata(gdc_circular_queue *q);
size_t gdc_circular_queue_capacity(gdc_circular_queue *q);
int gdc_circular_queue_empty(gdc_circular_queue *q);
size_t gdc_circular_queue_available(gdc_circular_queue *q);
size_t gdc_circular_queue_space(gdc_circular_queue *q);
void* gdc_circular_queue_peek(gdc_circular_queue *q);
void gdc_circular_queue_pop(gdc_circular_queue *q, size_t n);
void* gdc_circular_queue_alloc(gdc_circular_queue *q, size_t len);
void gdc_circular_queue_commit(gdc_circular_queue *q, size_t len);


#ifdef __cplusplus
}


#include <type_traits>
#include <stdexcept>
#include <memory>
#include <string>
#include <atomic>
#include <cstddef>
#include <cassert>


namespace gdc
{
	
	template<typename T>
	class circular_queue
	{
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
			auto q = reinterpret_cast<const gdc_circular_queue*>(this);
			auto qq = const_cast<gdc_circular_queue*>(q);
			return ::gdc_circular_queue_capacity(qq);
		}
		
		
		void* metadata() noexcept
		{
			auto q = reinterpret_cast<gdc_circular_queue*>(this);
			return ::gdc_circular_queue_metadata(q);
		}
		
		
		bool empty() const noexcept
		{
			auto q = reinterpret_cast<const gdc_circular_queue*>(this);
			auto qq = const_cast<gdc_circular_queue*>(q);
			return ::gdc_circular_queue_empty(qq);
		}
		
		
		size_type available() const noexcept
		{
			auto q = reinterpret_cast<const gdc_circular_queue*>(this);
			auto qq = const_cast<gdc_circular_queue*>(q);
			return ::gdc_circular_queue_available(qq);
		}
		
		
		size_type space() const noexcept
		{
			auto q = reinterpret_cast<const gdc_circular_queue*>(this);
			auto qq = const_cast<gdc_circular_queue*>(q);
			return ::gdc_circular_queue_space(qq);
		}
		
		
		const_pointer peek() const
		{
			auto q = reinterpret_cast<const gdc_circular_queue*>(this);
			auto qq = const_cast<gdc_circular_queue*>(q);
			auto p = ::gdc_circular_queue_peek(qq);
			return reinterpret_cast<const_pointer>(p);
		}
		
		
		void pop(size_type len) noexcept
		{
			auto q = reinterpret_cast<gdc_circular_queue*>(this);
			return ::gdc_circular_queue_pop(q, len);
		}
		
		
		pointer alloc(size_type len) const
		{
			auto q = reinterpret_cast<const gdc_circular_queue*>(this);
			auto qq = const_cast<gdc_circular_queue*>(q);
			auto p = ::gdc_circular_queue_alloc(qq, len);
			return reinterpret_cast<pointer>(p);
		}
		
		
		void commit(size_type len)
		{
			auto q = reinterpret_cast<const gdc_circular_queue*>(this);
			auto qq = const_cast<gdc_circular_queue*>(q);
			::gdc_circular_queue_commit(qq, len);
		}
		
		
		bool push(const_pointer data, size_type len)
		{
			auto p = alloc(len);
			
			if (p == nullptr)
			{
				return false;
			}
			
			std::memmove(p, data, len);
			commit(len);
			return true;
		}
		
		
		bool push(const_reference data)
		{
			return push(&data, sizeof (T));
		}
		
		
		const_reference front() const
		{
			auto q = reinterpret_cast<const gdc_circular_queue*>(this);
			auto qq = const_cast<gdc_circular_queue*>(q);
			auto p = ::gdc_circular_queue_peek(qq);
			auto pp = reinterpret_cast<const_pointer>(p);
			return *pp;
		}
		
	};
	
}


#endif // __cplusplus
#endif // __gdc_circular_queue__
