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

#ifndef GRAPE_REORDER_REORDER_FACTORY_H_
#define GRAPE_REORDER_REORDER_FACTORY_H_

#include "grape/reorder/degree_reorder.h"
#include "grape/reorder/gorder_reorder.h"
#include "grape/reorder/reorder_base.h"

namespace grape {

template <typename FRAG_T>
std::shared_ptr<ReorderBase<FRAG_T>> create_reorder(int type) {
  if (type == 1) {
    return std::make_shared<DegreeAscReorder<FRAG_T>>();
  } else if (type == 2) {
    return std::make_shared<DegreeDescReorder<FRAG_T>>();
  } else if (type >= 3 && type <= 11) {
    uint32_t w = static_cast<uint32_t>(type - 2);
    w = (4 << w);
    return std::make_shared<GOrderReorder<FRAG_T>>(w);
  } else {
    return std::make_shared<DummyReorder<FRAG_T>>();
  }
}

}  // namespace grape

#endif  // GRAPE_REORDER_REORDER_FACTORY_H_
