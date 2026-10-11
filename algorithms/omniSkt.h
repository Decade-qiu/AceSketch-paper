#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <cstdint>
#include <iostream>
#include <set>
#include <unordered_set>
#include <vector>
#include <utility>

#include "../utils/hash.h"
#include "../utils/max_heap.h"

using namespace std;

struct omniSkt {
    static constexpr int nbits = 1 << 17;
    uint32_t FP_SIZE = 16;
    uint32_t FP_MASK = (1 << 16) - 1;
    uint32_t FP_SEED = 13331;
    uint32_t ELE_SEED = 13331;

    struct Bucket {
        MaxHeap* heap;
        uint8_t* bitmaps;
        uint32_t cnt;

        void init(int k) {
            heap = new MaxHeap(k);
            cnt = 0;
            bitmaps = new uint8_t[nbits]{};
        }
    };

    double memory, r_memory;
    int bucket_len, r_bucket_len;
    int bucket_d, r_bucket_d;
    int attrs;
    int k;
    int n;

    Bucket*** buckets;
    Bucket** r_buckets;
    uint32_t* seeds;

    omniSkt(double memory_, int range_memo_ratio, int max_bit, int attrs_, int k_, int bucket_d_, bool print, uint32_t* seeds_, uint32_t FP_SEED_, uint32_t HLL_SEED_) {
        k = k_;
        FP_SIZE = 32;
        FP_MASK = UINT32_MAX;
        double per_bucket_bit = k * FP_SIZE + 32 + nbits;

        if (range_memo_ratio > 0) {
            r_memory = memory_ * range_memo_ratio / 10;
            memory = memory_ - r_memory;
            n = max_bit + 1;
            r_bucket_d = bucket_d_;
            r_bucket_len = (int)ceil(r_memory * 8 * 1024 * 1024 / per_bucket_bit / r_bucket_d);
            r_buckets = new Bucket*[r_bucket_d];
            for (int i = 0; i < r_bucket_d; ++i) {
                r_buckets[i] = new Bucket[r_bucket_len];
                for (int j = 0; j < r_bucket_len; ++j) {
                    r_buckets[i][j].init(k);
                }
            }

            if (print) {
                cout << "r_omni memory: " << r_memory << "MB, r_bucket_len: " << r_bucket_len << ", r_bucket_d: " << r_bucket_d << ", n: " << n << endl;
            }
        } else {
            r_memory = 0;
            memory = memory_;
        }

        attrs = attrs_ - 1;
        bucket_d = bucket_d_;
        bucket_len = (int)ceil(memory * 8 * 1024 * 1024 / attrs / per_bucket_bit / bucket_d);

        buckets = new Bucket**[attrs];
        for (int i = 0; i < attrs; ++i) {
            buckets[i] = new Bucket*[bucket_d];
            for (int j = 0; j < bucket_d; ++j) {
                buckets[i][j] = new Bucket[bucket_len];
                for (int _ = 0; _ < bucket_len; ++_) {
                    buckets[i][j][_].init(k);
                }
            }
        }

        seeds = seeds_;
        FP_SEED = FP_SEED_;
        ELE_SEED = HLL_SEED_;

        if (print) {
            cout << "OMNI memory: " << memory << "MB, bucket_len: " << bucket_len << ", bucket_d: " << bucket_d << ", k: " << k << endl;
            cout << "seeds[0] = " << seeds[0] << ", FP_SEED = " << FP_SEED << ", ELE_SEED = " << ELE_SEED << endl;
        }
    }

    ~omniSkt() {
        for (int i = 0; i < attrs; ++i) {
            for (int j = 0; j < bucket_d; ++j) {
                for (int k = 0; k < bucket_len; ++k) {
                    delete buckets[i][j][k].heap;
                    delete[] buckets[i][j][k].bitmaps;
                }
                delete[] buckets[i][j];
            }
            delete[] buckets[i];
        }
        delete[] buckets;
        if (r_memory > 0) {
            for (int i = 0; i < r_bucket_d; ++i) {
                for (int j = 0; j < r_bucket_len; ++j) {
                    delete r_buckets[i][j].heap;
                    delete[] r_buckets[i][j].bitmaps;
                }
                delete[] r_buckets[i];
            }
            delete[] r_buckets;
        }
    }

