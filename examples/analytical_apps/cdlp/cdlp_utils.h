/** Copyright 2020 Alibaba Group Holding Limited.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef EXAMPLES_ANALYTICAL_APPS_CDLP_CDLP_UTILS_H_
#define EXAMPLES_ANALYTICAL_APPS_CDLP_CDLP_UTILS_H_

#include <algorithm>
#include <random>
#include <vector>

#include <grape/grape.h>
#include <x86intrin.h>
#include "grape/simd/avx512-64bit-qsort.hpp"
#include "grape/simd/avx512-32bit-qsort.hpp"

namespace grape {

template <typename LABEL_T>
using LabelMapType = std::map<LABEL_T, int>;

void mfl(int64_t* arr, int num, int64_t& best_label, int& best_count) {
  if (num <= 8) {
    return;
  } else {
    __m256i curr = _mm256_lddqu_si256((__m256i*) arr);
    int i = 4;
    int count = 0;
    int64_t curr_value{};
    while (i + 4 <= num) {
      __m256i next = _mm256_lddqu_si256((__m256i*) (arr + i));
      __mmask8 c = _mm256_cmpeq_epi64_mask(curr, next);
      if (count == 0) {
        if (c & 1) {
          count = (4 + __builtin_popcount(c));
	  curr_value = _mm256_extract_epi64(next, 0);
	}
      } else {
	count += __builtin_popcount(c);
	if (!(c & 1)) {
          if (count > best_count) {
            best_count = count;
	    best_label = curr_value;
	  }
	  count = 0;
	}
      }
      curr = next;
      i += 4;
    }
    if (!count) {
      while (i != num && arr[i] == curr_value) {
        ++i;
	++count;
      }
    }
    if (count > best_count) {
      best_count = count;
      best_label = curr_value;
    }
  }
}

void binary_mfi(int64_t* left, int64_t* right, int64_t &max_label, int &max_count) {
  int64_t* mid = left + (right - left) / 2;
  int64_t curr_label = *mid;
  auto lb = std::lower_bound(mid, right, curr_label + 1);
  auto rb = std::upper_bound(left, mid, curr_label - 1);
   
  if (rb - left > max_count) {
    binary_mfi(left, rb, max_label, max_count);
  }

  int curr_count = lb - rb;
  if (curr_count > max_count) {
    max_count = curr_count;
    max_label = curr_label;
  }

  if (right - lb > max_count) {
    binary_mfi(lb, right, max_label, max_count);
  }
}

/*
void binary_mfi(int64_t* arr, int left, int right, int64_t &max_label, int &max_count) {
  int mid = (left + right) / 2;
  int64_t curr_label = arr[mid];
  auto lb = std::lower_bound(&arr[mid], &arr[right], curr_label + 1);
  auto rb = std::upper_bound(&arr[left], &arr[mid], curr_label - 1);
  int curr_count = lb - rb;

  int new_left = lb - arr;
  int new_right = rb - arr;
   
  if (curr_count > max_count) {
    max_count = curr_count - 1;
  }
  if (new_right - left > max_count) {
    binary_mfi(arr, left, new_right, max_label, max_count);
  }

  if (curr_count > max_count) {
    max_count = curr_count;
    max_label = curr_label;
  }

  if (right - new_left > max_count) {
    binary_mfi(arr, new_left, right, max_label, max_count);
  }
}
*/

template <typename LABEL_T, typename VERTEX_ARRAY_T, typename ADJ_LIST_T>
inline LABEL_T update_label_fast(const ADJ_LIST_T& edges,
                                 const VERTEX_ARRAY_T& labels) {
  static thread_local std::vector<LABEL_T> local_labels;
  local_labels.clear();
  for (auto& e : edges) {
    local_labels.emplace_back(labels[e.get_neighbor()]);
  }
  // std::sort(local_labels.begin(), local_labels.end());
  avx512_qsort(local_labels.data(), local_labels.size());

#if 0
  LABEL_T best_label{};
  int best_count = 0;

  binary_mfi(local_labels.data(), local_labels.data() + local_labels.size(), best_label, best_count);
  // binary_mfi(local_labels.data(), 0, local_labels.size(), best_label, best_count);

  return best_label;
#endif

#if 0
  LABEL_T curr_label = local_labels[0];
  int curr_count = 1;
  LABEL_T best_label = LABEL_T{};
  int best_count = 0;

  int64_t* arr = local_labels.data();
  int label_num = local_labels.size();
  for (int i = 1; i < label_num; ++i) {
    if (arr[i] != arr[i - 1]) {
      if (curr_count > best_count) {
        best_label = curr_label;
	best_count = curr_count;
      }
      if (best_count >= 8) {
        mfl(&arr[i], label_num - i, best_label, best_count);
	return best_label;
      } else {
        curr_label = local_labels[i];
        curr_count = 1;
      }
    } else {
      ++curr_count;
    }
  }

  if (curr_count > best_count) {
    return curr_label;
  } else {
    return best_label;
  }
#endif

#if 1
  LABEL_T curr_label = local_labels[0];
  int curr_count = 1;
  LABEL_T best_label = LABEL_T{};
  int best_count = 0;
  int label_num = local_labels.size();

  for (int i = 1; i < label_num; ++i) {
    if (local_labels[i] != local_labels[i - 1]) {
      if (curr_count > best_count) {
        best_label = curr_label;
        best_count = curr_count;
      }
      curr_label = local_labels[i];
      curr_count = 1;
    } else {
      ++curr_count;
    }
  }

  if (curr_count > best_count) {
    return curr_label;
  } else {
    return best_label;
  }
#else
  /*
  auto from = local_labels.begin();
  auto to = local_labels.end();

  LABEL_T best_label{};
  int best_count = 0;

  while (from != to) {
    LABEL_T curr = *from;
    auto new_from = std::lower_bound(from, to, curr + 1);
    int count = new_from - from;
    if (count > best_count) {
      best_label = curr;
      best_count = count;
    }
    from = new_from;
  }

  return best_label;
  */
  
#endif
}

