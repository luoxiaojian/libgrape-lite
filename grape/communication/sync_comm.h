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

#ifndef GRAPE_COMMUNICATION_SYNC_COMM_H_
#define GRAPE_COMMUNICATION_SYNC_COMM_H_

#include <mpi.h>

#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <glog/logging.h>

#include "grape/serialization/in_archive.h"
#include "grape/serialization/out_archive.h"

namespace grape {

#ifdef OPEN_MPI
#define NULL_COMM NULL
#else
#define NULL_COMM -1
#endif
#define ValidComm(comm) ((comm) != NULL_COMM)

inline void InitMPIComm() {
  int provided;
  MPI_Init_thread(NULL, NULL, MPI_THREAD_MULTIPLE, &provided);
}
inline void FinalizeMPIComm() { MPI_Finalize(); }

namespace sync_comm {

// static const int chunk_size = 409600;
static constexpr int chunk_size = 536870912;

template <typename T>
static inline void send_small_buffer(const T* ptr, size_t len, int dst_worker_id, MPI_Comm comm, int tag) {
  size_t len_in_bytes = len * sizeof(T);
  assert(len_in_bytes <= chunk_size);
  MPI_Send(ptr, len_in_bytes, MPI_CHAR, dst_worker_id, tag, comm);
}

template <typename T>
static inline void isend_small_buffer(const T* ptr, size_t len, int dst_worker_id,
                                      MPI_Comm comm, int tag,
                                      MPI_Request& req) {
  size_t len_in_bytes = len * sizeof(T);
  assert(len_in_bytes <= chunk_size);
  MPI_Isend(ptr, len_in_bytes, MPI_CHAR, dst_worker_id, tag, comm, &req);
}

template <typename T>
static inline void recv_small_buffer(T* ptr, size_t len, int src_worker_id, MPI_Comm comm, int tag) {
  size_t len_in_bytes = len * sizeof(T);
  assert(len_in_bytes <= chunk_size);
  MPI_Recv(ptr, len_in_bytes, MPI_CHAR, src_worker_id, tag, comm, MPI_STATUS_IGNORE);
}

template <typename T>
static inline void irecv_small_buffer(T* ptr, size_t len, int src_worker_id, MPI_Comm comm, int tag, MPI_Request& req) {
  size_t len_in_bytes = len * sizeof(T);
  assert(len_in_bytes <= chunk_size);
  MPI_Irecv(ptr, len_in_bytes, MPI_CHAR, src_worker_id, tag, comm, &req);
}

template <typename T>
static inline void send_buffer(const T* ptr, size_t len, int dst_worker_id,
                               MPI_Comm comm, int tag) {
  static constexpr size_t chunk_num = chunk_size / sizeof(T);
  if (len <= chunk_num) {
    send_small_buffer(ptr, len, dst_worker_id, comm, tag);
    return ;
  }
  const size_t chunk_size_in_bytes = chunk_num * sizeof(T);
  int iter = len / chunk_num;
  size_t remaining = (len % chunk_num) * sizeof(T);
  LOG(INFO) << "sending large buffer in " << iter + (remaining != 0) << " iterations";
  for (int i = 0; i < iter; ++i) {
    MPI_Send(ptr, chunk_size_in_bytes, MPI_CHAR, dst_worker_id, tag, comm);
    ptr += chunk_num;
  }
  if (remaining != 0) {
    MPI_Send(ptr, remaining, MPI_CHAR, dst_worker_id, tag, comm);
  }
}

template <typename T>
static inline void isend_buffer(const T* ptr, size_t len, int dst_worker_id,
                                MPI_Comm comm, int tag,
                                std::vector<MPI_Request>& reqs) {
  static constexpr size_t chunk_num = chunk_size / sizeof(T);
  if (len <= chunk_num) {
    MPI_Request req;
    isend_small_buffer(ptr, len, dst_worker_id, comm, tag, req);
    reqs.push_back(req);
    return ;
  }
  const size_t chunk_size_in_bytes = chunk_num * sizeof(T);
  int iter = len / chunk_num;
  size_t remaining = (len % chunk_num) * sizeof(T);
  LOG(INFO) << "isending large buffer in " << iter + (remaining != 0) << " iterations";
  for (int i = 0; i < iter; ++i) {
    MPI_Request req;
    MPI_Isend(ptr, chunk_size_in_bytes, MPI_CHAR, dst_worker_id, tag, comm, &req);
    reqs.push_back(req);
    ptr += chunk_num;
  }
  if (remaining != 0) {
    MPI_Request req;
    MPI_Isend(ptr, remaining, MPI_CHAR, dst_worker_id, tag, comm, &req);
    reqs.push_back(req);
  }
}

template <typename T>
static inline void recv_buffer(T* ptr, size_t len, int src_worker_id,
                               MPI_Comm comm, int tag) {
  static constexpr size_t chunk_num = chunk_size / sizeof(T);
  if (len <= chunk_num) {
    recv_small_buffer(ptr, len, src_worker_id, comm, tag);
    return ;
  }
  const size_t chunk_size_in_bytes = chunk_num * sizeof(T);
  int iter = len / chunk_num;
  size_t remaining = (len % chunk_num) * sizeof(T);
  LOG(INFO) << "recving large buffer in " << iter + (remaining != 0) << " iterations";
  for (int i = 0; i < iter; ++i) {
    MPI_Recv(ptr, chunk_size_in_bytes, MPI_CHAR, src_worker_id, tag, comm,
             MPI_STATUS_IGNORE);
    ptr += chunk_num;
  }
  if (remaining != 0) {
    MPI_Recv(ptr, remaining, MPI_CHAR, src_worker_id, tag, comm,
             MPI_STATUS_IGNORE);
  }
}

template <typename T>
static inline void irecv_buffer(T* ptr, size_t len, int src_worker_id,
                                MPI_Comm comm, int tag,
                                std::vector<MPI_Request>& reqs) {
  static constexpr size_t chunk_num = chunk_size / sizeof(T);
  if (len <= chunk_num) {
    MPI_Request req;
    irecv_small_buffer(ptr, len, src_worker_id, comm, tag, req);
    reqs.push_back(req);
    return ;
  }
  const size_t chunk_size_in_bytes = chunk_num * sizeof(T);
  int iter = len / chunk_num;
  size_t remaining = (len % chunk_num) * sizeof(T);
  LOG(INFO) << "irecving large buffer in " << iter + (remaining != 0) << " iterations";
  for (int i = 0; i < iter; ++i) {
    MPI_Request req;
    MPI_Irecv(ptr, chunk_size_in_bytes, MPI_CHAR, src_worker_id, tag, comm,
             &req);
    reqs.push_back(req);
    ptr += chunk_num;
  }
  if (remaining != 0) {
    MPI_Request req;
    MPI_Irecv(ptr, remaining, MPI_CHAR, src_worker_id, tag, comm,
             &req);
    reqs.push_back(req);
  }
}

template <typename T>
static inline void bcast_small_buffer(T* ptr, size_t len, int root, MPI_Comm comm) {
  size_t len_in_bytes = len * sizeof(T);
  assert(len_in_bytes <= chunk_size);
  MPI_Bcast(ptr, len_in_bytes, MPI_CHAR, root, comm);
}

template <typename T>
static inline void bcast_buffer(T* ptr, size_t len, int root, MPI_Comm comm) {
  static constexpr size_t chunk_num = chunk_size / sizeof(T);
  if (len <= chunk_num) {
    bcast_small_buffer(ptr, len, root, comm);
    return ;
  }
  const size_t chunk_size_in_bytes = chunk_num * sizeof(T);
  int iter = len / chunk_num;
  size_t remaining = (len % chunk_num) * sizeof(T);
  LOG(INFO) << "bcast large buffer in " << iter + (remaining != 0) << " iterations";
  for (int i = 0; i < iter; ++i) {
    MPI_Bcast(ptr, chunk_size_in_bytes, MPI_CHAR, root, comm);
    ptr += chunk_num;
  }
  if (remaining != 0) {
    MPI_Bcast(ptr, remaining, MPI_CHAR, root, comm);
  }
}

template <class T, class Enable = void>
struct CommImpl {
  static void send(const T& value, int dst_worker_id, MPI_Comm comm, int tag) {
    InArchive arc;
    arc << value;
    int64_t len = arc.GetSize();
    send_small_buffer<int64_t>(&len, 1, dst_worker_id, comm, tag);
    if (len > 0) {
      send_buffer<char>(arc.GetBuffer(), len, dst_worker_id, comm, tag);
    }
  }