    void insert(string* data, int attr_num) {
        const string& element = data[attr_num - 1];

        uint32_t ehash = H(element, ELE_SEED) % FP_MASK;

        ehash += (ehash == 0);

        for (int pid = 0; pid < attr_num - 1; ++pid) {
            const string& predicate = data[pid];
            if (predicate.empty()) {
                continue;
            }

            uint32_t phash = H(predicate, FP_SEED);

            for (int i = 0; i < bucket_d; ++i) {
                uint32_t index = H(phash ^ seeds[i]) % bucket_len;

                Bucket& bucket = buckets[pid][i][index];

                bucket.heap->Insert(ehash);
                if (bucket.bitmaps[ehash % nbits] == 0) {
                    bucket.cnt++;
                    bucket.bitmaps[ehash % nbits] = 1;
                }
            }
        }
    }

    vector<pair<uint64_t, uint64_t>> wrapperInitLogRanges(uint32_t inputKey) {
        vector<uint64_t> lowerBound(n);
        vector<uint64_t> upperBound(n);
        vector<uint64_t> coeff(n);

        uint64_t halfPoint = (uint64_t)pow(2, n - 1);
        if (inputKey < halfPoint) {
            coeff[0] = 0;
            lowerBound[0] = 1;
            upperBound[0] = halfPoint;
        } else {
            coeff[0] = 1;
            lowerBound[0] = halfPoint;
            upperBound[0] = halfPoint * 2;
        }

        uint64_t powVal = halfPoint;
        for (int i = 1; i < n - 1; i++) {
            uint64_t prevCoeff = coeff[i - 1];
            uint64_t newCoeffLower = prevCoeff * 2;
            powVal /= 2;
            if ((newCoeffLower + 1) * powVal < inputKey) {
                newCoeffLower++;
                coeff[i] = newCoeffLower;
            } else {
                coeff[i] = newCoeffLower;
            }
            lowerBound[i] = coeff[i] * powVal + 1;
            upperBound[i] = (coeff[i] + 1) * powVal;
        }
        lowerBound[n - 1] = inputKey;
        upperBound[n - 1] = inputKey;

        vector<pair<uint64_t, uint64_t>> result;
        for (int i = 0; i < n; i++) {
            lowerBound[i] -= 1;
            upperBound[i] -= 1;
            result.push_back({lowerBound[i], upperBound[i]});
        }
        return result;
    }

    vector<pair<uint64_t, uint64_t>> wrapLogRanges(uint32_t low, uint32_t up) {
        uint64_t startInclusive = low - 1;
        uint64_t stopInclusive = up - 1;

        vector<pair<uint64_t, uint64_t>> result;
        uint64_t powVal = 1;

        for (int j = 0; j <= n - 1; j++) {
            if (startInclusive + powVal - 1 > stopInclusive)
                break;
            else if ((startInclusive % (powVal * 2) == 0) && (startInclusive + powVal - 1 <= stopInclusive)) {
            } else {
                result.push_back({1 + startInclusive, 1 + startInclusive + powVal - 1});
                startInclusive += powVal;
            }
            powVal *= 2;
        }

        powVal = (uint64_t)1 << n;
        for (int j = n; j >= 0; j--) {
            if (startInclusive % powVal == 0 && startInclusive + powVal - 1 <= stopInclusive) {
                result.push_back({1 + startInclusive, 1 + startInclusive + powVal - 1});
                startInclusive += powVal;
            }
            powVal /= 2;
        }

        for (auto& p : result) {
            p.first -= 1;
            p.second -= 1;
        }

        return result;
    }

    void _range_insert(uint32_t range, uint32_t e) {
        vector<pair<uint64_t, uint64_t>> ranges = wrapperInitLogRanges(range);

        for (auto& [l, r] : ranges) {
            uint64_t predicate = (l << 32) | r;

            uint32_t phash = H(predicate, FP_SEED);

            for (int i = 0; i < r_bucket_d; ++i) {
                uint32_t index = H(phash ^ seeds[i]) % r_bucket_len;

                Bucket& bucket = r_buckets[i][index];
                bucket.cnt++;
                bucket.heap->Insert(e);
            }
        }
    }

    void range_insert(string* data, int attr_num, int range_attr_idx) {
        const string& element = data[attr_num - 1];

        uint32_t ehash = H(element, ELE_SEED) % FP_MASK;

        ehash += (ehash == 0);

        for (int pid = 0; pid < attr_num - 1; ++pid) {
            const string& predicate = data[pid];
            if (predicate.empty()) {
                continue;
            }

            if (pid == range_attr_idx) {
                _range_insert(stoul(data[pid]), ehash);
                continue;
            }

            uint32_t phash = H(predicate, FP_SEED);

            for (int i = 0; i < bucket_d; ++i) {
                uint32_t index = H(phash ^ seeds[i]) % bucket_len;

                Bucket& bucket = buckets[pid][i][index];
                bucket.cnt++;
                bucket.heap->Insert(ehash);
            }
        }
    }

