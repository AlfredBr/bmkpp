#ifndef BMKPP_BMKPP_HPP
#define BMKPP_BMKPP_HPP

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace bmkpp {

// GCC/Clang barriers work for arbitrary objects, including large containers.
// Other compilers use a volatile read and a signal fence as a portable fallback.
// Inspect generated code for critical microbenchmarks, especially with LTO.
template <class T> inline void do_not_optimize(const T& value) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "m"(value) : "memory");
#else
    const volatile unsigned char* p =
        reinterpret_cast<const volatile unsigned char*>(&value);
    (void)*p;
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

inline void clobber_memory() noexcept {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : : "memory");
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

struct config {
    std::size_t warmup_iterations = 3;
    std::size_t measurement_iterations = 15;
    std::chrono::nanoseconds target_iteration_time = std::chrono::milliseconds(10);
    std::size_t max_invocations = 1u << 24;
};

struct case_options {
    std::string group = "default";
    bool baseline = false;
    std::size_t operations_per_invocation = 1;
    std::function<void()> setup;
    std::function<void()> cleanup;
    // Called once per batch, outside timing (also during calibration/warmup).
    std::function<void()> iteration_setup;
    std::function<void()> iteration_cleanup;
};

struct statistics {
    double mean_ns = 0;
    double median_ns = 0;
    double stddev_ns = 0;
    double standard_error_ns = 0;
    double min_ns = 0;
    double max_ns = 0;
};

inline statistics summarize(const std::vector<double>& samples) {
    if (samples.empty()) throw std::invalid_argument("samples must not be empty");
    statistics s;
    std::size_t n = 0;
    double m2 = 0;
    for (double x : samples) {
        if (!std::isfinite(x) || x < 0)
            throw std::invalid_argument("samples must be finite and nonnegative");
        ++n;
        const double delta = x - s.mean_ns;
        s.mean_ns += delta / static_cast<double>(n);
        m2 += delta * (x - s.mean_ns);
    }
    auto sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    s.min_ns = sorted.front();
    s.max_ns = sorted.back();
    s.median_ns = n % 2 ? sorted[n / 2]
        : sorted[n / 2 - 1] / 2 + sorted[n / 2] / 2;
    s.stddev_ns = n > 1 ? std::sqrt(std::max(0.0, m2 / (n - 1))) : 0;
    s.standard_error_ns = s.stddev_ns / std::sqrt(static_cast<double>(n));
    return s;
}

struct result {
    std::string name;
    std::string group;
    bool baseline = false;
    std::size_t invocations_per_iteration = 0;
    std::size_t operations_per_invocation = 1;
    std::vector<double> samples_ns;
    statistics stats;
    // NaN means the group has no baseline, or its baseline measured zero.
    double ratio = std::numeric_limits<double>::quiet_NaN();
};

namespace detail {
inline std::string csv_quote(const std::string& value) {
    std::string out = "\"";
    for (char c : value) { if (c == '"') out += '"'; out += c; }
    return out + '"';
}

template <class Body>
inline void with_cleanup(Body&& body, const std::function<void()>& cleanup) {
    try { body(); }
    catch (...) {
        // Preserve the original failure if cleanup also throws.
        if (cleanup) { try { cleanup(); } catch (...) {} }
        throw;
    }
    if (cleanup) cleanup();
}
} // namespace detail

class suite {
    struct entry {
        std::string name;
        std::function<void()> body;
        case_options options;
    };
    std::vector<entry> entries_;
    config config_;

    static void validate(const config& c) {
        if (!c.measurement_iterations || !c.max_invocations ||
            c.target_iteration_time.count() <= 0)
            throw std::invalid_argument("measurement count, target time and invocation cap must be positive");
    }

    static double batch(const entry& e, std::size_t count) {
        if (e.options.iteration_setup) e.options.iteration_setup();
        double elapsed = 0;
        detail::with_cleanup([&] {
            const auto start = std::chrono::steady_clock::now();
            for (std::size_t i = 0; i < count; ++i) e.body();
            const auto end = std::chrono::steady_clock::now();
            elapsed = std::chrono::duration<double, std::nano>(end - start).count();
        }, e.options.iteration_cleanup);
        return elapsed;
    }

public:
    explicit suite(config settings = {}) : config_(settings) { validate(settings); }

    template <class F>
    suite& add(std::string name, F body, case_options options = {}) {
        static_assert(std::is_invocable_v<F&>, "benchmark must accept no arguments");
        static_assert(std::is_copy_constructible_v<F>, "benchmark must be copyable");
        if (name.empty() || options.group.empty() || !options.operations_per_invocation)
            throw std::invalid_argument("name, group and operations count must be nonempty/positive");
        for (const auto& e : entries_) {
            if (e.options.group == options.group && e.name == name)
                throw std::invalid_argument("duplicate benchmark name in group");
            if (e.options.group == options.group && e.options.baseline && options.baseline)
                throw std::invalid_argument("a group can have only one baseline");
        }
        // Returned values are consumed automatically; void callbacks should
        // explicitly consume observable results via do_not_optimize().
        auto invoke = [fn = std::move(body)]() mutable {
            if constexpr (std::is_void_v<std::invoke_result_t<F&>>) std::invoke(fn);
            else { decltype(auto) value = std::invoke(fn); do_not_optimize(value); }
        };
        entries_.push_back({std::move(name), std::move(invoke), std::move(options)});
        return *this;
    }