  template <typename ITER_T>
  static void multiple_send(const T& value,
                            const ITER_T& worker_id_begin,
                            const ITER_T& worker_id_end,
                            MPI_Comm comm, int tag) {
    InArchive arc;
    arc << value;
    int64_t len = arc.GetSize();
    for (ITER_T iter = worker_id_begin; iter != worker_id_end; ++iter) {
      int dst_worker_id = *iter;
      send_small_buffer<int64_t>(&len, 1, dst_worker_id, comm, tag);
      if (len > 0) {
        send_buffer<char>(arc.GetBuffer(), len, dst_worker_id, comm, tag);
      }
    }
  }

  static void recv(T& value, int src_worker_id, MPI_Comm comm, int tag) {
    int64_t len;
    recv_small_buffer<int64_t>(&len, 1, src_worker_id, comm, tag);
    if (len > 0) {
      OutArchive arc(len);
      recv_buffer<char>(arc.GetBuffer(), len, src_worker_id, comm, tag);
      arc >> value;
    }
  }

  static void bcast(T& value, int root, MPI_Comm comm) {
    int worker_id;
    MPI_Comm_rank(comm, &worker_id);
    if (worker_id == root) {
      InArchive arc;
      arc << value;
      int64_t len = arc.GetSize();
      bcast_small_buffer<int64_t>(&len, 1, root, comm);
      bcast_buffer<char>(arc.GetBuffer(), arc.GetSize(), root, comm);
    } else {
      int64_t len;
      bcast_small_buffer<int64_t>(&len, 1, root, comm);
      OutArchive arc(len);
      bcast_buffer<char>(arc.GetBuffer(), arc.GetSize(), root, comm);
      arc >> value;
    }
  }
};

template <class T>
struct CommImpl<T, typename std::enable_if<std::is_pod<T>::value>::type> {
  static void send(const T& value, int dst_worker_id, MPI_Comm comm, int tag) {
    send_small_buffer<T>(&value, 1, dst_worker_id, comm, tag);
  }

