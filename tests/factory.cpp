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


SCENARIO("C/C++ circular queue factory", "[factory]")
{

	typedef gdc::circular_queue_factory<char> F;
	typedef typename F::value_type Q;

	// Remove any stale shared queue.
	F::delete_shared(name);
	
	
	GIVEN("circular_queue_factory to create a shared queue")
	{

		F f(name, 10 * page_size);
		
		
		WHEN("writing to the data area")
		{
			auto& q = f.get();
			auto p = reinterpret_cast<char*>(&q);
			auto data = &p[page_size];
			std::strcpy(data, "blah");
			
			THEN("the data is visible in the 2nd mapped area")
			{
				auto data2 = &data[10 * page_size];
				REQUIRE(std::strncmp(data2, "blah", 5) == 0);
			}
		}

	}


	GIVEN("circular_queue_factory to create a private queue")
	{

		F f(10 * page_size);
		
		
		WHEN("writing to the data area")
		{
			auto& q = f.get();
			auto p = reinterpret_cast<char*>(&q);
			auto data = &p[page_size];
			std::strcpy(data, "blah");
			
			THEN("the data is visible in the 2nd mapped area")
			{
				auto data2 = &data[10 * page_size];
				REQUIRE(std::strncmp(data2, "blah", 5) == 0);
			}
		}

	}

	
	GIVEN("circular_queue_factory to map an existing shared queue")
	{

		WHEN("the shared queue exists")
		{
			gdc::circular_queue_factory<char> f(name, 10 * page_size);
			f.get();

			THEN("can_get() returns true")
			{
				gdc::circular_queue_factory<char> ff(name);
				REQUIRE(ff.can_get());
			}

			THEN("mapping the queue succeeds")
			{
				gdc::circular_queue_factory<char> ff(name);
				REQUIRE_NOTHROW(f.get());
			}
		}


		WHEN("the shared queue doesn't exist")
		{
			THEN("can_get() returns false")
			{
				F f(name);
				REQUIRE_FALSE(f.can_get());
			}

			THEN("mapping the queue throws gdc::circular_queue_error")
			{
				F f(name);
				REQUIRE_THROWS_AS(f.get(), gdc::circular_queue_error);
			}
		}

	}


	GIVEN("circular_queue_factory to create a new shared queue")
	{

		F f(name, 10 * page_size);


		THEN("bool operator returns false before the queue been created")
		{
			REQUIRE_FALSE(f);
		}

		
		THEN("bool operator returns true after the queue been created")
		{
			f.get();
			REQUIRE(f);
		}
		
		
		WHEN("using same name for another factory gives a different queue")
		{
			decltype(f) f2(name);
			decltype(f) fanother(name, 10 * page_size);
			decltype(f) fanother2(name);
			auto& orig = f.get();
			auto& orig2 = f2.get();
			auto& another = fanother.get();
			auto& another2 = fanother2.get();
			orig.push('a');
			another.push('b');

			CHECK_FALSE(orig2.empty());
			CHECK(orig2.front() == 'a');

			CHECK_FALSE(another2.empty());
			CHECK(another2.front() == 'b');
		}


		WHEN("moving the factory")
		{
			auto& b = f.get();
			auto ff(std::move(f));

			THEN("the original factory no longer has the queue")
			{
				REQUIRE_FALSE(f);
			}

			THEN("the new factory has the queue")
			{
				REQUIRE(ff);
			}
		}

	}


	GIVEN("a metadata initializer function")
	{

		bool did_init = false;
		auto init = [&did_init](Q& md __attribute__((unused))) -> int
		{
			did_init = true;
			return 0;
		};


		WHEN("creating a factory for shared circular_queue")
		{

			gdc::circular_queue_factory<char> f(name, 10 * page_size, init);

			THEN("the initialzer function is called")
			{
				REQUIRE_FALSE(did_init);
				f.get();
				REQUIRE(did_init);
			}

		}


		WHEN("creating a factory for private circular_queue")
		{
			F f(10 * page_size, init);

			THEN("the initialzer function is called")
			{
				REQUIRE_FALSE(did_init);
				f.get();
				REQUIRE(did_init);
			}
		}

	}

}

