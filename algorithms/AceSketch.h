#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>
#include <vector>

#include "../utils/hash.h"
#include "../utils/hyperloglog.h"

using namespace std;

struct AceSketch {
    uint32_t FP_SEED = 13331;

    struct Bucket {
        uint8_t* cells;
        uint32_t id;

        void init(int k) {
            cells = new uint8_t[k];
            memset(cells, 0, k * sizeof(uint8_t));
            id = 0;
        }
    };

    double memory, r_memory;
    int bucket_len, r_bucket_len;
    int bucket_d, r_bucket_d;
    int m;
    uint32_t n, base;

    HLL* hll;
    Bucket* buckets;
    Bucket*** r_buckets;
    uint32_t* seeds;

    uint8_t* register_union;
    uint8_t* register_min;
    uint8_t* register_cur;

    AceSketch(double memory_, int range_memo_ratio, int max_bit, int max_range_bit, int b_, int bucket_d_, bool print, uint32_t* seeds_, uint32_t FP_SEED_, uint32_t HLL_SEED_) {
        hll = new HLL(b_, HLL_SEED_);
        m = hll->m;

        double per_bucket_bit = m * 5 + 32;
        if (range_memo_ratio > 0) {
            r_memory = memory_ * range_memo_ratio / 10;
            memory = memory_ - r_memory;
            n = max_bit;
            base = 1 << (max_range_bit / max_bit);
            r_bucket_d = bucket_d_;
            r_bucket_len = (int)ceil(r_memory * 8 * 1024 * 1024 / per_bucket_bit / r_bucket_d / (n + 1));
            r_buckets = new Bucket**[n + 1];
            for (uint32_t i = 0; i <= n; ++i) {
                r_buckets[i] = new Bucket*[r_bucket_d];
                for (int j = 0; j < r_bucket_d; ++j) {
                    r_buckets[i][j] = new Bucket[r_bucket_len];
                    for (int k = 0; k < r_bucket_len; ++k) {
                        r_buckets[i][j][k].init(m);
                    }
                }
            }

            if (print) {
                cout << "r_AceSketch memory: " << r_memory << "MB, r_bucket_len: " << r_bucket_len << ", r_bucket_d: " << r_bucket_d << ", n: " << n << ", base: " << base << endl;
            }
        } else {
            r_memory = 0;
            memory = memory_;
        }
        bucket_d = bucket_d_;
        bucket_len = (int)ceil(memory * 8 * 1024 * 1024 / per_bucket_bit / bucket_d);

        buckets = new Bucket[bucket_d * bucket_len];
        for (int i = 0; i < bucket_d * bucket_len; ++i) {
            buckets[i].init(m);
        }

        seeds = seeds_;
        FP_SEED = FP_SEED_;

        register_union = new uint8_t[m];
        register_min = new uint8_t[m];
        register_cur = new uint8_t[m];

        if (print) {
            cout << "AceSketch memory: " << memory << "MB, bucket_len: " << bucket_len << ", bucket_d: " << bucket_d << ", m: " << m << endl;
            cout << "seeds[0] = " << seeds[0] << ", FP_SEED = " << FP_SEED << ", HLL_SEED = " << HLL_SEED_ << endl;
        }
    }

    ~AceSketch() {
        delete hll;
        for (int i = 0; i < bucket_d * bucket_len; ++i) {
            delete[] buckets[i].cells;
        }
        delete[] buckets;
        if (r_memory > 0) {
            for (uint32_t i = 0; i <= n; ++i) {
                for (int j = 0; j < r_bucket_d; ++j) {
                    for (int k = 0; k < r_bucket_len; ++k) {
                        delete[] r_buckets[i][j][k].cells;
                    }
                    delete[] r_buckets[i][j];
                }
                delete[] r_buckets[i];
            }
            delete[] r_buckets;
        }
        delete[] register_union;
        delete[] register_min;
        delete[] register_cur;
    }