  static void recv(T& value, int src_worker_id, MPI_Comm comm, int tag) {
    recv_small_buffer<T>(&value, 1, src_worker_id, comm, tag);
  }

  template <typename ITER_T>
  static void multiple_send(const T& value,
                            const ITER_T& worker_id_begin,
                            const ITER_T& worker_id_end,
                            MPI_Comm comm, int tag) {
    for (ITER_T iter = worker_id_begin; iter != worker_id_end; ++iter) {
      int dst_worker_id = *iter;
      send(value, dst_worker_id, comm, tag);
    }
  }

  static void bcast(T& value, int root, MPI_Comm comm) {
    bcast_small_buffer<T>(&value, 1, root, comm);
  }
};

template <class T>
struct CommImpl<std::vector<T>, typename std::enable_if<std::is_pod<T>::value>::type> {
  static void send(const std::vector<T>& vec, int dst_worker_id, MPI_Comm comm, int tag) {
    int64_t len = vec.size();
    CommImpl<int64_t>::send(len, dst_worker_id, comm, tag);
    if (len > 0) {
      send_buffer<T>(vec.data(), vec.size(), dst_worker_id, comm, tag);
    }
  }

  static void send_partial(const std::vector<T>& vec, size_t from, size_t to,
                           int dst_worker_id, MPI_Comm comm, int tag) {
    int64_t len = to - from;
    CommImpl<int64_t>::send(len, dst_worker_id, comm, tag);
    if (len > 0) {
      send_buffer<T>(vec.data() + from, len, dst_worker_id, comm, tag);
    }
  }

