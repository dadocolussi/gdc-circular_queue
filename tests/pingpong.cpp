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
#include <cstdlib>
#include <iostream>

#include "catch.hpp"

#if USE_C_API
// C++ classes use C implementation.
#include "gdc_circular_queue_factory.h"
#else
// C++ classes implement the queue.
#include "gdc_circular_queue_factory.hpp"
#endif


typedef gdc::circular_queue_factory<std::size_t> F;
typedef typename F::value_type Q;


void
run()
{
	long page_size = ::sysconf(_SC_PAGESIZE);
	assert(page_size > 0);
	std::string ping_name("/gdcq.ping");
	std::string pong_name("/gdcq.pong");

#if USE_C_API
	F rqf(ping_name, 10 * page_size);
	F wqf(pong_name);
	bool seed = false;
#else
	F wqf(ping_name);
	F rqf(pong_name, 10 * page_size);
	bool seed = true;
#endif

	std::cout << "Trying to resolve write queue" << std::endl;
	Q& rq = rqf.get();
	for (int i = 0; i < 10; ++i)
	{
		if (wqf.can_get())
		{
			break;
		}

		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	
	if (!wqf.can_get())
	{
		throw std::runtime_error("Failed to resolve write queue");
	}
	
	Q& wq = wqf.get();
	std::cout << "Did resolve write queue" << std::endl;

	std::size_t seq = 0;
	std::size_t n = 1000000;

	if (seed)
	{
		wq.push(&seq, sizeof seq);
		++seq;
	}

	while (seq < n)
	{
		auto i = rq.peek();

		if (i == nullptr)
		{
			continue;
		}

		if (seq == 0)
		{
			// Initialize sequence.
			seq = *i;
		}

		assert(*i == seq);
		rq.pop(sizeof seq);
		assert(rq.empty());

		if (++seq < n)
		{
			wq.push(&seq, sizeof seq);
			++seq;
		}
	}

	std::cout << "Did send and receive " << n << " messages" << std::endl;
}

int
main()
{
	try
	{
		run();
	}
	catch (const std::exception& ex)
	{
		std::cerr << ex.what() << std::endl;
		exit(EXIT_FAILURE);
	}
}

