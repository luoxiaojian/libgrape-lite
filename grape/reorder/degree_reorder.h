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

#ifndef GRAPE_REORDER_DEGREE_REORDER_H_
#define GRAPE_REORDER_DEGREE_REORDER_H_

#include "grape/reorder/reorder_base.h"

#include <vector>

namespace grape {

template <typename V_T, typename D_T>
struct DegreeAscComparer {
  bool operator()(const std::pair<V_T, D_T>& a,
                  const std::pair<V_T, D_T>& b) const {
    return a.second < b.second;
  }
};

template <typename V_T, typename D_T>
struct DegreeDescComparer {
  bool operator()(const std::pair<V_T, D_T>& a,
                  const std::pair<V_T, D_T>& b) const {
    return a.second > b.second;
  }
};

template <typename FRAG_T, typename COMPARER_T>
class DegreeReorder : public ReorderBase<FRAG_T> {
  using vid_t = typename FRAG_T::vid_t;
  using vertex_t = typename FRAG_T::vertex_t;

 public:
  void Reorder(FRAG_T& frag) override {
    std::vector<std::pair<vertex_t, int>> vertex_degree;
    for (auto v : frag.InnerVertices()) {
      vertex_degree.emplace_back(
          v, frag.GetLocalOutDegree(v) + frag.GetLocalInDegree(v));
    }
    std::sort(vertex_degree.begin(), vertex_degree.end(), COMPARER_T());

    std::vector<vertex_t> vertex_ranking;
    vertex_ranking.reserve(frag.GetInnerVerticesNum());
    for (auto& vd : vertex_degree) {
      vertex_ranking.push_back(vd.first);
    }

    frag.Reorder(vertex_ranking);
  }
};

template <typename FRAG_T>
using DegreeAscReorder =
    DegreeReorder<FRAG_T, DegreeAscComparer<typename FRAG_T::vertex_t, int>>;

template <typename FRAG_T>
using DegreeDescReorder =
    DegreeReorder<FRAG_T, DegreeDescComparer<typename FRAG_T::vertex_t, int>>;

}  // namespace grape

#endif  // GRAPE_REORDER_DEGREE_REORDER_H_