  static void recv(std::vector<T>& vec, int src_worker_id, MPI_Comm comm, int tag) {
    int64_t len;
    CommImpl<int64_t>::recv(len, src_worker_id, comm, tag);
    vec.resize(len);
    if (len > 0) {
      recv_buffer<T>(vec.data(), vec.size(), src_worker_id, comm, tag);
    }
  }

  static void recv_at(std::vector<T>& vec, size_t offset, int src_worker_id, MPI_Comm comm, int tag) {
    int64_t len;
    CommImpl<int64_t>::recv(len, src_worker_id, comm, tag);
    if (offset + len > vec.size()) {
      vec.resize(offset + len);
    }
    if (len > 0) {
      recv_buffer<T>(vec.data() + offset, len, src_worker_id, comm, tag);
    }
  }

  template <typename ITER_T>
  static void multiple_send(const std::vector<T>& vec,
                            const ITER_T& worker_id_begin,
                            const ITER_T& worker_id_end,
                            MPI_Comm comm, int tag) {
    for (ITER_T iter = worker_id_begin; iter != worker_id_end; ++iter) {
      int dst_worker_id = *iter;
      send(vec, dst_worker_id, comm, tag);
    }
  }

  static void bcast(std::vector<T>& vec, int root, MPI_Comm comm) {
    int64_t len = vec.size();
    bcast_small_buffer<int64_t>(&len, 1, root, comm);
    vec.resize(len);
    bcast_buffer<T>(vec.data(), len, root, comm);
  }
};

template <>
struct CommImpl<InArchive, void> {
  static void send(const InArchive& arc, int dst_worker_id, MPI_Comm comm, int tag) {
    int64_t len = arc.GetSize();
    CommImpl<int64_t>::send(len, dst_worker_id, comm, tag);
    if (len > 0) {
      send_buffer<char>(arc.GetBuffer(), arc.GetSize(), dst_worker_id, comm, tag);
    }
  }

  static void recv(InArchive& arc, int src_worker_id, MPI_Comm comm, int tag) {
    int64_t len;
    CommImpl<int64_t>::recv(len, src_worker_id, comm, tag);
    arc.Resize(len);
    if (len > 0) {
      recv_buffer<char>(arc.GetBuffer(), len, src_worker_id, comm, tag);
    }
  }

  template <typename ITER_T>
  static void multiple_send(const InArchive& arc,
                            const ITER_T& worker_id_begin,
                            const ITER_T& worker_id_end,
                            MPI_Comm comm, int tag) {
    for (ITER_T iter = worker_id_begin; iter != worker_id_end; ++iter) {
      int dst_worker_id = *iter;
      send(arc, dst_worker_id, comm, tag);
    }
  }

  static void bcast(InArchive& arc, int root, MPI_Comm comm) {
    int64_t len = arc.GetSize();
    bcast_small_buffer<int64_t>(&len, 1, root, comm);
    arc.Resize(len);
    bcast_buffer<char>(arc.GetBuffer(), len, root, comm);
  }
};

template <>
struct CommImpl<OutArchive, void> {
  static void send(const OutArchive& arc, int dst_worker_id, MPI_Comm comm, int tag) {
    int64_t len = arc.GetSize();
    CommImpl<int64_t>::send(len, dst_worker_id, comm, tag);
    if (len > 0) {
      send_buffer<char>(arc.GetBuffer(), arc.GetSize(), dst_worker_id, comm, tag);
    }
  }

