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

#ifndef GRAPE_UTILS_STRING_VIEW_VECTOR_H_
#define GRAPE_UTILS_STRING_VIEW_VECTOR_H_

#include <stdlib.h>
#include <string.h>

#include <vector>

#include "string_view/string_view.hpp"

namespace grape {

template <typename T>
class VectorSlice {
 public:
  VectorSlice() : buffer_(NULL), size_(0), alloc_(false) {}
  VectorSlice(T* buffer, size_t size)
      : buffer_(buffer), size_(size), alloc_(false) {}
  ~VectorSlice() {
    if (alloc_ && buffer_ != NULL) {
      free(buffer_);
    }
  }

  void Init(size_t size) {
    buffer_ = static_cast<T*>(malloc(size * sizeof(T)));
    size_ = size;
    alloc_ = true;
  }

  const T& operator[](size_t ind) const { return buffer_[ind]; }

  T& operator[](size_t ind) { return buffer_[ind]; }

  T* buffer() { return buffer_; }

  const T* buffer() const { return buffer_; }

  size_t size() const { return size_; }

 private:
  T* buffer_;
  size_t size_;
  bool alloc_;
};

class StringViewVectorSlice {
 public:
  StringViewVectorSlice()
      : buffer_(NULL),
        buffer_size_(0),
        offsets_(NULL),
        offsets_size_(0),
        alloc_(false) {}
  StringViewVectorSlice(char* buffer, size_t buffer_size, size_t* offsets,
                        size_t offsets_size)
      : buffer_(buffer),
        buffer_size_(buffer_size),
        offsets_(offsets),
        offsets_size_(offsets_size),
        alloc_(false) {}
  ~StringViewVectorSlice() { release(); }

  void Init(size_t buffer_size, size_t offsets_size) {
    release();
    buffer_ = static_cast<char*>(malloc(buffer_size));
    buffer_size_ = buffer_size;
    offsets_ = static_cast<size_t*>(malloc(offsets_size * sizeof(size_t)));
    offsets_size_ = offsets_size;
    alloc_ = true;
  }

  nonstd::string_view operator[](size_t index) const {
    return nonstd::string_view(buffer_ + (offsets_[index] - offsets_[0]),
                               offsets_[index + 1] - offsets_[index]);
  }

  size_t size() const { return offsets_size_ - 1; }

  size_t buffer_size() const { return buffer_size_; }

  char* buffer() { return buffer_; }

  size_t* offsets() { return offsets_; }

  const char* buffer() const { return buffer_; }

  const size_t* offsets() const { return offsets_; }

 private:
  void release() {
    if (alloc_) {
      if (buffer_ != NULL) {
        free(buffer_);
        buffer_ = NULL;
      }
      buffer_size_ = 0;
      if (offsets_ != NULL) {
        free(offsets_);
        offsets_ = NULL;
      }
      offsets_size_ = 0;
    }
  }

  char* buffer_;
  size_t buffer_size_;
  size_t* offsets_;
  size_t offsets_size_;
  bool alloc_;
};

class StringViewVector {
 public:
  StringViewVector() { offsets_.push_back(0); }
  ~StringViewVector() {}

  void push_back(const nonstd::string_view& val) {
    size_t old_size = buffer_.size();
    buffer_.resize(old_size + val.size());
    memcpy(&buffer_[old_size], val.data(), val.size());
    offsets_.push_back(buffer_.size());
  }

  void emplace_back(const nonstd::string_view& val) {
    size_t old_size = buffer_.size();
    buffer_.resize(old_size + val.size());
    memcpy(&buffer_[old_size], val.data(), val.size());
    offsets_.push_back(buffer_.size());
  }

  size_t size() const {
    assert(offsets_.size() > 0);
    return offsets_.size() - 1;
  }

  nonstd::string_view operator[](size_t index) const {
    size_t from = offsets_[index];
    size_t len = offsets_[index + 1] - from;
    return nonstd::string_view(&buffer_[from], len);
  }

  std::vector<char>& content_buffer() { return buffer_; }

  const std::vector<char>& content_buffer() const { return buffer_; }

  std::vector<size_t>& offset_buffer() { return offsets_; }

  const std::vector<size_t>& offset_buffer() const { return offsets_; }

  void clear() {
    buffer_.clear();
    offsets_.clear();
    offsets_.push_back(0);
  }

  void swap(StringViewVector& rhs) {
    buffer_.swap(rhs.buffer_);
    offsets_.swap(rhs.offsets_);
  }

  StringViewVectorSlice GetSlice(size_t from, size_t to) {
    size_t buf_begin = offsets_[from];
    size_t buf_end = offsets_[to];
    return StringViewVectorSlice(&buffer_[buf_begin], buf_end - buf_begin,
                                 &offsets_[from], to - from + 1);
  }

 private:
  std::vector<char> buffer_;
  std::vector<size_t> offsets_;
};

}  // namespace grape

#endif  // GRAPE_UTILS_STRING_VIEW_VECTOR_H_
