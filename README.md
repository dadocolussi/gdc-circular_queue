**gdc-circular_queue** is a wait-free multi-threaded circular queue
implementation in C and C++. The source code comes with separate C and
C++ implementations, as well as a C++ wrapper for the C implementation
(if one wishes to use a common C implementation from C++).

The memory synchronization is based on the atomic types introduced in
C11 and C++11. **gdc-circular_queue** uses POSIX shared memory for the
queue data. This makes it possible to use **gdc-circular-queue** for
multi-process applications on some CPU architectures (check your compiler
and CPU architecture).

**gdc-circular_queue** has been tested on Linux using GCC 5.0 and on
Mac OS X using clang 7.0.


```c++
// Include the .hpp file to use the C++ implementation
#include "gdc_circular_queue_factory.hpp"

// To create a new queue
gdc::circular_queue_factory<char> factory(1000 * 1024);
gdc::circular_queue<char>& queue = factory.get();

// To put data in the queue
std::string s("Hello World!");
queue.push(s.c_str(), s.length());

// To get data from the queue
auto nbytes = queue.available();
auto data = queue.peek();
std::string hello(data, nbytes);

 // Prints Hello World!
std::cout << hello << std::endl;

```