  static void recv(OutArchive& arc, int src_worker_id, MPI_Comm comm, int tag) {
    int64_t len;
    CommImpl<int64_t>::recv(len, src_worker_id, comm, tag);
    arc.Clear();
    if (len > 0) {
      arc.Allocate(len);
      recv_buffer<char>(arc.GetBuffer(), len, src_worker_id, comm, tag);
    }
  }

  template <typename ITER_T>
  static void multiple_send(const OutArchive& arc,
                            const ITER_T& worker_id_begin,
                            const ITER_T& worker_id_end,
                            MPI_Comm comm, int tag) {
    for (ITER_T iter = worker_id_begin; iter != worker_id_end; ++iter) {
      int dst_worker_id = *iter;
      send(arc, dst_worker_id, comm, tag);
    }
  }

  static void bcast(OutArchive& arc, int root, MPI_Comm comm) {
    int worker_id;
    MPI_Comm_rank(comm, &worker_id);
    int64_t len = arc.GetSize();
    bcast_small_buffer<int64_t>(&len, 1, root, comm);
    if (root != worker_id) {
      arc.Clear();
      arc.Allocate(len);
    }
    bcast_buffer<char>(arc.GetBuffer(), len, root, comm);
  }
};

template <class T>
struct CommImpl<std::vector<T>, typename std::enable_if<!std::is_pod<T>::value>::type> {
  static void send(const std::vector<T>& vec, int dst_worker_id, MPI_Comm comm, int tag) {
    InArchive arc;
    arc << vec;
    CommImpl<InArchive>::send(arc, dst_worker_id, comm, tag);
  }

  static void send_partial(const std::vector<T>& vec, size_t from, size_t to,
                           int dst_worker_id, MPI_Comm comm, int tag) {
    InArchive arc;
    arc << (to - from);
    while (from != to) {
      arc << vec[from++];
    }
    CommImpl<InArchive>::send(arc, dst_worker_id, comm, tag);
  }

  static void recv(std::vector<T>& vec, int src_worker_id, MPI_Comm comm, int tag) {
    OutArchive arc;
    CommImpl<OutArchive>::recv(arc, src_worker_id, comm, tag);
    arc >> vec;
  }

  static void recv_at(std::vector<T>& vec, size_t offset, int src_worker_id, MPI_Comm comm, int tag) {
    OutArchive arc;
    CommImpl<OutArchive>::recv(arc, src_worker_id, comm, tag);
    size_t num;
    arc >> num;
    if (num + offset > vec.size()) {
      vec.resize(num + offset);
    }
    while (num--) {
      arc >> vec[offset++];
    }
  }

  template <typename ITER_T>
  static void multiple_send(const std::vector<T>& vec,
                            const ITER_T& worker_id_begin,
                            const ITER_T& worker_id_end,
                            MPI_Comm comm, int tag) {
    InArchive arc;
    arc << vec;
    int64_t len = arc.GetSize();
    for (ITER_T iter = worker_id_begin; iter != worker_id_end; ++iter) {
      int dst_worker_id = *iter;
      send_small_buffer<int64_t>(&len, 1, dst_worker_id, comm, tag);
      if (len > 0) {
        send_buffer<char>(arc.GetBuffer(), len, dst_worker_id, comm, tag);
      }
    }
  }

