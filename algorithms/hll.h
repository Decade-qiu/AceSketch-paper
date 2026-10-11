#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <iostream>
#include <random>
#include <unordered_map>
#include <vector>

#include "../utils/hash.h"
#include "../utils/subpop.h"

using namespace std;

struct vHLL_Single {
    uint8_t* M;
    uint32_t m;
    uint32_t s;
    uint32_t log2s;
    double m_alpha;
    double s_alpha;
    uint32_t ELE_SEED;

    std::vector<uint32_t> virtual_seeds;

    double cached_total_cardinality;
    bool total_cardinality_computed;

    vHLL_Single()
        : M(nullptr), m(0), s(0), log2s(0),
          m_alpha(0), s_alpha(0), ELE_SEED(13331),
          cached_total_cardinality(0),
          total_cardinality_computed(false) {}

    void init(double memory_mb, uint32_t seed) {
        s = 16;
        log2s = 4;

        ELE_SEED = seed;
        total_cardinality_computed = false;
        std::mt19937 rng(ELE_SEED);
        std::uniform_int_distribution<uint32_t> dist(0, UINT32_MAX);
        virtual_seeds.resize(s);
        for (auto& sd : virtual_seeds) {
            sd = dist(rng);
        }
        double total_bits = memory_mb * 8 * 1024 * 1024;
        m = static_cast<uint32_t>(total_bits / 5.0);
        if (m <= s) throw std::runtime_error("vHLL requires more physical than virtual registers");

        M = new uint8_t[m];
        memset(M, 0, m * sizeof(uint8_t));

        m_alpha = alpha(m);
        s_alpha = alpha(s);
    }

    ~vHLL_Single() {
        if (M) {
            delete[] M;
            M = nullptr;
        }
    }
    double alpha(uint32_t x) {
        switch (x) {
            case 16: return 0.673;
            case 32: return 0.697;
            case 64: return 0.709;
            default: return 0.7213 / (1.0 + 1.079 / x);
        }
    }
    inline uint8_t rho(uint32_t x) {
        uint32_t val = x >> log2s;
        if (val == 0) {
            return static_cast<uint8_t>(32 - log2s + 1);
        }
        int clz = __builtin_clz(val);
        return static_cast<uint8_t>(clz - static_cast<int>(log2s) + 1);
    }
    void insert(const std::string& f, const std::string& e) {
        uint32_t he = H(e, ELE_SEED);
        uint32_t p = he & (s - 1);
        uint8_t r = rho(he);
        uint32_t index = H(f, virtual_seeds[p]) % m;

        if (r > M[index]) {
            M[index] = r;
            total_cardinality_computed = false;
        }
    }
    double estimate(const std::string& f) {
        double sum_inv = 0.0;
        uint32_t num_zeros = 0;
        for (uint32_t i = 0; i < s; ++i) {
            uint32_t index = H(f, virtual_seeds[i]) % m;
            uint8_t val = M[index];
            sum_inv += 1.0 / (1ULL << val);
            if (val == 0) ++num_zeros;
        }

        double s_d = static_cast<double>(s);
        double raw_ns = s_alpha * s_d * s_d / sum_inv;
        if (num_zeros > 0 && raw_ns < 2.5 * s_d) {
            raw_ns = -s_d * std::log(static_cast<double>(num_zeros) / s_d);
        }

        double total = get_total_cardinality();
        double m_d = static_cast<double>(m);
        double nf = m_d * s_d / (m_d - s_d) * (raw_ns / s_d - total / m_d);
        return std::max(1.0, nf);
    }
    double get_total_cardinality() {
        if (total_cardinality_computed) {
            return cached_total_cardinality;
        }

        double sum_inv = 0.0;
        uint32_t num_zeros = 0;

        for (uint32_t i = 0; i < m; ++i) {
            uint8_t val = M[i];
            sum_inv += 1.0 / (1ULL << val);
            if (val == 0) ++num_zeros;
        }

        double m_d = static_cast<double>(m);
        double raw = m_alpha * m_d * m_d / sum_inv;
        if (num_zeros > 0 && raw < 2.5 * m_d) {
            raw = -m_d * std::log(static_cast<double>(num_zeros) / m_d);
        }

        cached_total_cardinality = raw;
        total_cardinality_computed = true;
        return cached_total_cardinality;
    }
};

struct hll {

    double memory;
    int attr_num;

    SubPopMap subpop;
    unordered_map<int, vHLL_Single> vhll_instances;

    hll(double memory_, int attr_num_, bool print, uint32_t ELE_SEED_) {
        attr_num = attr_num_ - 1;
        memory = memory_;

        subpop = SubPopMap(attr_num);

        int num_subpops = subpop.subPopulations2.size();
        double per_subpop_memory = memory / num_subpops;

        for (auto& [subpop_id, attrs] : subpop.subPopulations2) {
            vhll_instances[subpop_id].init(per_subpop_memory, ELE_SEED_ + subpop_id * 9973);
        }

        if (print) {
            cout << "hll(vHLL for combinations) memory: " << memory << "MB, attr_num: " << attr_num
                 << ", num_subpops: " << num_subpops << ", per_subpop_memory: " << per_subpop_memory << "MB" << endl;

        }
    }

    void insert(string* data, int total_attr_num) {
        const string& element = data[total_attr_num - 1];
        for (auto& [subpop_id, attrs] : subpop.subPopulations2) {
            string subpop_element = to_string(subpop_id) + "-";
            for (int attr : attrs) {
                subpop_element.append(data[attr] + "-");
            }
            vhll_instances[subpop_id].insert(subpop_element, element);
        }
    }

    double query_intersection(string* predicate_ins, int* predicate_idx, int attr_num_) {
        vector<int> attr_id;
        string predicate = "";
        for (int i = 0; i < attr_num_; i++) {
            attr_id.push_back(predicate_idx[i]);
            predicate.append(predicate_ins[i] + "-");
        }
        int subpop_id = subpop.getSubPop(attr_id);
        string subpop_element = to_string(subpop_id) + "-" + predicate;
        return vhll_instances[subpop_id].estimate(subpop_element);
    }

    double query_union(string* predicates, int* indices, int count) {
        return query_intersection(predicates, indices, count);
    }

    void range_insert(string*, int, int) {
        throw std::runtime_error("Range queries are not supported by this baseline");
    }

    double query_range(uint32_t, uint32_t) {
        throw std::runtime_error("Range queries are not supported by this baseline");
    }
};
