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

#ifndef EXAMPLES_ANALYTICAL_APPS_TRAVERSE_TRAVERSE_MUTATE_CONTEXT_H_
#define EXAMPLES_ANALYTICAL_APPS_TRAVERSE_TRAVERSE_MUTATE_CONTEXT_H_

#include <iomanip>
#include <iostream>
#include <limits>

#include <grape/grape.h>

namespace grape {

template <typename FRAG_T>
class TraverseMutateContext : public VertexDataContext<FRAG_T, int>,
                              public MutationContext<FRAG_T> {
 public:
  explicit TraverseMutateContext(const FRAG_T& fragment)
      : VertexDataContext<FRAG_T, int>(fragment),
        MutationContext<FRAG_T>(fragment) {}

  void Init(ParallelMessageManager& messages) {
    auto& frag = this->fragment();
    auto& inner_vertices = frag.InnerVertices();
    for (auto& v : inner_vertices) {
      if (frag.GetId(v) % 2 == 0) {
        auto es = frag.GetOutgoingAdjList(v);
        for (auto& e : es) {
          if (frag.GetId(e.neighbor) % 2 == 0) {
            this->remove_edge(frag.GetId(v), frag.GetId(e.neighbor));
          }
        }
      }
    }
  }

  void Output(std::ostream& os) override {
    auto& frag = this->fragment();
    auto& inner_vertices = frag.InnerVertices();
    for (auto& v : inner_vertices) {
      auto es = frag.GetOutgoingAdjList(v);
      for (auto& e : es) {
        os << frag.GetId(v) << " " << frag.GetId(e.get_neighbor()) << " "
           << e.get_data() << std::endl;
      }
    }
  }
};

}  // namespace grape

#endif  // EXAMPLES_ANALYTICAL_APPS_TRAVERSE_TRAVERSE_MUTATE_CONTEXT_H_