  static void bcast(std::vector<T>& vec, int root, MPI_Comm comm) {
    int worker_id;
    MPI_Comm_rank(comm, &worker_id);
    if (worker_id == root) {
      InArchive arc;
      arc << vec;
      int64_t len = arc.GetSize();
      bcast_small_buffer<int64_t>(&len, 1, root, comm);
      bcast_buffer<char>(arc.GetBuffer(), arc.GetSize(), root, comm);
    } else {
      int64_t len;
      bcast_small_buffer<int64_t>(&len, 1, root, comm);
      OutArchive arc(len);
      bcast_buffer<char>(arc.GetBuffer(), arc.GetSize(), root, comm);
      arc >> vec;
    }
  }
};

template <typename T>
void Send(const T& obj, int dst_worker_id, MPI_Comm comm, int tag) {
  CommImpl<T>::send(obj, dst_worker_id, comm, tag);
}

template <typename T>
void Recv(T& obj, int src_worker_id, MPI_Comm comm, int tag) {
  CommImpl<T>::recv(obj, src_worker_id, comm, tag);
}

template <typename T>
void SendPartial(const std::vector<T>& vec, size_t from, size_t to,
                 int dst_worker_id, MPI_Comm comm, int tag) {
  CommImpl<std::vector<T>>::send_partial(vec, from, to, dst_worker_id, comm, tag);
}

template <typename T>
void RecvAt(std::vector<T>& vec, size_t offset,
            int src_worker_id, MPI_Comm comm, int tag) {
  CommImpl<std::vector<T>>::recv_at(vec, offset, src_worker_id, comm, tag);
}

template <typename T>
void Bcast(T& object, int root, MPI_Comm comm) {
  CommImpl<T>::bcast(object, root, comm);
}

class WorkerIterator {
 public:
  WorkerIterator(int cur, int num) noexcept : cur_(cur), num_(num) {}
  ~WorkerIterator() = default;

  WorkerIterator& operator++() noexcept {
    cur_ = (cur_ + 1) % num_;
    return *this;
  }

  WorkerIterator operator++(int) noexcept {
    int prev = cur_;
    cur_ = (cur_ + 1) % num_;
    return WorkerIterator(prev, num_);
  }

  int operator*() const noexcept {
    return cur_;
  }

  bool operator==(const WorkerIterator& rhs) noexcept {
    return cur_ == rhs.cur_;
  }

  bool operator!=(const WorkerIterator& rhs) noexcept {
    return cur_ != rhs.cur_;
  }

 private:
  int cur_;
  int num_;
};

class ReversedWorkerIterator {
 public:
  ReversedWorkerIterator(int cur, int num) noexcept : cur_(cur), num_(num) {}

  ReversedWorkerIterator& operator++() noexcept {
    cur_ = (cur_ + num_ - 1) % num_;
    return *this;
  }

  ReversedWorkerIterator operator++(int) noexcept {
    int prev = cur_;
    cur_ = (cur_ + num_ - 1) % num_;
    return ReversedWorkerIterator(prev, num_);
  }

  int operator*() const noexcept {
    return cur_;
  }

  bool operator==(const ReversedWorkerIterator& rhs) noexcept {
    return cur_ == rhs.cur_;
  }

  bool operator!=(const ReversedWorkerIterator& rhs) noexcept {
    return cur_ != rhs.cur_;
  }

