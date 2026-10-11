#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <cstdint>
#include <iostream>
#include <vector>
#include <stdexcept>

#include "../utils/hash.h"
#include "../utils/subpop.h"

using namespace std;

struct hydraSkt {

    struct Bucket {
        int* cs;
        uint8_t* bitmap;
        int len;
        int row;
        int col;
        uint32_t ELE_SEED;

        void init(int row_, int col_, int bitmap_size, uint32_t ELE_SEED_) {
            row = row_;
            col = col_;
            ELE_SEED = ELE_SEED_;
            len = bitmap_size;
            cs = new int[row * col];
            memset(cs, 0, row * col * sizeof(int));
            bitmap = new uint8_t[bitmap_size];
            memset(bitmap, 0, bitmap_size * sizeof(uint8_t));
        }

        void add(const int* pos, const int* sign, const string& element) {
            uint32_t index = H(element, ELE_SEED) % len;
            if (bitmap[index] == 1) {
                return;
            }
            bitmap[index] = 1;
            for (int r = 0; r < row; r++) {
                cs[r * col + pos[r]] += sign[r];
            }
        }

        int inspectValue(const int* pos, const int* sign) const {
            vector<int> counters(row);
            for (int r = 0; r < row; ++r) {
                counters[r] = sign[r] * cs[r * col + pos[r]];
            }
            sort(counters.begin(), counters.end());
            return max(1, counters[row / 2]);
        }

    };

    double memory;
    int bucket_len, w;
    int bucket_d, d;
    int bitmap_size;

    SubPopMap subpop;

    Bucket** buckets;
    uint32_t* seeds;
    uint32_t* seeds_b;

    hydraSkt(double memory_, int attr_num, int bucket_d_, int d_, int w_, int bitmap_size_, bool print, uint32_t* seeds_, uint32_t* seeds_b_, uint32_t ELE_SEED_) {

        d = d_;
        w = w_;
        bitmap_size = bitmap_size_;

        subpop = SubPopMap(attr_num - 1);

        memory = memory_;

        double per_bucket_bit = d * w * 32 + bitmap_size;

        bucket_d = bucket_d_;
        bucket_len = (int)ceil(memory * 8 * 1024 * 1024 / per_bucket_bit / bucket_d);

        buckets = new Bucket*[bucket_d];
        for (int i = 0; i < bucket_d; ++i) {
            buckets[i] = new Bucket[bucket_len];
            for (int j = 0; j < bucket_len; ++j) {
                buckets[i][j].init(d, w, bitmap_size, ELE_SEED_);
            }
        }

        seeds = seeds_;
        seeds_b = seeds_b_;

        if (print) {
            cout << "HYDRA memory: " << memory << "MB, bucket_len: " << bucket_len << ", bucket_d: " << bucket_d << " " << "w, d: " << w << ", " << d << ", bitmap_size: " << bitmap_size << endl;
            cout << "seeds[0] = " << seeds[0] << ", seeds_b[0] = " << seeds_b[0] << endl;
        }
    }

    ~hydraSkt() {
        for (int i = 0; i < bucket_d; ++i) {
            for (int j = 0; j < bucket_len; ++j) {
                delete[] buckets[i][j].cs;
                delete[] buckets[i][j].bitmap;
            }
            delete[] buckets[i];
        }
        delete[] buckets;
    }

    void update(const string& predicate, const string& element) {
        int* pos = new int[d];
        int* sign = new int[d];
        for (int i = 0; i < d; i++) {
            uint32_t hash_v = H(predicate, seeds[i]);
            pos[i] = hash_v % w;
            sign[i] = 2 * ((hash_v >> 31) & 1) - 1;
        }

        for (int i = 0;i < bucket_d; i++) {
            uint32_t hash_v = H(predicate, seeds_b[i]);
            uint32_t index = hash_v % bucket_len;
            buckets[i][index].add(pos, sign, element);
        }

        delete[] pos;
        delete[] sign;
    }

    double query(const string& predicate) {
        int* pos = new int[d];
        int* sign = new int[d];
        for (int i = 0; i < d; i++) {
            uint32_t hash_v = H(predicate, seeds[i]);
            pos[i] = hash_v % w;
            sign[i] = 2 * ((hash_v >> 31) & 1) - 1;
        }

        vector<int> res;
        for (int i = 0; i < bucket_d; i++) {
            uint32_t hash_v = H(predicate, seeds_b[i]);
            uint32_t index = hash_v % bucket_len;
            int v = buckets[i][index].inspectValue(pos, sign);
            res.push_back(v);
        }

        delete[] pos;
        delete[] sign;
        sort(res.begin(), res.end());
        int mid = res.size() / 2;
        double maxv = res[mid];
        return maxv;
    }

    void insert(string* data, int attr_num) {
        const string& element = data[attr_num - 1];

        for (auto& [subpop_id, attrs] : subpop.subPopulations2) {
            string subpop_element = to_string(subpop_id) + "-";
            for (int attr : attrs) {
                subpop_element.append(data[attr]+"-");
            }
            update(subpop_element, element);
        }
    }

    double query_intersection(string* predicate_ins, int* predicate_idx, int attr_num) {
        vector<int> attr_id;
        string predicate = "";
        for (int i = 0; i < attr_num; i++) {
            attr_id.push_back(predicate_idx[i]);
            predicate.append(predicate_ins[i]+"-");
        }
        int subpop_id = subpop.getSubPop(attr_id);
        string subpop_element = to_string(subpop_id) + "-" + predicate;
        return query(subpop_element);
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
