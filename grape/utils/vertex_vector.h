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

#ifndef GRAPE_UTILS_VERTEX_VECTOR_H_
#define GRAPE_UTILS_VERTEX_VECTOR_H_
#include <algorithm>
#include <utility>

#include "grape/config.h"
#include "grape/serialization/in_archive.h"
#include "grape/serialization/out_archive.h"
#include "grape/utils/gcontainer.h"
#include "grape/utils/vertex_array.h"

namespace grape {
/**
 * @brief A discontinuous vertices collection representation. A increasing
 * labeled(but not need continuous) vertices must be provided to construct the
 * VertexVector
 *
 * @tparam T Vertex ID type.
 */
template <typename T>
class VertexVector {
 public:
  VertexVector() = default;
  explicit VertexVector(std::vector<Vertex<T>> &vertices)
      : vertices_(vertices) {
    // verify increasing order
    if (vertices.size() > 0) {
      auto prev = vertices[0];
      for (auto curr : vertices) {
        CHECK_LE(prev.GetValue(), curr.GetValue());
        prev = curr;
      }
    }
  }

  inline typename std::vector<Vertex<T>>::iterator begin() {
    return vertices_.begin();
  }

  inline typename std::vector<Vertex<T>>::iterator end() {
    return vertices_.end();
  }

  inline typename std::vector<Vertex<T>>::const_iterator cbegin() const {
    return vertices_.cbegin();
  }

  inline typename std::vector<Vertex<T>>::const_iterator cend() const {
    return vertices_.cend();
  }

  Vertex<T> operator[](size_t idx) { return vertices_[idx]; }

  Vertex<T> operator[](size_t idx) const { return vertices_[idx]; }

  inline size_t size() const { return vertices_.size(); }

  void Swap(VertexVector &rhs) {
    using std::swap;
    swap(vertices_, rhs.vertices_);
  }

 private:
  std::vector<Vertex<T>> vertices_;
};

template <typename T, typename VID_T>
class VertexValues : public Array<T, Allocator<T>> {
  using Base = Array<T, Allocator<T>>;

 public:
  VertexValues() = default;

  explicit VertexValues(const VertexVector<VID_T> &vertices) { Init(vertices); }

  VertexValues(const VertexVector<VID_T> &vertices, const T &value) {
    Init(vertices, value);
  }

  ~VertexValues() = default;

  void Init(const VertexVector<VID_T> &vertices) {
    if (vertices.size() == 0) return;
    const auto &min_v = vertices[0];
    const auto &max_v = vertices[vertices.size() - 1];

    Base::resize(max_v.GetValue() - min_v.GetValue() + 1);
    vertices_ = vertices;
    fake_start_ = Base::data() - min_v.GetValue();
  }

  void Init(const VertexVector<VID_T> &vertices, const T &value) {
    if (vertices.size() == 0) return;
    const auto &min_v = vertices[0];
    const auto &max_v = vertices[vertices.size() - 1];

    Base::resize(max_v.GetValue() - min_v.GetValue() + 1, value);
    vertices_ = vertices;
    fake_start_ = Base::data() - min_v.GetValue();
  }

  void SetValue(VertexVector<VID_T> &vertices, const T &value) {
    for (auto v : vertices) {
      fake_start_[v] = value;
    }
  }

  void SetValue(const T &value) {
    std::fill_n(Base::data(), Base::size(), value);
  }

  inline T &operator[](const Vertex<VID_T> &loc) {
    return fake_start_[loc.GetValue()];
  }

  inline const T &operator[](const Vertex<VID_T> &loc) const {
    return fake_start_[loc.GetValue()];
  }

  void Swap(VertexValues &rhs) {
    Base::swap((Base&) rhs);
    vertices_.Swap(rhs.vertices_);
    std::swap(fake_start_, rhs.fake_start_);
  }

  void Clear() {
    VertexValues ga;
    this->Swap(ga);
  }

  const VertexVector<VID_T> &GetVertexRange() const { return vertices_; }

 private:
  void Resize() {}

  VertexVector<VID_T> vertices_;
  T *fake_start_;
};
}  // namespace grape
#endif  // GRAPE_UTILS_VERTEX_VECTOR_H_
