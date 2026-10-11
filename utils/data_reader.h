#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Query {
    std::vector<std::string> predicates;
    std::vector<int> indices;
    uint32_t left = 0, right = 0;
    double truth = 0;
};

struct DataReader {
    int attr_num, RANGE_ATTR_IDX, RANGE_MAX_BIT;
    std::vector<std::vector<std::string>> data_stream;
    std::vector<Query> query_stream;

    static uint64_t integer(const std::string& value) {
        if (value.empty() || !std::all_of(value.begin(), value.end(),
            [](unsigned char c) { return std::isdigit(c); })) {
            throw std::runtime_error("Expected a nonnegative integer: " + value);
        }
        return std::stoull(value);
    }

    DataReader(int dataset, int query_type, const std::filesystem::path& root,
               const std::string& data_file = "", const std::string& query_file = "") {
        const char* names[] = {"caida", "ecb", "criteo"};
        const char* files[] = {"data_unique/caida_2019_5.txt",
                               "data_ecb/2019-Nov-upd.txt",
                               "data_criteo/day_0_compressed.txt"};
        const char* queries[] = {"single", "union", "intersection", "range"};
        const int counts[] = {5, 8, 17};
        attr_num = counts[dataset];
        RANGE_ATTR_IDX = dataset == 0 ? 3 : 6;
        RANGE_MAX_BIT = dataset == 0 ? 32 : 12;

        auto data_path = data_file.empty() ? root / files[dataset]
                                          : std::filesystem::path(data_file);
        auto query_path = query_file.empty()
            ? root / "APCE_data" / (std::string(names[dataset]) + "_" +
                queries[query_type] + "_query_result.bak.txt")
            : std::filesystem::path(query_file);

        read_lines(data_path, [&](std::vector<std::string> fields) {
            if (fields.size() != std::size_t(attr_num + (dataset != 0))) {
                throw std::runtime_error("Unexpected number of data columns");
            }
            if (dataset == 0) {
                std::reverse(fields.begin(), fields.end());
            } else if (dataset == 1) {
                fields.pop_back();
                for (auto& field : fields) if (field == "NULL") field.clear();
            } else {
                fields.erase(fields.begin());
                std::rotate(fields.begin(), fields.begin() + 1, fields.end());
            }
            if (fields.back().empty()) throw std::runtime_error("Empty distinct-count key");
            if (query_type == 3 && !fields[RANGE_ATTR_IDX].empty()) {
                check_range(integer(fields[RANGE_ATTR_IDX]));
            }
            for (int i = 0; i < attr_num - 1; ++i) {
                if (!fields[i].empty()) fields[i] += "," + std::to_string(i);
            }
            data_stream.push_back(std::move(fields));
        });

        read_lines(query_path, [&](std::vector<std::string> fields) {
            if (fields.size() < 2) throw std::runtime_error("Query needs predicates and truth");
            Query query;
            query.truth = static_cast<double>(integer(fields.back()));
            fields.pop_back();
            if (query_type == 3) {
                if (fields.size() != 1) throw std::runtime_error("Range query needs left,right");
                auto comma = fields[0].find(',');
                if (comma == std::string::npos) throw std::runtime_error("Missing range separator");
                auto left = integer(fields[0].substr(0, comma));
                auto right = integer(fields[0].substr(comma + 1));
                check_range(left);
                check_range(right);
                if (left > right) throw std::runtime_error("Reversed query range");
                query.left = static_cast<uint32_t>(left);
                query.right = static_cast<uint32_t>(right);
            } else {
                if (query_type == 0 && fields.size() != 1) {
                    throw std::runtime_error("Single query needs one predicate");
                }
                // Combination baselines use ascending attribute indices in their keys.
                std::vector<std::pair<int, std::string>> predicates;
                for (const auto& field : fields) {
                    auto comma = field.rfind(',');
                    if (comma == std::string::npos || comma == 0) {
                        throw std::runtime_error("Expected value,attribute_index");
                    }
                    auto index = integer(field.substr(comma + 1));
                    if (index >= uint64_t(attr_num - 1)) throw std::runtime_error("Invalid attribute index");
                    predicates.emplace_back(static_cast<int>(index), field);
                }
                std::sort(predicates.begin(), predicates.end());
                for (const auto& predicate : predicates) {
                    query.indices.push_back(predicate.first);
                    query.predicates.push_back(predicate.second);
                }
                if (query_type == 2 && std::adjacent_find(query.indices.begin(), query.indices.end())
                                      != query.indices.end()) {
                    throw std::runtime_error("AND queries require distinct attribute indices");
                }
            }
            query_stream.push_back(std::move(query));
        });
    }

    void check_range(uint64_t value) const {
        if (value == 0 || value >= (uint64_t{1} << RANGE_MAX_BIT)) {
            throw std::runtime_error("Range value is outside the positive attribute domain");
        }
    }

    template <class Consume>
    static void read_lines(const std::filesystem::path& path, Consume consume) {
        std::ifstream input(path);
        if (!input) throw std::runtime_error("Cannot open " + path.string());
        std::string line, field;
        std::size_t count = 0, line_number = 0;
        while (std::getline(input, line)) {
            ++line_number;
            std::istringstream stream(line);
            std::vector<std::string> fields;
            while (stream >> field) fields.push_back(field);
            if (fields.empty()) continue;
            try {
                consume(std::move(fields));
            } catch (const std::exception& error) {
                throw std::runtime_error(path.string() + ":" + std::to_string(line_number) +
                                         ": " + error.what());
            }
            ++count;
        }
        if (input.bad()) throw std::runtime_error("Cannot read " + path.string());
        if (!count) throw std::runtime_error("Empty input: " + path.string());
        std::cout << "Loaded " << count << " rows from " << path.string() << '\n';
    }
};
