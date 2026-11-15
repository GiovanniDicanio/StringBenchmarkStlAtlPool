# StringBenchmarkStlAtlPool
A C++ string benchmark, comparing the performance of STL strings vs. ATL CString vs. Custom Pool Allocator strings 

Related blog post:
https://giodicanio.com/2026/11/15/c-plus-plus-string-benchmark-stl-vs-atl-vs-custom-pool-allocator-2026/

The **string pool allocator code** is implemented in the [`StringPool.hpp`](./StringBenchmarkStlAtlPool/StringPool.hpp) 
and [`StringPool.cpp`](./StringBenchmarkStlAtlPool/StringPool.cpp) files.

The benchmark code is in the [`StringBenchmarkStlAtlPool.cpp`](./StringBenchmarkStlAtlPool/StringBenchmarkStlAtlPool.cpp) file.

There is also an Excel file containing a [sample of benchmark results](./Benchmark_Results.xlsx).

