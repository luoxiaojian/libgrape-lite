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

#ifndef GRAPE_COMMUNICATION_SHUFFLE_BETA_H_
#define GRAPE_COMMUNICATION_SHUFFLE_BETA_H_

#include <grape/communication/shuffle.h>

namespace grape {

template <int index, typename First, typename... Rest>
struct GetImpl;

template <typename First, typename... Rest>
struct ShuffleTuple : public ShuffleTuple<Rest...> {
  ShuffleTuple() : ShuffleTuple<Rest...>() {}

  void Emplace(const First& v0, const Rest&... vx) {
    first.emplace(v0);
    ShuffleTuple<Rest...>::Emplace(vx...);
  }

  void SendTo(int dst_worker_id, int tag, MPI_Comm comm) {
    first.SendTo(dst_worker_id, tag, comm);
    ShuffleTuple<Rest...>::SendTo(dst_worker_id, tag, comm);
  }

  void RecvFrom(int src_worker, int tag, MPI_Comm comm) {
    first.RecvFrom(src_worker, tag, comm);
    ShuffleTuple<Rest...>::RecvFrom(src_worker, tag, comm);
  }

  void Clear() {
    first.clear();
    ShuffleTuple<Rest...>::Clear();
  }

  ShuffleUnit<First> first;
};

template <typename First>
struct ShuffleTuple<First> {
  ShuffleTuple() {}

  void Emplace(const First& v0) { first.emplace(v0); }

  void SendTo(int dst_worker_id, int tag, MPI_Comm comm) {
    first.SendTo(dst_worker_id, tag, comm);
  }

  void RecvFrom(int src_worker, int tag, MPI_Comm comm) {
    first.RecvFrom(src_worker, tag, comm);
  }

  void Clear() { first.clear(); }

  ShuffleUnit<First> first;
};

template <typename First, typename... Rest>
class ShuffleOut {
 public:
  ShuffleOut() {}

  void Init(MPI_Comm comm, int tag = 0, size_t cs = 4096) {
    comm_ = comm;
    tag_ = tag;
    chunk_size_ = cs;
    current_size_ = 0;
    comm_disabled_ = false;
  }
  void DisableComm() { comm_disabled_ = true; }
  void SetDestination(int dst_worker_id, fid_t dst_frag_id) {
    dst_worker_id_ = dst_worker_id;
    dst_frag_id_ = dst_frag_id;
  }

  void Clear() {
    current_size_ = 0;
    tuple_.Clear();
  }

  void Emplace(const First& t0, const Rest&... rest) {
    tuple_.Emplace(t0, rest...);
    ++current_size_;
    if (comm_disabled_) {
      return;
    }
    if (current_size_ >= chunk_size_) {
      issue();
      Clear();
    }
  }

  void Flush() {
    if (comm_disabled_) {
      return;
    }
    if (current_size_) {
      issue();
      Clear();
    }
    issue();
  }

  ShuffleTuple<First, Rest...> tuple_;

 private:
  void issue() {
    frag_shuffle_header header(current_size_, dst_frag_id_);
    MPI_Send(&header, static_cast<int>(sizeof(frag_shuffle_header)), MPI_CHAR,
             dst_worker_id_, tag_, comm_);
    if (current_size_) {
      tuple_.SendTo(dst_worker_id_, tag_, comm_);
    }
  }

  size_t chunk_size_;
  size_t current_size_;
  int dst_worker_id_;
  fid_t dst_frag_id_;
  int tag_;
  bool comm_disabled_;

  MPI_Comm comm_;
};

template <typename First, typename... Rest>
class ShuffleIn {
 public:
  ShuffleIn() {}

  void Init(fid_t fnum, MPI_Comm comm, int tag = 0) {
    remaining_frag_num_ = fnum - 1;
    comm_ = comm;
    tag_ = tag;
    current_size_ = 0;
  }

  int Recv(fid_t& fid) {
    MPI_Status status;
    frag_shuffle_header header;
    while (true) {
      if (remaining_frag_num_ == 0) {
        return -1;
      }
      MPI_Recv(&header, static_cast<int>(sizeof(frag_shuffle_header)), MPI_CHAR,
               MPI_ANY_SOURCE, tag_, comm_, &status);
      if (header.size == 0) {
        --remaining_frag_num_;
      } else {
        int src_worker_id = status.MPI_SOURCE;
        current_size_ += header.size;
        fid = header.fid;
        tuple_.RecvFrom(src_worker_id, tag_, comm_);
        return src_worker_id;
      }
    }
  }

  int RecvFrom(int src_worker_id) {
    frag_shuffle_header header;
    MPI_Recv(&header, static_cast<int>(sizeof(frag_shuffle_header)), MPI_CHAR,
             src_worker_id, tag_, comm_, MPI_STATUS_IGNORE);
    if (header.size == 0) {
      --remaining_frag_num_;
    } else {
      current_size_ += header.size;
      tuple_.RecvFrom(src_worker_id, tag_, comm_);
    }
    return header.size;
  }

  bool Finished() { return remaining_frag_num_ == 0; }

  void Clear() {
    tuple_.Clear();
    current_size_ = 0;
  }

  size_t Size() const { return current_size_; }

  ShuffleTuple<First, Rest...> tuple_;

 private:
  fid_t remaining_frag_num_;
  int tag_;
  size_t current_size_;
  MPI_Comm comm_;
};

template <std::size_t index, typename Tp>
struct shuffle_tuple_element;

template <std::size_t index, typename Head, typename... Tail>
struct shuffle_tuple_element<index, ShuffleTuple<Head, Tail...>>
    : shuffle_tuple_element<index - 1, ShuffleTuple<Tail...>> {};

template <typename Head, typename... Tail>
struct shuffle_tuple_element<0, ShuffleTuple<Head, Tail...>> {
  typedef typename ShuffleUnit<Head>::BufferT type;
};

template <typename T>
struct add_ref {
  typedef T& type;
};

template <typename T>
struct add_ref<T&> {
  typedef T& type;
};

template <std::size_t index, typename Head, typename... Tail>
struct get_buffer_helper {
  static typename add_ref<typename shuffle_tuple_element<
      index, ShuffleTuple<Head, Tail...>>::type>::type
  value(ShuffleTuple<Head, Tail...>& t) {
    return get_buffer_helper<index - 1, Tail...>::value(t);
  }
};

template <typename Head, typename... Tail>
struct get_buffer_helper<0, Head, Tail...> {
  static typename ShuffleUnit<Head>::BufferT& value(
      ShuffleTuple<Head, Tail...>& t) {
    return t.first.data();
  }
};

template <std::size_t index, typename Head, typename... Tail>
typename add_ref<typename shuffle_tuple_element<
    index, ShuffleTuple<Head, Tail...>>::type>::type
get_buffer(ShuffleIn<Head, Tail...>& t) {
  return get_buffer_helper<index, Head, Tail...>::value(t.tuple_);
}

template <std::size_t index, typename Head, typename... Tail>
typename add_ref<typename shuffle_tuple_element<
    index, ShuffleTuple<Head, Tail...>>::type>::type
get_buffer(ShuffleOut<Head, Tail...>& t) {
  return get_buffer_helper<index, Head, Tail...>::value(t.tuple_);
}

}  // namespace grape

#endif  // GRAPE_COMMUNICATION_SHUFFLE_BETA_H_
