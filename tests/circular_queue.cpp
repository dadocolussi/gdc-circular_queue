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


#include <stdexcept>
#include <string>
#include <cstring>
#include <thread>
#include <future>
#include <unistd.h>

#include "catch.hpp"

#if USE_C_API
#include "gdc_circular_queue_factory.h"
#else
#include "gdc_circular_queue_factory.hpp"
#endif


namespace
{
	std::string name("/gdcq.unit_tests");
	long page_size = ::sysconf(_SC_PAGESIZE);
}


SCENARIO("circular queue in single thread", "[queue]")
{

	typedef gdc::circular_queue_factory<char> F;
	typedef typename F::value_type Q;


	// Remove any stale shared queue.
	F::delete_shared(name);
	

	GIVEN("a shared queue")
	{

		F producer(name, 10 * page_size);
		F consumer(name);


		WHEN("pushing data to the buffer")
		{
			auto& pq = producer.get();
			std::string hello("Hello World!");
			pq.push(hello.c_str(), hello.length());

			THEN("the pushed data is available for reading")
			{
				auto& cq = consumer.get();
				REQUIRE(cq.available() == hello.length());
				REQUIRE(std::string(cq.peek(), cq.available()) == hello);
			}
		}


		WHEN("push() + peek() + pop() 100000 times")
		{
			auto& pq = producer.get();
			auto& cq = consumer.get();
			std::string hello("Hello World!");

			for (auto i = 0; i < 100000; ++i)
			{
				CAPTURE(i);
				pq.push(hello.c_str(), hello.length());
				cq.pop(hello.length());
			}

			THEN("next data pushed is available for reading")
			{
				std::string bye("Bye!");
				pq.push(bye.c_str(), bye.length());
				REQUIRE(cq.available() == bye.length());
				REQUIRE(std::string(cq.peek(), cq.available()) == bye);
			}
		}

	}


	GIVEN("an empty private queue")
	{

		typedef typename Q::size_type size_type;
		size_type capacity = 10 * page_size;
		F f(capacity, [](Q& q) -> int
		{
			auto md = q.metadata();
			std::memcpy(md, "Hello World!", 12);
			return 0;
		});
		auto& q = f.get();


		WHEN("the queue has been initialized")
		{
			THEN("capacity is the configured capacity")
			{
				REQUIRE(q.capacity() == capacity);
			}


			THEN("metadata() returns the initialized metadata")
			{
				auto md = reinterpret_cast<char*>(q.metadata());
				REQUIRE(std::string(md, 12) == std::string("Hello World!"));
			}


			THEN("available() returns 0")
			{
				REQUIRE(q.available() == 0);
			}


			THEN("space() returns capacity - 1")
			{
				REQUIRE(q.space() == q.capacity() - 1);
			}


			THEN("peek() returns nullptr")
			{
				REQUIRE(q.peek() == nullptr);
			}


			THEN("alloc() returns pointer")
			{
				REQUIRE(q.alloc(32) != nullptr);
			}


			THEN("alloc(space()) returns pointer")
			{
				REQUIRE(q.alloc(q.space()) != nullptr);
			}


			THEN("alloc() larger than space returns nullptr")
			{
				char dummy[256];
				q.push(dummy, sizeof dummy);
				REQUIRE(q.alloc(q.space() + 1) == nullptr);
			}

			THEN("push() returns true")
			{
				std::string hello("Hello World!");
				q.push(hello.c_str(), hello.length());
			}
		}


		WHEN("alloc() + commit() to an empty queue")
		{
			std::string hello("Hello World!");
			size_type len = hello.length();
			auto buf = q.alloc(len);
			REQUIRE(buf != nullptr);
			std::memmove(buf, hello.c_str(), len);
			q.commit(len);


			THEN("available() returns data length")
			{
				REQUIRE(q.available() == len);
			}


			THEN("space() returns capacity - 1 - len")
			{
				REQUIRE(q.space() == capacity - 1 - len);
			}


			THEN("data in the queue is the pushed data")
			{
				REQUIRE(std::string(q.peek(), q.available()) == hello);
			}


			THEN("alloc() returns the next payload address")
			{
				REQUIRE(q.alloc(32) > q.peek());
			}
		}


		WHEN("push() puts a copy of the data in the queue")
		{
			std::string hello("Hello World!");
			size_type len = hello.length();
			q.push(hello.c_str(), len);


			THEN("available() returns data length")
			{
				REQUIRE(q.available() == len);
			}


			THEN("space() returns capacity - 1 - len")
			{
				REQUIRE(q.space() == capacity - 1 - len);
			}


			THEN("data in the queue is the pushed data")
			{
				REQUIRE(std::string(q.peek(), q.available()) == hello);
			}
		}


		WHEN("push() twice on an empty queue")
		{
			std::string eng("Hello World!");
			auto engp = q.alloc(eng.length());
			REQUIRE(engp != 0);
			std::memcpy(engp, eng.c_str(), eng.length());
			q.push(engp, eng.length());

			std::string ita("Ciao mondo!");
			auto itap = q.alloc(ita.length());
			REQUIRE(itap != 0);
			std::memcpy(itap, ita.c_str(), ita.length());
			q.push(itap, ita.length());


			THEN("available() returns cumulative data length")
			{
				CHECK(q.available() == eng.length() + ita.length());
			}


			THEN("space() returns capacity - 1 - cumulative data length")
			{
				CHECK(q.space() == capacity - 1 - eng.length() - ita.length());
			}


			THEN("data contains cumulative data")
			{
				std::string s(q.peek(), q.available());
				CHECK(s == eng + ita);
			}
		}


		WHEN("queue is full")
		{
			std::string hello("Hello World!");
			size_type len = hello.length();
			size_type n = (capacity - 1) / len;

			for (size_type i = 0; i < n; ++i)
			{
				q.push(hello.c_str(), len);
			}

			THEN("alloc() returns nullptr")
			{
				CHECK(q.alloc(len) == nullptr);
			}

			THEN("push() returns false")
			{
				CHECK_FALSE(q.push(hello.c_str(), len));
			}
		}


		WHEN("push() + peek() + pop() 100000 times")
		{
			std::string hello("Hello World!");

			for (auto i = 0; i < 100000; ++i)
			{
				CAPTURE(i);
				q.push(hello.c_str(), hello.length());
				q.pop(hello.length());
			}

			THEN("next data pushed is available for reading")
			{
				std::string bye("Bye!");
				q.push(bye.c_str(), bye.length());
				REQUIRE(q.available() == bye.length());
				REQUIRE(std::string(q.peek(), q.available()) == bye);
			}
		}

	}

}