template <typename LABEL_T, typename VERTEX_ARRAY_T, typename ADJ_LIST_T>
inline LABEL_T update_label_fast_jump(const ADJ_LIST_T& edges,
                                      const VERTEX_ARRAY_T& labels) {
  static thread_local std::vector<LABEL_T> local_labels;
  local_labels.clear();
  for (auto& e : edges) {
    local_labels.emplace_back(labels[e.get_neighbor()]);
  }
  // std::sort(local_labels.begin(), local_labels.end());
  avx512_qsort(local_labels.data(), local_labels.size());

#if 1
  LABEL_T curr_label = local_labels[0];
  int curr = 1;
  int label_num = local_labels.size();

  while ((curr != label_num) && (local_labels[curr] == curr_label)) {
    ++curr;
  }

  LABEL_T best_label = curr_label;
  int best_count = curr;

  while ((curr + best_count) < label_num) {
    curr_label = local_labels[curr];
    int next = curr + best_count;
    if (local_labels[next] == curr_label) {
      do {
        ++next;
      } while (next != label_num && (local_labels[next] == curr_label));
      best_count = (next - curr);
      best_label = curr_label;
      curr = next;
    } else {
      curr = next;
      curr_label = local_labels[next];
      while (local_labels[curr - 1] == curr_label) {
        --curr;
      }
    }
  }

  return best_label;
#else
  int best_count = 0;
  int64_t best_label = 0;

  int index = 0;
  int64_t* arr = local_labels.data();
  int label_num = local_labels.size();
  while (index != label_num) {
    int64_t curr_label = arr[index];
    int from = index;
    if (index + 4 < label_num) {
      __m256i curr_label_x = _mm256_set1_epi64x(curr_label);
      int c = 0;
      while (index + 4 < label_num) {
        __m256i curr_part = _mm256_lddqu_si256((__m256i*)(arr + index));
        c = __builtin_popcount(_mm256_cmpeq_epi64_mask(curr_label_x, curr_part));
        index += c;
        if (c != 4) {
          break;
        }
      }
      if (c == 4) {
        while (index != label_num && arr[index] == curr_label && index != label_num) {
          ++index;
        }
      }
    } else {
      ++index;
      while (index != label_num && arr[index] == curr_label && index != label_num) {
        ++index;
      }
    }
    int curr_count = index - from;
    if (curr_count > best_count) {
      best_count = curr_count;
      best_label = curr_label;
    }
  }

  return best_label;
#endif
}

template <typename LABEL_T, typename VERTEX_ARRAY_T, typename ADJ_LIST_T>
inline LABEL_T update_label_fast_sparse(const ADJ_LIST_T& edges,
                                        const VERTEX_ARRAY_T& labels) {
  static thread_local LabelMapType<LABEL_T> labels_map;
  labels_map.clear();
  for (auto& e : edges) {
    ++labels_map[labels[e.get_neighbor()]];
  }
  LABEL_T ret{};
  int max_count = 0;
  for (auto& pair : labels_map) {
    if (pair.second > max_count ||
        (pair.second == max_count && ret > pair.first)) {
      ret = pair.first;
      max_count = pair.second;
    }
  }
  return ret;
}

template <typename T, typename CNT_T = int>
class LabelHashMap {
 public:
  LabelHashMap() {}
  ~LabelHashMap() {}

  void resize(size_t n) {
    entries_.resize(n);
    counts_.resize(n, 0);
  }

  void emplace(const T& val) {
    size_t len = entries_.size();
    size_t index = val % len;
    while (true) {
      if (counts_[index] == 0) {
        counts_[index] = 1;
        entries_[index] = val;
        index_.push_back(index);
        break;
      } else if (entries_[index] == val) {
        ++counts_[index];
        break;
      }
      index = (index + 1) % len;
    }
  }

  T get_most_frequent_label() {
    T ret = std::numeric_limits<T>::max();
    CNT_T freq = 0;
    if (index_.size() <= entries_.size() / 2) {
      for (auto ind : index_) {
        if (counts_[ind] > freq) {
          freq = counts_[ind];
          ret = entries_[ind];
        } else if (counts_[ind] == freq && entries_[ind] < ret) {
          ret = entries_[ind];
        }
        counts_[ind] = 0;
      }
    } else {
      CNT_T num_entries = entries_.size();
      for (CNT_T i = 0; i != num_entries; ++i) {
        if (counts_[i] > freq) {
          freq = counts_[i];
          ret = entries_[i];
        } else if (counts_[i] == freq && entries_[i] < ret) {
          ret = entries_[i];
        }
        counts_[i] = 0;
      }
    }
    index_.clear();
    return ret;
  }

 private:
  std::vector<T> entries_;
  std::vector<CNT_T> counts_;
  std::vector<CNT_T> index_;
};

template <typename LABEL_T, typename VERTEX_ARRAY_T, typename ADJ_LIST_T>
inline LABEL_T update_label_fast_dense(const ADJ_LIST_T& edges,
                                       const VERTEX_ARRAY_T& labels) {
  static thread_local LabelHashMap<LABEL_T> labels_map;
  labels_map.resize(edges.Size());
  for (auto& e : edges) {
    labels_map.emplace(labels[e.get_neighbor()]);
  }
  return labels_map.get_most_frequent_label();
}

}  // namespace grape

#endif  // EXAMPLES_ANALYTICAL_APPS_CDLP_CDLP_UTILS_H_
