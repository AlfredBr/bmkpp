#include <bmkpp/bmkpp.hpp>
#include <fstream>
#include <iostream>
#include <numeric>
#include <vector>

int main(int argc, char** argv) {
    try {
        bmkpp::suite benchmarks;
        std::vector<int> data;
        for (std::size_t size : {1024u, 65536u}) {
            bmkpp::case_options options;
            options.group = "size=" + std::to_string(size);
            options.setup = [&, size] { data.assign(size, 1); };
            options.cleanup = [&] { data.clear(); };
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
        }
        const auto results = benchmarks.run(argc > 1 ? argv[1] : "");
        bmkpp::print_table(std::cout, results);
        if (argc > 2) {
            std::ofstream csv(argv[2]);
            csv.exceptions(std::ios::failbit | std::ios::badbit);
            bmkpp::write_csv(csv, results);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
