#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

#include "algorithms/AceSketch.h"
#include "algorithms/hll.h"
#include "algorithms/hydraSkt.h"
#include "algorithms/omniSkt.h"
#include "utils/data_reader.h"

namespace {

struct Options {
    int dataset = 0, query = 0, debug = 0, algorithm = 0, experiment = 1;
    double memory = 16;
    uint32_t seed = 2026;
    std::filesystem::path data_root = "..";
    std::string data_file, query_file;
};

void usage() {
    std::cout << "Usage: acesketch [dataset query debug algorithm experiment] [options]\n"
                 "  dataset: 0=CAIDA, 1=ECB, 2=Criteo\n"
                 "  query: 0=single, 1=OR, 2=AND, 3=range\n"
                 "  debug: 0=quiet, 1=show sample rows and sketch parameters\n"
                 "  algorithm: 0=AceSketch, 1=OmniSkt, 2=HydraSkt, 3=vHLL\n"
                 "  --memory MB       Logical sketch memory (default: 16)\n"
                 "  --seed N          Random seed (default: 2026)\n"
                 "  --data-root DIR   Dataset directory (default: ..)\n"
                 "  --data-file FILE  Override the dataset file\n"
                 "  --query-file FILE Override the query file\n";
}

Options parse_options(int argc, char* argv[]) {
    Options options;
    int next = 1;
    if (argc > 1 && std::string(argv[1]).rfind("--", 0) != 0) {
        if (argc < 6) throw std::runtime_error("Provide all five positional arguments");
        auto number = [&](int i) {
            auto value = DataReader::integer(argv[i]);
            if (value > INT32_MAX) throw std::runtime_error("Argument is too large");
            return static_cast<int>(value);
        };
        options.dataset = number(1);
        options.query = number(2);
        options.debug = number(3);
        options.algorithm = number(4);
        options.experiment = number(5);
        next = 6;
    }
    while (next < argc) {
        std::string flag = argv[next++];
        if (next == argc) throw std::runtime_error("Missing value for " + flag);
        std::string value = argv[next++];
        if (flag == "--memory") {
            std::size_t consumed = 0;
            options.memory = std::stod(value, &consumed);
            if (consumed != value.size()) throw std::runtime_error("Invalid memory value");
        } else if (flag == "--seed") {
            auto seed = DataReader::integer(value);
            if (seed > UINT32_MAX) throw std::runtime_error("Seed exceeds uint32_t");
            options.seed = static_cast<uint32_t>(seed);
        } else if (flag == "--data-root") {
            options.data_root = value;
        } else if (flag == "--data-file") {
            options.data_file = value;
        } else if (flag == "--query-file") {
            options.query_file = value;
        } else {
            throw std::runtime_error("Unknown option: " + flag);
        }
    }
    if (options.dataset > 2 || options.query > 3 || options.debug > 1 ||
        options.algorithm > 3 || options.experiment < 1) {
        throw std::runtime_error("Invalid experiment arguments; see --help");
    }
    if (!std::isfinite(options.memory) || options.memory < 0.01 || options.memory > 1024) {
        throw std::runtime_error("Memory must be between 0.01 and 1024 MB");
    }
    if (options.dataset == 2 && options.query == 3) {
        throw std::runtime_error("Criteo has no range-query configuration");
    }
    if (options.algorithm == 1 && options.query == 1) {
        throw std::runtime_error("The OmniSkt benchmark supports single, AND and range queries");
    }
    if (options.algorithm >= 2 &&
        (options.dataset != 0 || (options.query != 0 && options.query != 2))) {
        throw std::runtime_error("HydraSkt and vHLL benchmarks support CAIDA single and AND queries");
    }
    return options;
}

struct Metrics {
    double relative = 0, absolute = 0, squared = 0;
    double insert_throughput = 0, query_throughput = 0;
    std::array<double, 4> relative_sums{};
    std::array<std::size_t, 4> counts{};
};

template <class Sketch>
Metrics benchmark(Sketch& sketch, DataReader& data, int query_type) {
    using Clock = std::chrono::steady_clock;
    auto throughput = [](std::size_t count, Clock::time_point start) {
        double seconds = std::chrono::duration<double>(Clock::now() - start).count();
        return count / 1e6 / seconds;
    };
    Metrics result;
    auto start = Clock::now();
    for (auto& row : data.data_stream) {
        if (query_type == 3) sketch.range_insert(row.data(), data.attr_num, data.RANGE_ATTR_IDX);
        else sketch.insert(row.data(), data.attr_num);
    }
    result.insert_throughput = throughput(data.data_stream.size(), start);

    auto estimate = [&](Query& query) {
        if (query_type == 3) return sketch.query_range(query.left, query.right);
        int size = static_cast<int>(query.predicates.size());
        if (query_type == 2) {
            return sketch.query_intersection(query.predicates.data(), query.indices.data(), size);
        }
        return sketch.query_union(query.predicates.data(), query.indices.data(), size);
    };

    // Store predictions to time only querying and avoid a second, potentially mutating pass.
    std::vector<double> predictions(data.query_stream.size());
    start = Clock::now();
    for (std::size_t i = 0; i < predictions.size(); ++i) predictions[i] = estimate(data.query_stream[i]);
    result.query_throughput = throughput(predictions.size(), start);

    for (std::size_t i = 0; i < predictions.size(); ++i) {
        if (!std::isfinite(predictions[i])) throw std::runtime_error("Non-finite cardinality estimate");
        double truth = data.query_stream[i].truth;
        double error = std::abs(predictions[i] - truth);
        double relative = truth == 0 ? 0 : error / truth;
        result.relative += relative;
        result.absolute += error;
        result.squared += error * error;
        int bucket = truth <= 100 ? 0 : truth <= 1000 ? 1 : truth <= 10000 ? 2 : 3;
        result.relative_sums[bucket] += relative;
        ++result.counts[bucket];
    }
    double count = static_cast<double>(predictions.size());
    result.relative /= count;
    result.absolute /= count;
    result.squared /= count;
    return result;
}

void write_result(std::ostream& output, const char* name, const Metrics& result) {
    output << std::setprecision(10) << name << ' ' << result.relative << ' '
           << result.absolute << ' ' << result.squared << ' '
           << result.insert_throughput << ' ' << result.query_throughput;
    for (std::size_t i = 0; i < result.counts.size(); ++i) {
        output << ' ' << result.relative_sums[i] << ':' << result.counts[i];
    }
    output << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        usage();
        return 0;
    }
    try {
        Options options = parse_options(argc, argv);
        DataReader data(options.dataset, options.query, options.data_root,
                        options.data_file, options.query_file);
        if (options.debug) {
            for (std::size_t i = 0; i < std::min(std::size_t{2}, data.data_stream.size()); ++i) {
                for (const auto& field : data.data_stream[i]) std::cout << field << ' ';
                std::cout << '\n';
            }
        }

        std::mt19937 generator(options.seed);
        std::array<uint32_t, 10> seeds{}, secondary_seeds{};
        for (std::size_t i = 0; i < seeds.size(); ++i) {
            seeds[i] = generator();
            secondary_seeds[i] = generator();
        }
        uint32_t fingerprint_seed = generator(), hll_seed = generator();
        std::srand(options.seed);
        std::cout << "Memory: " << options.memory << "MB, seed: " << options.seed << '\n';

        Metrics result;
        double memory = options.memory;
        bool debug = options.debug;
        int range_ratio = options.query == 3 ? 2 : 0;
        switch (options.algorithm) {
            case 0: {
                AceSketch sketch(memory, range_ratio, data.RANGE_MAX_BIT == 32 ? 8 : 6,
                                 data.RANGE_MAX_BIT, 4, 3, debug, seeds.data(), fingerprint_seed, hll_seed);
                result = benchmark(sketch, data, options.query);
                break;
            }
            case 1: {
                omniSkt sketch(memory, range_ratio, data.RANGE_MAX_BIT, data.attr_num,
                               128, 3, debug, seeds.data(), fingerprint_seed, hll_seed);
                result = benchmark(sketch, data, options.query);
                break;
            }
            case 2: {
                hydraSkt sketch(memory, data.attr_num, 3, 3, 64, 1 << 20, debug,
                                seeds.data(), secondary_seeds.data(), fingerprint_seed);
                result = benchmark(sketch, data, options.query);
                break;
            }
            case 3: {
                hll sketch(memory, data.attr_num, debug, fingerprint_seed);
                result = benchmark(sketch, data, options.query);
                break;
            }
        }

        const char* names[] = {"AceSketch", "omniSkt", "hydraSkt", "hll"};
        auto directory = std::filesystem::path("result") /
            (std::to_string(options.dataset) + "_" + std::to_string(options.query)) /
            std::to_string(options.algorithm);
        std::filesystem::create_directories(directory);
        auto path = directory / (std::to_string(options.experiment) + ".txt");
        std::ofstream output(path);
        if (!output) throw std::runtime_error("Cannot write " + path.string());
        output << "Memory: " << memory << "MB\n";
        write_result(output, names[options.algorithm], result);
        output.close();
        if (!output) throw std::runtime_error("Cannot finish writing " + path.string());
        write_result(std::cout, names[options.algorithm], result);
        std::cout << "Result: " << path.string() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
