#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace std;

struct MaxHeap {
    typedef uint32_t node;

    vector<node> data_set;
    int CurrentSize;
    int DefaultSize;

    MaxHeap(int sz) {
        data_set.reserve(sz);
        DefaultSize = sz;
        CurrentSize = 0;
    }

    void ShiftDown(int start, int m) {
        int index_i = start;
        int index_j = 2 * index_i + 1;
        node temp = data_set[index_i];

        while (index_j <= m) {
            int left_index = index_j;
            int right_index = left_index + 1;

            if (left_index < m && data_set[left_index] < data_set[right_index]) {
                index_j = right_index;
            } else {
                index_j = left_index;
            }

            if (temp >= data_set[index_j]) {
                break;
            } else {
                data_set[index_i] = data_set[index_j];
                index_i = index_j;
                index_j = 2 * index_i + 1;
            }
        }

        data_set[index_i] = temp;
    }

    void ShiftUp(int start) {
        int index_j = start;
        node temp = data_set[index_j];

        while (index_j > 0) {
            int parent_index = (index_j - 1) / 2;
            if (data_set[parent_index] >= temp) {
                break;
            } else {
                data_set[index_j] = data_set[parent_index];
                index_j = parent_index;
            }
        }

        data_set[index_j] = temp;
    }

    bool Insert(node x) {

        for (int i = 0; i < CurrentSize; ++i) {
            if (data_set[i] == x) {
                return false;
            }
        }

        if (CurrentSize < DefaultSize) {
            data_set.push_back(x);
            ShiftUp(CurrentSize);
            CurrentSize++;
        } else {
            if (x < data_set[0]) {
                data_set[0] = x;
                ShiftDown(0, CurrentSize - 1);
            }
        }
        return true;
    }
};

