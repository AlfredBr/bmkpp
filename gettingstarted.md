# Getting started: from an empty file to a benchmarked function

This walkthrough builds a small program that measures the time needed to sum a
vector of integers. Start with an empty C++ file, add a function, check its result,
and then benchmark it with bmkpp.

## 1. Prepare your tools

You need:

- A C++17 compiler.
- CMake 3.14 or newer.
- A local copy of this repository.

On Windows, install Visual Studio or Visual Studio Build Tools with the
**Desktop development with C++** workload. VS Code alone does not include a C++
compiler. The Windows commands below use PowerShell and a Visual Studio CMake
generator.

Open the repository folder in VS Code, then open **Terminal > New Terminal**.
Run all commands below from the repository root.

## 2. Create an empty source file

Create a folder named `gettingstarted-example` in the repository root. Inside it,
create an empty file named `main.cpp` and open it in the editor.

Your new files will eventually look like this:

```text
gettingstarted-example/
    main.cpp
    CMakeLists.txt
```

This is a separate example project; you do not need to change the library or its
existing standalone project.

## 3. Write the function you want to measure

Paste this complete program into the empty `main.cpp` file:

```cpp
#include <iostream>
#include <vector>

long long sum_loop(const std::vector<int>& values) {
    long long total = 0;
    for (int value : values) {
        total += value;
    }
    return total;
}

int main() {
    const std::vector<int> values(1024, 1);
    std::cout << "Sum: " << sum_loop(values) << '\n';
}
```

`values` contains 1,024 integers, all equal to 1, so the expected sum is 1,024.
Check correctness before measuring performance.

## 4. Tell CMake how to build it

Create `CMakeLists.txt` beside `main.cpp` and paste in:

```cmake
cmake_minimum_required(VERSION 3.14)
project(bmkpp_gettingstarted LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../bmkpp.cmake")

add_executable(first_benchmark main.cpp)
target_link_libraries(first_benchmark PRIVATE bmkpp::bmkpp)
```

Including [bmkpp.cmake](./bmkpp.cmake) creates the `bmkpp::bmkpp` interface target.
Linking it supplies the header search path and enables C++17; there is no compiled
bmkpp library to install.

Save both files, then configure and build:

```powershell
cmake -S gettingstarted-example -B build-gettingstarted -DCMAKE_BUILD_TYPE=Release
cmake --build build-gettingstarted --config Release
```

Run the program:

```powershell
.\build-gettingstarted\Release\first_benchmark.exe
```

Expected output:

```text
Sum: 1024
```

For a single-configuration generator such as Ninja or Unix Makefiles, the same
configure/build commands work, but the executable is directly in the build
folder. On Windows, run:

```powershell
.\build-gettingstarted\first_benchmark.exe
```

On Linux or macOS, run:

```bash
./build-gettingstarted/first_benchmark
```

## 5. Turn the function call into a benchmark

Add this include at the top of `main.cpp`:

```cpp
#include <bmkpp/bmkpp.hpp>
```

Keep `sum_loop` unchanged. Replace the entire `main` function with:

```cpp
int main() {
    const std::vector<int> values(1024, 1);
    if (sum_loop(values) != 1024) {
        std::cerr << "Unexpected sum\n";
        return 1;
    }

    bmkpp::suite benchmarks;
    benchmarks.add("sum_loop", [&] {
        bmkpp::clobber_memory();
        return sum_loop(values);
    });

    const auto results = benchmarks.run();
    bmkpp::print_table(std::cout, results);
}
```

Here is what changed:

1. `suite` holds the benchmark cases.
2. `add` registers a named callback; it does not run the benchmark yet.
3. The callback captures `values` by reference and calls your function.
4. `clobber_memory` helps prevent the compiler from reusing an earlier result for
   the unchanged input. It is included in the timed callback.
5. Returning the sum lets bmkpp consume it automatically. For a `void` callback,
   explicitly consume its result with `bmkpp::do_not_optimize(result)` instead.
6. `run` calibrates the invocation count, warms up, and records measurements.
7. `print_table` displays the results.

The vector is constructed before timing starts. Each callback invocation sums
one vector, so one reported operation means **one complete call to `sum_loop`**,
not one integer addition.