    void insert(string* data, int attr_num) {
        const string& element = data[attr_num - 1];

        auto [idx, lz] = hll->insert(element);
        uint32_t y = 1u << lz;

        for (int pid = 0; pid < attr_num - 1; ++pid) {
            const string& predicate = data[pid];
            if (predicate.empty()) {
                continue;
            }

            uint32_t phash = H(predicate, FP_SEED);

            for (int i = 0; i < bucket_d; ++i) {
                uint32_t index = H(phash ^ seeds[i]) % bucket_len;
                Bucket& bucket = buckets[i * bucket_len + index];
                if (bucket.cells[idx] > lz) {
                    continue;
                }
                if (bucket.id == 0 or bucket.id == phash) {
                    bucket.cells[idx] = lz;
                    bucket.id = phash;
                } else {
                    if (static_cast<uint64_t>(rand()) * y < RAND_MAX) {
                        if (bucket.cells[idx] >= 1) {
                            bucket.cells[idx]--;
                        }
                        if (bucket.cells[idx] == 0) {
                            bucket.id = phash;
                            bucket.cells[idx] = lz;
                        }
                    }
                }
            }
        }
    }

    vector<pair<uint32_t, uint32_t>> point_dyadic_ranges(uint32_t value, int base = 2) {
        vector<pair<uint32_t, uint32_t>> ranges;
        uint32_t max_exponent = n;
        uint64_t max_r = 1;
        for (uint32_t i = 0; i < max_exponent; ++i) {
            max_r *= base;
        }
        uint64_t range_size = 1;
        for (uint32_t p = 0; p <= max_exponent; ++p) {
            uint32_t start_x = (value - 1) / range_size;
            uint64_t l = start_x * range_size + 1;
            uint64_t r = (start_x + 1) * range_size;
            if (l <= value && value <= r && r <= max_r) {
                ranges.push_back(make_pair(start_x, p));
            }
            range_size *= base;
        }
        return ranges;
    }

    vector<pair<uint32_t, uint32_t>> cover_dyadic_ranges(uint32_t l, uint32_t r, int base = 2) {
        vector<pair<uint32_t, uint32_t>> intervals;
        uint64_t cursor = l;
        while (cursor <= r) {
            uint32_t p = 0;
            uint64_t current_power = base;
            while (cursor % current_power == 1 && cursor + current_power - 1 <= r) {
                p += 1;
                current_power *= base;
            }
            uint64_t length = 1;
            for (uint32_t i = 0; i < p; ++i) {
                length *= base;
            }
            uint32_t x = (cursor - 1) / length;
            intervals.push_back(make_pair(x, p));
            cursor += length;
        }
        return intervals;
    }

    void _range_insert(uint32_t range, uint32_t idx, uint8_t lz) {
        vector<pair<uint32_t, uint32_t>> ranges = point_dyadic_ranges(range, base);
        uint32_t y = 1u << lz;
        for (auto& [start_x, k] : ranges) {
            for (int i = 0; i < r_bucket_d; ++i) {
                uint32_t index = H(start_x ^ seeds[i]) % r_bucket_len;
                Bucket& bucket = r_buckets[k][i][index];
                if (bucket.id == 0 or bucket.id == start_x) {
                    bucket.cells[idx] = max(bucket.cells[idx], lz);
                    bucket.id = start_x;
                } else {
                    uint32_t x = 1u << bucket.cells[idx];
                    if (x <= y) {
                        if (static_cast<uint64_t>(rand()) * y < RAND_MAX) {
                            if (bucket.cells[idx] >= 1) {
                                bucket.cells[idx]--;
                            }
                            if (bucket.cells[idx] == 0) {
                                bucket.id = start_x;
                                bucket.cells[idx] = lz;
                            }
                        }
                    }
                }
            }
        }
    }