    // Each parameter becomes an independent case. Put matching implementations
    // in the same group (e.g. "size=1024") to obtain per-parameter ratios.
    template <class T, class F>
    suite& add_parameters(const std::string& name, const std::vector<T>& values,
                          F body, case_options options = {}) {
        for (const auto& value : values) {
            std::ostringstream label;
            label << name << " [" << value << ']';
            add(label.str(), [body, value]() mutable -> decltype(auto) {
                return std::invoke(body, value);
            }, options);
        }
        return *this;
    }

    std::vector<result> run(const std::string& filter = {}) const {
        std::vector<result> results;
        for (const auto& e : entries_) {
            if (!filter.empty() && e.name.find(filter) == std::string::npos &&
                e.options.group.find(filter) == std::string::npos) continue;
            result r;
            r.name = e.name;
            r.group = e.options.group;
            r.baseline = e.options.baseline;
            r.operations_per_invocation = e.options.operations_per_invocation;
            if (e.options.setup) e.options.setup();
            detail::with_cleanup([&] {
                std::size_t count = 1;
                // Double until reaching the requested batch duration or cap.
                // The cap also bounds calibration for clock-resolution limits.
                while (true) {
                    const double elapsed = batch(e, count);
                    if (elapsed >= static_cast<double>(config_.target_iteration_time.count()) ||
                        count == config_.max_invocations) break;
                    count = count > config_.max_invocations / 2
                        ? config_.max_invocations : count * 2;
                }
                r.invocations_per_iteration = count;
                for (std::size_t i = 0; i < config_.warmup_iterations; ++i) batch(e, count);
                r.samples_ns.reserve(config_.measurement_iterations);
                for (std::size_t i = 0; i < config_.measurement_iterations; ++i) {
                    r.samples_ns.push_back(batch(e, count) /
                        static_cast<double>(count) / static_cast<double>(r.operations_per_invocation));
                }
                r.stats = summarize(r.samples_ns);
            }, e.options.cleanup);
            results.push_back(std::move(r));
        }
        for (auto& r : results) {
            for (const auto& baseline : results) {
                if (baseline.group == r.group && baseline.baseline && baseline.stats.mean_ns > 0) {
                    r.ratio = r.stats.mean_ns / baseline.stats.mean_ns;
                    break;
                }
            }
        }
        return results;
    }
};

inline void print_table(std::ostream& output, const std::vector<result>& results) {
    // Format locally so callers' stream formatting is preserved.
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const std::vector<std::string> headings = {
        "Benchmark", "Group", "Mean (ns/op)", "Median (ns/op)",
        "StdDev (ns/op)", "StdErr (ns/op)", "Ratio", "Baseline"
    };
    std::vector<std::size_t> widths;
    for (const auto& heading : headings) widths.push_back(heading.size());
    std::vector<std::vector<std::string>> rows;
    const auto number = [](double value) {
        std::ostringstream cell;
        cell.imbue(std::locale::classic());
        cell << std::fixed << std::setprecision(3) << value;
        return cell.str();
    };
    const auto label = [](std::string value) {
        for (char& c : value) {
            if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
        }
        return value;
    };
    for (const auto& r : results) {
        rows.push_back({label(r.name), label(r.group), number(r.stats.mean_ns),
            number(r.stats.median_ns), number(r.stats.stddev_ns),
            number(r.stats.standard_error_ns),
            std::isfinite(r.ratio) ? number(r.ratio) : "-",
            r.baseline ? "yes" : ""});
        for (std::size_t i = 0; i < widths.size(); ++i)
            widths[i] = std::max(widths[i], rows.back()[i].size());
    }
    const auto border = [&] {
        out << '+';
        for (auto width : widths) out << std::string(width + 2, '-') << '+';
        out << '\n';
    };
    const auto row = [&](const std::vector<std::string>& cells, bool header) {
        out << '|';
        for (std::size_t i = 0; i < cells.size(); ++i) {
            const bool numeric = i >= 2 && i <= 6;
            out << ' ' << ((numeric && !header) ? std::right : std::left)
                << std::setw(static_cast<int>(widths[i])) << cells[i] << " |";
        }
        out << '\n';
    };
    border();
    row(headings, true);
    border();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (i && results[i].group != results[i - 1].group) border();
        row(rows[i], false);
    }
    if (!rows.empty()) border();
    output << out.str();
}

inline void write_csv(std::ostream& output, const std::vector<result>& results) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "benchmark,group,baseline,invocations,operations,mean_ns,median_ns,stddev_ns,stderr_ns,min_ns,max_ns,ratio\n";
    out << std::setprecision(17);
    for (const auto& r : results) {
        out << detail::csv_quote(r.name) << ',' << detail::csv_quote(r.group) << ','
            << (r.baseline ? "true" : "false") << ',' << r.invocations_per_iteration << ','
            << r.operations_per_invocation << ',' << r.stats.mean_ns << ',' << r.stats.median_ns
            << ',' << r.stats.stddev_ns << ',' << r.stats.standard_error_ns << ','
            << r.stats.min_ns << ',' << r.stats.max_ns << ',';
        if (std::isfinite(r.ratio)) out << r.ratio;
        out << '\n';
    }
    output << out.str();
}

} // namespace bmkpp
#endif // BMKPP_BMKPP_HPP
