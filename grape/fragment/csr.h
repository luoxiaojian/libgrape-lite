#ifndef GRAPE_FRAGMENT_CSR_H_
#define GRAPE_FRAGMENT_CSR_H_

#include <memory>
#include <utility>
#include <vector>

#include "grape/utils/gcontainer.h"
#include "grape/config.h"
#include "grape/graph/adj_list.h"

namespace grape {

template <typename T>
struct CSR;

template <typename T>
class CSRAppendBuilder {
 public:
  template <typename ITER_T>
  void append(ITER_T begin, ITER_T end) {
    size_t old = entries_.size();
    entries_.insert(entries_.end(), begin, end);
    degrees_.push_back(entries_.size() - old);
  }

  void build(CSR<T>& ret) {
    size_t vertex_num = degrees_.size();
    size_t edge_num = entries_.size();

    ret.vertex_num = vertex_num;
    ret.edge_num = edge_num;
    ret.offsets_buffer_ = std::make_shared<Array<T*, Allocator<T*>>>(vertex_num + 1);
    ret.entries_buffer_ = std::make_shared<Array<T, Allocator<T>>>(edge_num);

    std::copy_n(entries_.begin(), edge_num, ret.entries_buffer_->begin());
    T** offsets = ret.offsets_buffer_->data();
    offsets[0] = ret.entries_buffer_->data();
    for (size_t i = 0; i < vertex_num; ++i) {
      offsets[i + 1] = offsets[i] + degrees_[i];
    }

    ret.offsets_ = offsets;
  }

 private:
  std::vector<int> degrees_;
  std::vector<T> entries_;
};

template <typename T>
class CSRBuilder {
 public:
  void init(size_t vertex_num) {
    vertex_num_ = vertex_num;

    degrees_.clear();
    degrees_.resize(vertex_num_, 0);

    offsets_buffer_ = std::make_shared<Array<T*, Allocator<T*>>>(vertex_num_ + 1);
    offsets_ = offsets_buffer_->data();
  }

  void inc_degree(size_t i) {
    ++degrees_[i];
  }

  void build_offsets() {
    edge_num_ = 0;
    for (auto d : degrees_) {
      edge_num_ += d;
    }

    entries_buffer_ = std::make_shared<Array<T, Allocator<T>>>(edge_num_);

    offsets_[0] = entries_buffer_->data();
    for (size_t i = 0; i < vertex_num_; ++i) {
      offsets_[i + 1] = offsets_[i] + degrees_[i];
    }

    iters_ = *offsets_buffer_;
  }

  template <typename... Args>
  void add_edge(size_t src, Args&&... args) {
    T* cur = iters_[src]++;
    new cur T(std::forward<Args>(args)...);
  }

  void build(CSR<T>& ret) {
    ret.vertex_num_ = vertex_num_;
    ret.edge_num_ = edge_num_;

    ret.offsets_buffer_ = offsets_buffer_;
    ret.offsets_ = ret.offsets_buffer_->data();
    ret.entries_buffer_ = entries_buffer_;
  }

 private:
  size_t vertex_num_;
  size_t edge_num_;

  std::vector<int> degrees_;

  std::shared_ptr<Array<T*, Allocator<T*>>> offsets_buffer_;
  T** offsets_;

  Array<T*, Allocator<T*>> iters_;

  std::shared_ptr<Array<T, Allocator<T>>> entries_buffer_;
};

template <typename T>
class MultiChannelsCSR;

template <typename T>
class CSR {
 public:
  CSR() : vertex_num_(0), edge_num_(0), offsets_buffer_(nullptr), entries_buffer_(nullptr), offsets_(NULL) {}
  ~CSR() {}

  inline bool empty() const { return (vertex_num_ == 0); }

  inline int degree(size_t i) {
    return offsets_[i + 1] - offsets_[i];
  }

  inline T* get_begin(size_t i) {
    return offsets_[i];
  }

  inline T* get_end(size_t i) {
    return offsets_[i + 1];
  }

  inline const T* get_begin(size_t i) const {
    return offsets_[i];
  }

  inline const T* get_end(size_t i) const {
    return offsets_[i + 1];
  }

  inline size_t edge_num() const { return edge_num_; }

  inline size_t vertex_num() const { return vertex_num_; }