 private:
  int cur_;
  int num_;
};

template <class T>
inline void AllToAll(const std::vector<T>& out, std::vector<T>& objects,
                     MPI_Comm comm) {
  MPI_Barrier(comm);
  int worker_id, worker_num;
  MPI_Comm_rank(comm, &worker_id);
  MPI_Comm_size(comm, &worker_num);
  std::thread send_thread([&]() {
    InArchive arc;
    for (int i = 1; i < worker_num; ++i) {
      int dst_worker_id = (worker_id + i) % worker_num;
      Send<T>(out[dst_worker_id], dst_worker_id, comm, 0);
    }
  });
  std::thread recv_thread([&]() {
    for (int i = 1; i < worker_num; ++i) {
      int src_worker_id = (worker_id + worker_num - i) % worker_num;
      Recv<T>(objects[src_worker_id], src_worker_id, comm, 0);
    }
  });

  send_thread.join();
  recv_thread.join();
}

template <class T>
inline void AllGather(std::vector<T>& objects, MPI_Comm comm) {
  MPI_Barrier(comm);
  int worker_id, worker_num;
  MPI_Comm_rank(comm, &worker_id);
  MPI_Comm_size(comm, &worker_num);
  std::thread send_thread([&]() {
    CommImpl<T>::multiple_send(objects[worker_id], WorkerIterator((worker_id + 1) % worker_num, worker_num),
                               WorkerIterator(worker_id, worker_num), comm, 0);
  });
  std::thread recv_thread([&]() {
    for (int i = 1; i < worker_num; ++i) {
      int src_worker_id = (worker_id + worker_num - i) % worker_num;
      CommImpl<T>::recv(objects[src_worker_id], src_worker_id, comm, 0);
    }
  });

  send_thread.join();
  recv_thread.join();
}

template <typename T>
inline void FlatAllGather(const std::vector<T>& local, std::vector<T>& global, MPI_Comm comm) {
  MPI_Barrier(comm);
  int worker_id, worker_num;
  MPI_Comm_rank(comm, &worker_id);
  MPI_Comm_size(comm, &worker_num);
  std::vector<int64_t> sizes(worker_num);
  sizes[worker_id] = local.size();
  AllGather<int64_t>(sizes, comm);
  int64_t total_size = 0;
  std::vector<int64_t> offsets;
  for (auto size : sizes) {
    offsets.push_back(total_size);
    total_size += size;
  }
  global.resize(total_size);
  std::thread send_thread([&]() {
    CommImpl<std::vector<T>>::multiple_send(
        local, WorkerIterator((worker_id + 1) % worker_num, worker_num),
        WorkerIterator(worker_id, worker_num), comm, 0);
    std::copy(local.begin(), local.end(), global.begin() + offsets[worker_id]);
  });
  std::thread recv_thread([&]() {
    for (int i = 1; i < worker_num; ++i) {
      int src_worker_id = (worker_id + worker_num - i) % worker_num;
      CommImpl<std::vector<T>>::recv_at(
          global, offsets[src_worker_id], src_worker_id, comm, 0);
    }
  });

  send_thread.join();
  recv_thread.join();
}

bool ArchiveAllToAll(std::vector<InArchive>& out, std::vector<OutArchive>& in, MPI_Comm comm) {
  MPI_Barrier(comm);
  int worker_id, worker_num;
  MPI_Comm_rank(comm, &worker_id);
  MPI_Comm_size(comm, &worker_num);
  std::vector<int64_t> lengths_out(worker_num);
  int64_t total_lengths_out = 0;
  for (int i = 0; i < worker_num; ++i) {
    lengths_out[i] = out[i].GetSize();
    total_lengths_out += lengths_out[i];
  }
  int64_t total_comm_bytes;
  MPI_Allreduce(&total_lengths_out, &total_comm_bytes, 1, MPI_INT64_T, MPI_SUM, comm);
  if (total_comm_bytes == 0) {
    return false;
  }
  std::vector<int64_t> lengths_in(worker_num);
  MPI_Alltoall(lengths_out.data(), 1, MPI_INT64_T, lengths_in.data(), 1, MPI_INT64_T, comm);

  std::vector<MPI_Request> reqs;
  for (int i = 1; i < worker_num; ++i) {
    int dst_worker_id = (worker_id + i) % worker_num;
    isend_buffer(out[dst_worker_id].GetBuffer(), lengths_out[dst_worker_id],
                 dst_worker_id, comm, 0, reqs);
  }
  in.resize(worker_num);
  for (int i = 1; i < worker_num; ++i) {
    int src_worker_id = (worker_id + worker_num - i) % worker_num;
    in[src_worker_id].Allocate(lengths_in[src_worker_id]);
    irecv_buffer(in[src_worker_id].GetBuffer(), lengths_in[src_worker_id],
                 src_worker_id, comm, 0, reqs);
  }
  in[worker_id] = std::move(out[worker_id]);
  if (!reqs.empty()) {
    MPI_Waitall(reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
  }
  return true;
}

}  // namespace sync_comm

}  // namespace grape

#endif  // GRAPE_COMMUNICATION_SYNC_COMM_H_
