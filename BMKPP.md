# bmkpp

A dependency-free, header-only C++17 benchmarking library inspired by
BenchmarkDotNet. Copy `include/bmkpp/bmkpp.hpp` into your include path.

```cpp
#include <bmkpp/bmkpp.hpp>
#include <iostream>

int main() {
    bmkpp::suite benchmarks;
    bmkpp::case_options baseline;
    baseline.baseline = true;
    benchmarks.add("sum", [] {
        unsigned sum = 0;
        for (unsigned i = 0; i < 1000; ++i) {
            bmkpp::do_not_optimize(i);
            sum += i;
        }
        return sum; // Returned values are consumed automatically.
    }, baseline);
    bmkpp::print_table(std::cout, benchmarks.run());
}
```

## Build the example and tests

```sh
cmake -S standalone -B build-bmkpp -DCMAKE_BUILD_TYPE=Release
cmake --build build-bmkpp --config Release
ctest --test-dir build-bmkpp -C Release --output-on-failure
```

With a single-configuration generator, run `build-bmkpp/bmkpp_example`.
With Visual Studio, run `build-bmkpp/Release/bmkpp_example.exe`.
The first argument is an optional case/group substring filter; the second is
an optional CSV path. An empty filter selects all cases:

```sh
build-bmkpp/Release/bmkpp_example.exe "" results.csv
```

In an existing CMake project, `include(path/to/bmkpp.cmake)` and link
`bmkpp::bmkpp`. No compiled library or package installation is required.

## Features and measurement model

- Adaptive invocation counts targeting 10 ms batches, bounded by a configurable cap.
- Three warmup batches and fifteen measured batches by default.
- Mean, median, sample standard deviation, standard error, minimum and maximum.
- One baseline per group; ratios are mean / baseline mean (lower is faster).
- Parameter expansion using `add_parameters(name, std::vector<T>, callback)`.
- Global and per-batch setup/cleanup hooks, excluded from timing.
- Console tables, CSV escaping, and raw per-batch ns/op samples in each result.
- `do_not_optimize` and `clobber_memory` compiler barriers.

Pass a `bmkpp::config` to the suite constructor to change warmup count,
measurement count, target duration, or maximum invocations. Use
`case_options::operations_per_invocation` when one callback performs multiple
logical operations: reported times are normalized by that count.

Cases run sequentially in registration order in the current process. A callback
can execute many times during calibration, warmup and measurement. Per-batch
setup runs once before those repeated invocations, not once per invocation.
Global setup runs once per selected case per `run()`; cleanup runs after a
successful setup even if the body fails. If setup itself throws, its associated
cleanup is not called. Exceptions stop the run and propagate to the caller;
cleanup exceptions during an existing failure are suppressed to preserve it.

Parameter values must be copyable and streamable, and callbacks must be
copyable. `add_parameters` shares the supplied group; for comparisons across
implementations at each parameter value, register cases manually with a group
such as `size=1024`, as the example does. Filtering out a baseline leaves ratios
empty. Samples are retained without removing outliers. Standard error is
sample standard deviation divided by the square root of the batch count; it
is not a confidence interval.

## Interpreting results

Build optimized code and run on an idle machine without a debugger. Consume
results and prevent hoisting of invariant work where appropriate. GCC/Clang
use inline assembly barriers; other compilers use a weaker portable fallback.
Inspect optimized assembly for important microbenchmarks, particularly on MSVC
and with link-time optimization. Barriers do not make invalid C++ code safe.

Reported times include callback dispatch and loop overhead; no overhead is
subtracted. Very short operations should be grouped into meaningful batches.
The invocation cap can prevent the requested batch duration from being reached.
There is no runtime limit on a callback that blocks or takes a long time.

This library supplies the core benchmark workflow. It does not reproduce
BenchmarkDotNet's process isolation, generated executables, allocation tracking,
hardware counters, disassembly diagnostics, or runtime comparisons. It does
not change CPU affinity or power settings, and suites are not safe to mutate
or run concurrently when callbacks share state.