SCENARIO("circular queue in multiple threads", "[pingpong]")
{
	typedef gdc::circular_queue_factory<std::size_t> F;
	typedef typename F::value_type Q;

	
	auto echo = [](Q* rq, Q* wq, std::size_t n)
	{
		std::size_t seq = 0;

		while (seq < n)
		{
			auto i = rq->peek();

			if (i == nullptr)
			{
				continue;
			}

			if (seq == 0)
			{
				// Initialize sequence.
				seq = *i;
			}

			if (*i != seq)
			{
				REQUIRE(*i == seq);
			}

			rq->pop(sizeof seq);

			if (!rq->empty())
			{
				REQUIRE(rq->empty());
			}

			if (++seq < n)
			{
				wq->push(&seq, sizeof seq);
				++seq;
			}
		}
	};


	GIVEN("two private queues")
	{
		F pingf(10 * page_size);
		F pongf(10 * page_size);
		Q& pingq = pingf.get();
		Q& pongq = pongf.get();
		std::size_t count = 200000;

		const std::size_t seed = 0;
		pingq.push(&seed, sizeof seed);
		auto f1 = std::async(std::launch::async, echo, &pingq, &pongq, count);
		auto f2 = std::async(std::launch::async, echo, &pongq, &pingq, count);
		CHECK_NOTHROW(f1.wait());
		CHECK_NOTHROW(f2.wait());
	}

}

