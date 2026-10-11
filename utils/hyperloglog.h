#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

#include "hash.h"

struct HLL {
    int m, b;
    uint32_t HASH_SEED;

    HLL(int bits, uint32_t seed) : m(1 << bits), b(bits), HASH_SEED(seed) {}

    static double getAlpha(int registers) {
        if (registers == 16) return 0.673;
        if (registers == 32) return 0.697;
        if (registers == 64) return 0.709;
        return 0.7213 / (1 + 1.079 / registers);
    }

    std::pair<uint32_t, uint8_t> insert(const std::string& element) const {
        uint32_t hash = H(element, HASH_SEED);
        uint32_t index = hash >> (32 - b);
        uint32_t remaining = hash & ((1u << (32 - b)) - 1);
        uint8_t rank = remaining ? __builtin_clz(remaining) - b + 1 : 32 - b;
        return {index, rank};
    }

    double query(const uint8_t* registers) const {
        double sum = 0;
        int zeros = 0;
        for (int i = 0; i < m; ++i) {
            sum += std::ldexp(1.0, -registers[i]);
            zeros += registers[i] == 0;
        }
        double estimate = getAlpha(m) * m * m * (1.0 / sum);
        if (estimate <= 2.5 * m && zeros) {
            estimate = m * std::log(double(m) / zeros);
        }
        return estimate < 1 ? 0 : estimate;
    }
};
