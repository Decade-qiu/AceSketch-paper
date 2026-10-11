#pragma once

#include <stdexcept>
#include <unordered_map>
#include <vector>

struct SubPopMap {
    std::unordered_map<int, std::vector<int>> subPopulations2;

    SubPopMap() = default;

    explicit SubPopMap(int attributes) {
        int next_id = 0;
        generate({}, 0, attributes, next_id);
        subPopulations2.erase(0);
    }

    void generate(std::vector<int> current, int start, int attributes, int& next_id) {
        subPopulations2[next_id++] = current;
        for (int i = start; i < attributes; ++i) {
            current.push_back(i);
            generate(current, i + 1, attributes, next_id);
            current.pop_back();
        }
    }

    int getSubPop(const std::vector<int>& attributes) const {
        for (const auto& entry : subPopulations2) {
            if (entry.second == attributes) return entry.first;
        }
        throw std::runtime_error("Unknown predicate combination");
    }
};
