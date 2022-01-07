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

#ifndef EXAMPLES_ANALYTICAL_APPS_TRAVERSE_TRAVERSE_H_
#define EXAMPLES_ANALYTICAL_APPS_TRAVERSE_TRAVERSE_H_

#include <grape/grape.h>

#include "traverse/traverse_context.h"

namespace grape {

template <typename FRAG_T>
class Traverse : public ParallelAppBase<FRAG_T, TraverseContext<FRAG_T>>,
                 public ParallelEngine {
 public:
  INSTALL_PARALLEL_WORKER(Traverse<FRAG_T>, TraverseContext<FRAG_T>, FRAG_T)

  void PEval(const fragment_t& frag, context_t& ctx,
             message_manager_t& messages) {}

  void IncEval(const fragment_t& frag, context_t& ctx,
               message_manager_t& messages) {}
};

}  // namespace grape

#endif  // EXAMPLES_ANALYTICAL_APPS_TRAVERSE_TRAVERSE_H_
