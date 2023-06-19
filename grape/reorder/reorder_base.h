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

#ifndef GRAPE_REORDER_REORDER_BASE_H_
#define GRAPE_REORDER_REORDER_BASE_H_

namespace grape {

template <typename FRAG_T>
class ReorderBase {
 public:
  virtual void Reorder(FRAG_T& fragment) = 0;
};

template <typename FRAG_T>
class DummyReorder : public ReorderBase<FRAG_T> {
 public:
  void Reorder(FRAG_T& fragment) override {}
};

}  // namespace grape

#endif  // GRAPE_REORDER_REORDER_BASE_H_