    int getAltEstKMV(const vector<set<uint32_t>>& samples) {
        if (samples.empty()) return 0;
        int c = 0;
        auto iter0 = samples[0].begin();

        while (iter0 != samples[0].end()) {
            uint32_t curVal = *iter0;
            bool found = true;
            for (size_t j = 1; j < samples.size(); j++) {
                auto it = samples[j].lower_bound(curVal);
                if (it == samples[j].end()) {
                    return c;
                } else if (*it == curVal) {
                    continue;
                } else {
                    iter0 = samples[0].lower_bound(*it);
                    found = false;
                    break;
                }
            }
            if (found) {
                c++;
                ++iter0;
            }
        }
        return c;
    }

    double query_intersection(string* predicate_ins, int* predicate_idx, int attr_num) {
        vector<set<uint32_t>> samples;
        double nmax = 0;

        for (int _pid = 0; _pid < attr_num; ++_pid) {
            const string& predicate = predicate_ins[_pid];
            if (predicate.empty()) {
                continue;
            }
            uint32_t phash = H(predicate, FP_SEED);

            int pid = predicate_idx[_pid];

            for (int i = 0; i < bucket_d; ++i) {
                uint32_t index = H(phash ^ seeds[i]) % bucket_len;

                Bucket& bucket = buckets[pid][i][index];

                nmax = max(nmax, (double)bucket.cnt);
                set<uint32_t> sampleSet;
                for (int j = 0; j < bucket.heap->CurrentSize; j++) {
                    sampleSet.insert(bucket.heap->data_set[j]);
                }
                samples.push_back(std::move(sampleSet));
            }
        }

        int c = getAltEstKMV(samples);
        double ans = nmax / k * c;
        return ans;
    }

    double query_union(string* predicate_ins, int* predicate_idx, int attr_num) {
        vector<set<uint32_t>> samples;
        double nmax = 0;
        double b_0 = k * attr_num;
        for (int j = 0; j < bucket_d; ++j) {
            unordered_set<uint32_t> tmp;
            double n_sum = 0;
            for (int _pid = 0; _pid < attr_num; ++_pid) {
                const string& predicate = predicate_ins[_pid];
                if (predicate.empty()) {
                    continue;
                }
                uint32_t phash = H(predicate, FP_SEED);

                int pid = predicate_idx[_pid];

                uint32_t index = H(phash ^ seeds[j]) % bucket_len;

                Bucket& bucket = buckets[pid][j][index];

                n_sum += bucket.cnt;
                for (int i = 0; i < bucket.heap->CurrentSize; i++) {
                    tmp.insert(bucket.heap->data_set[i]);
                }
            }

            nmax = max(nmax, n_sum);
            set<uint32_t> sampleSet;
            for (int e : tmp) {
                sampleSet.insert(e);
            }
            samples.push_back(std::move(sampleSet));
        }

        int c = getAltEstKMV(samples);
        double ans = nmax / b_0 * c;

        return ans;
    }

    double query_range(uint32_t l, uint32_t r) {
        vector<pair<uint64_t, uint64_t>> ranges = wrapLogRanges(l, r);

        vector<set<uint32_t>> samples;
        double nmax = 0;
        double b_0 = k * ranges.size();
        for (int j = 0; j < r_bucket_d; ++j) {
            unordered_set<uint32_t> tmp;
            double n_sum = 0;
            for (auto& [l, r] : ranges) {
                uint64_t predicate = (l << 32) | r;

                uint32_t phash = H(predicate, FP_SEED);

                uint32_t index = H(phash ^ seeds[j]) % r_bucket_len;

                Bucket& bucket = r_buckets[j][index];

                n_sum += bucket.cnt;
                for (int i = 0; i < bucket.heap->CurrentSize; i++) {
                    tmp.insert(bucket.heap->data_set[i]);
                }
            }

            nmax = max(nmax, n_sum);
            set<uint32_t> sampleSet;
            for (int e : tmp) {
                sampleSet.insert(e);
            }
            samples.push_back(std::move(sampleSet));
        }

        int c = getAltEstKMV(samples);
        double ans = nmax / b_0 * c;

        return ans;
    }
};
