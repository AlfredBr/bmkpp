#include <bmkpp/bmkpp.hpp>
#include <iostream>
#include <sstream>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template <class F> static void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "expected invalid_argument");
}

int main() {
    try {
        const auto stats = bmkpp::summarize({1, 2, 3, 4});
        require(stats.mean_ns == 2.5 && stats.median_ns == 2.5, "central statistics");
        require(std::abs(stats.stddev_ns - std::sqrt(5.0 / 3)) < 1e-12, "sample standard deviation");
        require(stats.min_ns == 1 && stats.max_ns == 4, "extrema");
        rejects([] { bmkpp::summarize({}); });
        rejects([] { bmkpp::summarize({-1}); });
        rejects([] { bmkpp::summarize({std::numeric_limits<double>::quiet_NaN()}); });
        bmkpp::config cfg;
        cfg.measurement_iterations = 3;
        cfg.warmup_iterations = 2;
        cfg.max_invocations = 1;
        cfg.target_iteration_time = std::chrono::nanoseconds(1);
        auto bad = cfg;
        bad.measurement_iterations = 0;
        rejects([&] { bmkpp::suite invalid(bad); });

        int setup = 0, cleanup = 0, before = 0, after = 0, calls = 0;
        bmkpp::case_options options;
        options.baseline = true;
        options.setup = [&] { ++setup; };
        options.cleanup = [&] { ++cleanup; };
        options.iteration_setup = [&] { ++before; };
        options.iteration_cleanup = [&] { ++after; };
        bmkpp::suite suite(cfg);
        suite.add("base", [&] { return ++calls; }, options);
        rejects([&] { suite.add("base", [] {}, options); });
        rejects([&] { suite.add("second baseline", [] {}, options); });
        options.baseline = false;
        suite.add("alternative", [] { return 42; }, options);
        const auto results = suite.run("base");
        require(results.size() == 1 && results[0].samples_ns.size() == 3, "filter/sample count");
        require(calls == 6 && before == 6 && after == 6, "calibration/warmup/measurement lifecycle");
        require(setup == 1 && cleanup == 1, "global lifecycle");
        require(results[0].invocations_per_iteration == 1, "invocation cap");
        require(!std::isfinite(results[0].ratio) || results[0].ratio == 1, "baseline ratio");
        require(suite.run("missing").empty(), "empty filter result");

        bmkpp::suite parameters(cfg);
        parameters.add_parameters("param", std::vector<int>{1, 2}, [](int n) { return n * n; });
        require(parameters.run().size() == 2, "parameter expansion");

        bmkpp::suite failure(cfg);
        before = after = setup = cleanup = 0;
        failure.add("throws", [] { throw std::runtime_error("body failure"); }, options);
        bool threw = false;
        try { failure.run(); } catch (const std::runtime_error& e) {
            threw = std::string(e.what()) == "body failure";
        }
        require(threw && before == 1 && after == 1 && setup == 1 && cleanup == 1,
                "cleanup on body failure");

        auto escaped = results;
        escaped[0].name = "name,\"quoted\"";
        std::ostringstream csv;
        bmkpp::write_csv(csv, escaped);
        require(csv.str().find("\"name,\"\"quoted\"\"\"") != std::string::npos, "CSV escaping");
        std::cout << "All bmkpp tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