    void range_insert(string* data, int attr_num, int range_attr_idx) {
        const string& element = data[attr_num - 1];

        auto [idx, lz] = hll->insert(element);

        for (int pid = 0; pid < attr_num - 1; ++pid) {
            if (data[pid].empty()) {
                continue;
            }

            if (pid == range_attr_idx) {
                _range_insert(stoul(data[pid]), idx, lz);
                continue;
            }

            uint32_t phash = H(data[pid], FP_SEED);
            for (int i = 0; i < bucket_d; ++i) {
                uint32_t index = H(phash ^ seeds[i]) % bucket_len;
                Bucket& bucket = buckets[i * bucket_len + index];
                if (bucket.id == 0 or bucket.id == phash) {
                    bucket.cells[idx] = max(bucket.cells[idx], lz);
                    bucket.id = phash;
                } else {
                    uint32_t x = 1u << bucket.cells[idx];
                    uint32_t y = 1u << lz;
                    if (x <= y) {
                        if (static_cast<uint64_t>(rand()) * y < RAND_MAX) {
                            if (bucket.cells[idx] >= 1) {
                                bucket.cells[idx]--;
                            }
                            if (bucket.cells[idx] == 0) {
                                bucket.id = phash;
                                bucket.cells[idx] = lz;
                            }
                        }
                    }
                }
            }
        }
    }

    template <class Locate>
    const uint8_t* read_registers(uint32_t fingerprint, int rows, Locate locate) {
        memset(register_min, 255, m);
        memset(register_cur, 255, m);
        int matches = 0;
        for (int row = 0; row < rows; ++row) {
            Bucket& bucket = locate(row);
            bool matched = bucket.id == fingerprint;
            matches += matched;
            for (int j = 0; j < m; ++j) {
                register_min[j] = min(register_min[j], bucket.cells[j]);
                if (matched) register_cur[j] = min(register_cur[j], bucket.cells[j]);
            }
        }
        // Retain the original fallback when at most one row matches the fingerprint.
        return matches <= 1 ? register_min : register_cur;
    }

    const uint8_t* predicate_registers(uint32_t fingerprint) {
        return read_registers(fingerprint, bucket_d, [&](int row) -> Bucket& {
            uint32_t index = H(fingerprint ^ seeds[row]) % bucket_len;
            return buckets[row * bucket_len + index];
        });
    }

    double query_union(string* predicates, int*, int count) {
        memset(register_union, 0, m);
        for (int i = 0; i < count; ++i) {
            const uint8_t* registers = predicate_registers(H(predicates[i], FP_SEED));
            for (int j = 0; j < m; ++j) {
                register_union[j] = max(register_union[j], registers[j]);
            }
        }
        return round(hll->query(register_union));
    }

    double query_intersection(string* predicates, int*, int count) {
        vector<vector<uint8_t>> registers(count, vector<uint8_t>(m));
        vector<uint8_t*> pointers(count);
        for (int i = 0; i < count; ++i) {
            const uint8_t* values = predicate_registers(H(predicates[i], FP_SEED));
            copy(values, values + m, registers[i].begin());
            pointers[i] = registers[i].data();
        }
        return inclusion_exclusion(pointers.data(), count, m);
    }

    double inclusion_exclusion(uint8_t** regs, int n, int m) {
        uint8_t* min_reg = new uint8_t[m];
        std::memset(min_reg, 255, m * sizeof(uint8_t));
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < m; ++j) {
                min_reg[j] = std::min(min_reg[j], regs[i][j]);
            }
        }
        double min_card = hll->query(min_reg);
        delete[] min_reg;

        double sum_k1 = 0;
        for (int i = 0; i < n; ++i) {
            sum_k1 += hll->query(regs[i]);
        }
        uint8_t* union_reg = new uint8_t[m];
        double sum_k2 = 0;
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                memset(union_reg, 0, m * sizeof(uint8_t));
                for (int k = 0; k < m; ++k) {
                    union_reg[k] = std::max(regs[i][k], regs[j][k]);
                }
                sum_k2 += hll->query(union_reg);
            }
        }
        delete[] union_reg;

        double max_card = max(0.0, sum_k1 - sum_k2);

        return max(0.0, (min_card + max_card) / 2);
    }

    double query_range(uint32_t left, uint32_t right) {
        memset(register_union, 0, m);
        for (const auto& range : cover_dyadic_ranges(left, right, base)) {
            uint32_t start = range.first, level = range.second;
            const uint8_t* registers = read_registers(start, r_bucket_d, [&](int row) -> Bucket& {
                uint32_t index = H(start ^ seeds[row]) % r_bucket_len;
                return r_buckets[level][row][index];
            });
            for (int j = 0; j < m; ++j) {
                register_union[j] = max(register_union[j], registers[j]);
            }
        }
        return round(hll->query(register_union));
    }
};
