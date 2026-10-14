# bmkpp

A dependency-free, header-only **C++17 benchmarking library** inspired by
BenchmarkDotNet. Register callbacks, compare implementations against a baseline,
and export timing results without installing a framework or compiling a library.

## Features

- Adaptive calibration targeting 10 ms batches, with a configurable invocation cap.
- Warmup and repeated measurements using `std::chrono::steady_clock`.
- Mean, median, sample standard deviation, standard error, minimum, and maximum.
- Benchmark groups with one optional baseline per group.
- Parameterized cases and case-level and batch-level setup/cleanup hooks.
- Console tables, CSV export, and raw per-batch samples in nanoseconds per operation.
- `do_not_optimize` and `clobber_memory` helpers for microbenchmarks.

## Quick start

New to benchmarking? Follow [Getting started](./gettingstarted.md) to go from an
empty source file to a benchmarked function, step by step.

Add the [include directory](./include) to your compiler's include path, or copy
[bmkpp.hpp](./include/bmkpp/bmkpp.hpp) into your project's include path. Only a
C++17 compiler and the standard library are required.

```cpp
#include <bmkpp/bmkpp.hpp>
#include <iostream>
#include <numeric>
#include <vector>

int main() {
    std::vector<int> data(1024, 1);
    bmkpp::suite benchmarks;

    bmkpp::case_options options;
    options.group = "sum";
    options.baseline = true;
    benchmarks.add("loop", [&] {
        bmkpp::clobber_memory();
        long long sum = 0;
        for (int value : data) sum += value;
        return sum;
    }, options);

    options.baseline = false;
    benchmarks.add("accumulate", [&] {
        bmkpp::clobber_memory();
        return std::accumulate(data.begin(), data.end(), 0LL);
    }, options);

    bmkpp::print_table(std::cout, benchmarks.run());
}
```

Returned values are consumed automatically. For callbacks returning `void`, use
`bmkpp::do_not_optimize(value)` explicitly on observable results.

The reported ratio is the case's mean time divided by its group's baseline mean:
**lower is faster**, and the baseline ratio is 1 when its measured mean is nonzero.
Ratios are unavailable when the baseline is absent or measures zero.

## CMake integration

Include [bmkpp.cmake](./bmkpp.cmake) from your existing CMake project and link the
interface target to propagate the include path and C++17 requirement:

```cmake
include("${CMAKE_CURRENT_SOURCE_DIR}/third_party/bmkpp/bmkpp.cmake")
target_link_libraries(my_benchmarks PRIVATE bmkpp::bmkpp)
```

This assumes the repository is located at `third_party/bmkpp` within your project.

## Build the example and tests

The [standalone project](./standalone/CMakeLists.txt) requires CMake 3.14 or newer
and a C++17 compiler. Run these commands from the repository root:

```powershell
cmake -S standalone -B build-bmkpp -DCMAKE_BUILD_TYPE=Release
cmake --build build-bmkpp --config Release
ctest --test-dir build-bmkpp -C Release --output-on-failure
```

With a Visual Studio generator, run the example from PowerShell:

```powershell
.\build-bmkpp\Release\bmkpp_example.exe
.\build-bmkpp\Release\bmkpp_example.exe "size=1024" results.csv
```

With a single-configuration generator such as Ninja or Unix Makefiles, the
executable is directly in the build directory:

```bash
./build-bmkpp/bmkpp_example
./build-bmkpp/bmkpp_example "size=1024" results.csv
```

The first argument filters case names or groups by a case-sensitive substring.
The optional second argument writes CSV results. Omit the filter to run all cases;
use an empty string as the first argument to export all cases.

See the [example source](./examples/bmkpp_example.cpp) for comparisons at multiple
input sizes and CSV export.

## Configuration

Pass a `bmkpp::config` to the suite constructor to override the defaults:

| Setting | Default | Purpose |
| --- | --- | --- |
| `warmup_iterations` | 3 | Warmup batches excluded from reported statistics |
| `measurement_iterations` | 15 | Batches retained for statistics |
| `target_iteration_time` | 10 ms | Target duration used during calibration |
| `max_invocations` | 16,777,216 | Maximum callback invocations per batch |

Use `case_options::operations_per_invocation` when a callback performs multiple
logical operations; reported times are normalized by that count.

Case-level `setup` and `cleanup` run once per selected case per run. Batch-level
`iteration_setup` and `iteration_cleanup` run outside timing for every calibration,
warmup, and measurement batch—not for each callback invocation.

For parameter expansion, use `suite::add_parameters` with a vector of values and
a callback accepting one value. To compare implementations at each input size,
register them in matching groups, as demonstrated in the example.

## Measurement caveats

- Build optimized code and run on an idle machine without a debugger.
- Cases execute sequentially in registration order in the current process.
- Timings include callback dispatch and loop overhead; no overhead is subtracted.
- Samples are retained without removing outliers. Standard error is not a
  confidence interval.
- Compiler barriers are stronger on GCC/Clang than the portable fallback used by
  other compilers. Inspect optimized assembly for important microbenchmarks,
  especially with MSVC or link-time optimization.
- The invocation cap may prevent reaching the target duration. It does not impose
  a timeout on a blocking or slow callback.

bmkpp provides a small benchmarking workflow, not BenchmarkDotNet's process
isolation, allocation tracking, hardware counters, disassembly diagnostics, or
runtime comparisons. It does not manage CPU affinity or power settings.

See [the detailed guide](./BMKPP.md) for lifecycle semantics, exception behavior,
and further guidance on interpreting results.