  template <typename IOADAPTOR_T>
  bool Serialize(std::unique_ptr<IOADAPTOR_T>& adaptor) {
    std::vector<int> degree_list(vertex_num_);
    for (size_t i = 0; i < vertex_num_; ++i) {
      degree_list[i] = offsets_[i + 1] - offsets_[i];
    }

    InArchive ia;
    ia << vertex_num_ << edge_num_;

    bool ret = adaptor->WriteArchive(ia);
    if (!ret) {
      return ret;
    }
    ia.Clear();

    ret = adaptor->Write(&degree_list[0], vertex_num_ * sizeof(int));
    if (!ret) {
      return ret;
    }

    if (std::is_pod<T>::value) {
      ret = adaptor->Write(entries_buffer_->data(), edge_num_ * sizeof(T));
    } else {
      T* entries = entries_buffer_->data();
      for (size_t i = 0; i < edge_num_; ++i) {
        ia << entries[i];
      }
      ret = adaptor->WriteArchive(ia);
      ia.Clear();
    }
    if (!ret) {
      return ret;
    }
    return true;
  }

  template <typename IOADAPTOR_T>
  bool Deserialize(std::unique_ptr<IOADAPTOR_T>& adaptor) {
    OutArchive oa;
    bool ret = adaptor->ReadArchive(oa);
    if (!ret) {
      return ret;
    }
    oa >> vertex_num_ >> edge_num_;
    oa.Clear();

    std::vector<int> degree_list(vertex_num_);
    ret = adaptor->Read(&degree_list[0], vertex_num_ * sizeof(int));
    if (!ret) {
      return ret;
    }

    entries_buffer_ = std::make_shared<Array<T, Allocator<T>>>(edge_num_);
    if (std::is_pod<T>::value) {
      ret = adaptor->Read(entries_buffer_->data(), edge_num_ * sizeof(T));
    } else {
      T* entries = entries_buffer_->data();
      ret = adaptor->ReadArchive(oa);
      for (size_t i = 0; i < edge_num_; ++i) {
        oa >> entries[i];
      }
    }
    if (!ret) {
      return ret;
    }

    offsets_buffer_ = std::make_shared<Array<T*, Allocator<T*>>>(vertex_num_ + 1);
    offsets_ = offsets_buffer_->data();
    offsets_[0] = entries_buffer_->data();
    for (size_t i = 0; i < vertex_num_; ++i) {
      offsets_[i + 1] = offsets_[i] + degree_list[i];
    }

    return true;
  }

 private:
  template <typename _T>
  friend class CSRAppendBuilder<_T>;

  template <typename _T>
  friend class CSRBuilder<_T>;

  template <typename _T>
  friend class MultiChannelsCSR<_T>;

  size_t vertex_num_;
  size_t edge_num_;

  std::shared_ptr<Array<T*, Allocator<T *>>> offsets_buffer_;
  std::shared_ptr<Array<T, Allocator<T>>> entries_buffer_;

  T** offsets_;
};

template <typename T>
class MultiChannelsCSR {
 public:
  MultiChannelsCSR() {}
  ~MultiChannelsCSR() {}

  template <typename SPLITER_T>
  void init(int channel_num, CSR<T>& csr, const SPLITER_T& spliter) {
    channel_num_ = channel_num;
    vertex_num_ = csr.vertex_num();
    edge_num_ = csr.edge_num();

    offsets_buffer_ = csr.offsets_buffer_;
    entries_buffer_ = csr.entries_buffer_;

    offsets_.resize(channel_num_ + 1);
    offsets_[0] = offsets_.data();
    offsets_[channel_num_] = offsets_.data() + 1;

    if (channel_num_ > 1) {
      spliters_.clear();
      spliters_.resize(channel_num_ - 1);
      for (int i = 0; i < channel_num_ - 1; ++i) {
        spliters_[i].clear();
        spliters_[i].resize(vertex_num_);
        offsets_[i + 1] = spliters_[i].data();
      }
      T** begin_list = offsets_[0];
      T** end_list = offsets_[channel_num_];

      std::vector<int> split_num(channel_num);
      for (size_t i = 0; i < vertex_num_; ++i) {
        spliter(begin_list[i], end_list[i], split_num);
        T* cur = begin_list[i];
        for (int i = 0; i < channel_num_ - 1; ++i) {
          cur += split_num[i];
          spliters_[i] = cur;
        }
      }
    }
  }

 private:
  int channel_num_;
  size_t vertex_num_;
  size_t edge_num_;

  std::shared_ptr<Array<T*, Allocator<T*>>> offsets_buffer_;
  std::shared_ptr<Array<T, Allocator<T>>> entries_buffer_;

  std::vector<T**> offsets_;
  std::vector<std::vector<T*>> spliters_;
};

}  // namespace grape

#endif  // GRAPE_FRAGMENT_CSR_H_