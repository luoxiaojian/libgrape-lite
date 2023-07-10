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

#ifndef GRAPE_COMMUNICATION_MPI_COMM_H_
#define GRAPE_COMMUNICATION_MPI_COMM_H_

#ifdef USE_MPI

#include "grape/serialization/in_archive.h"
#include "grape/serialization/out_archive.h"

#include <thread>
#include <vector>

#include "mpi.h"

namespace grape {

#ifdef OPEN_MPI
#define NULL_COMM NULL
#else
#define NULL_COMM -1
#endif
#define ValidComm(comm) ((comm) != NULL_COMM)

class MPICommAllocator;

class MPIComm {
 public:
  MPIComm();
  MPIComm(MPIComm&& rhs);

  MPIComm& operator=(MPIComm&& rhs);
  MPIComm& operator=(const MPIComm& rhs) = delete;

 private:
  MPIComm(MPI_Comm comm, int rank, int size, int local_rank, int local_size);

 public:
  static constexpr size_t kChunkSize = 1 << 20;
  ~MPIComm();

  int rank() const;
  int size() const;

  int local_rank() const;

  int local_size() const;

  void barrier();

  void send(int dst, const char* buf, size_t count, int tag);

  void send(int dst, const std::vector<char>& vec, int tag);

  void send(int dst, std::vector<char>&& vec, int tag);

  void send_empty(int dst, int tag);

  void wait_send();

  void recv(int& src, std::vector<char>& vec, int& tag);

  void recv_from(int src, std::vector<char>& vec, int& tag);

  void recv_tagged(int& src, std::vector<char>& vec, int tag);

  void recv_from_tagged(int src, std::vector<char>& vec, int tag);

  int64_t sum(int64_t input);

  void sum(int64_t* input, int64_t* output, size_t count);

  void gather(std::vector<char>&& input,
              std::vector<std::vector<char>>& output);

  void gather(const std::vector<char>& input,
              std::vector<std::vector<char>>& output);

  void all_to_all(std::vector<std::vector<char>>&& input,
                  std::vector<std::vector<char>>& output);

  void all_to_all(const std::vector<std::vector<char>>& input,
                  std::vector<std::vector<char>>& output);

  void bcast(std::vector<char>& val, int root);

  MPI_Comm comm() const;

 private:
  MPI_Comm comm_;
  int rank_;
  int size_;
  std::vector<MPI_Request> reqs_;
  std::vector<std::vector<char>> bufs_;

  int local_rank_;
  int local_size_;

  friend class MPICommAllocator;
};

namespace mpi_comm_ops {

static constexpr int chunk_size = 536870912;

template <typename T>
static inline void isend_small_buffer(const T* ptr, size_t len,
                                      int dst_worker_id, int tag, MPI_Comm comm,
                                      MPI_Request& req) {
  size_t len_in_bytes = len * sizeof(T);
  assert(len_in_bytes <= chunk_size);
  MPI_Isend(ptr, len_in_bytes, MPI_CHAR, dst_worker_id, tag, comm, &req);
}

template <typename T>
static inline void irecv_small_buffer(T* ptr, size_t len, int src_worker_id,
                                      int tag, MPI_Comm comm,
                                      MPI_Request& req) {
  size_t len_in_bytes = len * sizeof(T);
  assert(len_in_bytes <= chunk_size);
  MPI_Irecv(ptr, len_in_bytes, MPI_CHAR, src_worker_id, tag, comm, &req);
}

template <typename T>
static inline void isend_buffer(const T* ptr, size_t len, int dst_worker_id,
                                int tag, MPI_Comm comm,
                                std::vector<MPI_Request>& reqs) {
  static constexpr size_t chunk_num = chunk_size / sizeof(T);
  if (len <= chunk_num) {
    MPI_Request req;
    isend_small_buffer(ptr, len, dst_worker_id, tag, comm, req);
    reqs.push_back(req);
    return;
  }
  const size_t chunk_size_in_bytes = chunk_num * sizeof(T);
  int iter = len / chunk_num;
  size_t remaining = (len % chunk_num) * sizeof(T);
  LOG(INFO) << "isending large buffer in " << iter + (remaining != 0)
            << " iterations";
  for (int i = 0; i < iter; ++i) {
    MPI_Request req;
    MPI_Isend(ptr, chunk_size_in_bytes, MPI_CHAR, dst_worker_id, tag, comm,
              &req);
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
static inline int chunk_num(size_t len) {
  static constexpr size_t chunk_num = chunk_size / sizeof(T);
  return (len + chunk_num - 1) / chunk_num;
}

template <typename T>
static inline void irecv_buffer(T* ptr, size_t len, int src_worker_id, int tag,
                                MPI_Comm comm, MPI_Request* reqs) {
  static constexpr size_t chunk_num = chunk_size / sizeof(T);
  if (len <= chunk_num) {
    irecv_small_buffer(ptr, len, src_worker_id, tag, comm, reqs[0]);
    return;
  }
  const size_t chunk_size_in_bytes = chunk_num * sizeof(T);
  int iter = len / chunk_num;
  size_t remaining = (len % chunk_num) * sizeof(T);
  LOG(INFO) << "irecving large buffer in " << iter + (remaining != 0)
            << " iterations";
  for (int i = 0; i < iter; ++i) {
    MPI_Irecv(ptr, chunk_size_in_bytes, MPI_CHAR, src_worker_id, tag, comm,
              &reqs[i]);
    ptr += chunk_num;
  }
  if (remaining != 0) {
    MPI_Irecv(ptr, remaining, MPI_CHAR, src_worker_id, tag, comm, &reqs[iter]);
  }
}

template <typename T>
static inline void irecv_buffer(T* ptr, size_t len, int src_worker_id, int tag,
                                MPI_Comm comm, std::vector<MPI_Request>& reqs) {
  static constexpr size_t chunk_num = chunk_size / sizeof(T);
  if (len <= chunk_num) {
    MPI_Request req;
    irecv_small_buffer(ptr, len, src_worker_id, tag, comm, req);
    reqs.push_back(req);
    return;
  }
  const size_t chunk_size_in_bytes = chunk_num * sizeof(T);
  int iter = len / chunk_num;
  size_t remaining = (len % chunk_num) * sizeof(T);
  LOG(INFO) << "irecving large buffer in " << iter + (remaining != 0)
            << " iterations";
  for (int i = 0; i < iter; ++i) {
    MPI_Request req;
    MPI_Irecv(ptr, chunk_size_in_bytes, MPI_CHAR, src_worker_id, tag, comm,
              &req);
    reqs.push_back(req);
    ptr += chunk_num;
  }
  if (remaining != 0) {
    MPI_Request req;
    MPI_Irecv(ptr, remaining, MPI_CHAR, src_worker_id, tag, comm, &req);
    reqs.push_back(req);
  }
}

}  // namespace mpi_comm_ops

class MPICommAllocator {
 public:
  MPICommAllocator();
  ~MPICommAllocator();

  void init();

  static MPICommAllocator& get() {
    static MPICommAllocator allocator;
    return allocator;
  }

  MPIComm allocate();

  int rank() const;
  int size() const;
  int local_rank() const;
  int local_size() const;

 private:
  __attribute__((no_sanitize_address)) void initLocalInfo();

  MPI_Comm comm_;
  int rank_;
  int size_;

  int local_rank_;
  int local_size_;
};

using CommAllocatorType = MPICommAllocator;
using CommType = MPIComm;

}  // namespace grape

#endif

#endif  // GRAPE_COMMUNICATION_MPI_COMM_H_