`bmkpp::suite benchmarks;` creates a collection of benchmark cases. `benchmarks.add("sum_loop", ...)` registers a case named `sum_loop`; it does not execute it yet.

The lambda `([&] { ... })` captures surrounding variables by reference, allowing it to access values without copying the vector. The vector must remain alive while the benchmark runs.

`bmkpp::clobber_memory()` provides a compiler barrier intended to discourage reusing previously computed results. It does not clear memory, and its effectiveness depends on the compiler.

`return sum_loop(values);` calls the function and returns its result, which bmkpp automatically consumes to discourage eliminating the computation. When benchmarks.run() is called, this callback executes repeatedly during calibration, warmup, and measurement.

## 6. Rebuild and measure

Save `main.cpp`, rebuild, and run the executable again:

```powershell
cmake --build build-gettingstarted --config Release
.\build-gettingstarted\Release\first_benchmark.exe
```

Use the executable path from step 4 if you chose a different generator or OS.
Run the executable directly rather than under the debugger, and keep the machine
as idle as possible.

The output is now a table with a `sum_loop` row. Actual numbers depend on your
compiler, optimization settings, CPU, and background activity.

| Column | Meaning |
| --- | --- |
| Benchmark | The name passed to `add` |
| Group | `default`, unless you assign a group |
| Mean (ns/op) | Average measured time per complete vector sum |
| Median (ns/op) | Middle measured batch sample, normalized per operation |
| StdDev (ns/op) | Sample standard deviation across normalized batch samples |
| StdErr (ns/op) | Standard error of the mean, not a confidence interval |
| Ratio | `-` here, because no baseline was registered |
| Baseline | Empty here, because this case is not marked as a baseline |

By default, bmkpp calibrates toward 10 ms batches, runs three warmup batches,
and retains fifteen measurement batches. A batch may invoke your callback many
times. Calibration and warmup samples do not contribute to the reported statistics.

**You now have a benchmarked function.**

## 7. Optional: compare a second implementation

Add `<numeric>` to the includes:

```cpp
#include <numeric>
```

In `main`, replace the suite construction and the existing `benchmarks.add` call
with this block. Keep the correctness check and the `run`/`print_table` calls:

```cpp
bmkpp::suite benchmarks;
bmkpp::case_options options;
options.group = "sum-1024";
options.baseline = true;

benchmarks.add("sum_loop", [&] {
    bmkpp::clobber_memory();
    return sum_loop(values);
}, options);

options.baseline = false;
benchmarks.add("std::accumulate", [&] {
    bmkpp::clobber_memory();
    return std::accumulate(values.begin(), values.end(), 0LL);
}, options);
```

Both cases now belong to the same group, with `sum_loop` as the baseline. The
ratio is the case's mean divided by the baseline mean: 0.8 means roughly 20% less
time per operation; 1.2 means roughly 20% more time. Lower is faster. A zero-time
baseline leaves ratios unavailable.

Do not assume the second implementation will be faster. Compilers can generate
similar code for both, and small differences can be measurement noise.

## 8. Optional: save results as CSV

Add `<fstream>` to the includes:

```cpp
#include <fstream>
```

After the existing `print_table` call, add:

```cpp
std::ofstream csv("results.csv");
csv.exceptions(std::ios::failbit | std::ios::badbit);
bmkpp::write_csv(csv, results);
```

Rebuild and run again. The program writes `results.csv` in the terminal's current
working directory, overwriting any previous file with that name. CSV contains
summary statistics; raw per-batch samples are available through each returned
result's `samples_ns` member.

## Next steps and limitations

- Change the vector size to measure a different workload.
- Use `bmkpp::config` to adjust warmup, measurement count, or batch duration.
- Use setup/cleanup hooks for work that should be excluded from timing.
- See the [repository example](./examples/bmkpp_example.cpp) for grouped
  comparisons at multiple input sizes.
- Read [the README](./README.md) for configuration defaults and
  [the detailed guide](./BMKPP.md) for lifecycle and exception semantics.

Reported time includes callback dispatch and loop overhead; bmkpp does not
subtract it. Compiler barriers are not a guarantee against every optimization:
inspect optimized assembly for important microbenchmarks, especially on MSVC or
with link-time optimization. A single timing run is not proof that one function
is universally faster.
