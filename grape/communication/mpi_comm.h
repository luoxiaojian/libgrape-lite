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
  MPIComm()
      : comm_(NULL_COMM), rank_(0), size_(1), local_rank_(0), local_size_(1) {}
  MPIComm(MPIComm&& rhs)
      : comm_(rhs.comm_),
        rank_(rhs.rank_),
        size_(rhs.size_),
        reqs_(std::move(rhs.reqs_)),
        bufs_(std::move(rhs.bufs_)),
        local_rank_(rhs.local_rank_),
        local_size_(rhs.local_size_) {
    rhs.comm_ = NULL_COMM;
  }

  MPIComm& operator=(MPIComm&& rhs) {
    if (this == &rhs) {
      return *this;
    }
    if (ValidComm(comm_)) {
      wait_send();
      MPI_Comm_free(&comm_);
    }
    comm_ = rhs.comm_;
    rank_ = rhs.rank_;
    size_ = rhs.size_;
    reqs_ = std::move(rhs.reqs_);
    bufs_ = std::move(rhs.bufs_);
    local_rank_ = rhs.local_rank_;
    local_size_ = rhs.local_size_;

    rhs.comm_ = NULL_COMM;
    rhs.rank_ = 0;
    rhs.size_ = 1;
    rhs.reqs_.clear();
    rhs.bufs_.clear();
    rhs.local_rank_ = 0;
    rhs.local_size_ = 1;

    return *this;
  }

  MPIComm& operator=(const MPIComm& rhs) = delete;

 private:
  MPIComm(MPI_Comm comm, int rank, int size, int local_rank, int local_size)
      : comm_(comm),
        rank_(rank),
        size_(size),
        local_rank_(local_rank),
        local_size_(local_size) {}

 public:
  static constexpr size_t kChunkSize = 1 << 20;
  ~MPIComm() {
    if (ValidComm(comm_)) {
      wait_send();
      MPI_Comm_free(&comm_);
    }
  }

  int rank() const { return rank_; }

  int size() const { return size_; }

  int local_rank() const { return local_rank_; }

  int local_size() const { return local_size_; }

  void barrier() { MPI_Barrier(comm_); }

  void send(int dst, const char* buf, size_t count, int tag) {
    size_t chunk_num = count / kChunkSize;
    for (size_t i = 0; i < chunk_num; ++i) {
      MPI_Send(buf + i * kChunkSize, static_cast<int>(kChunkSize), MPI_CHAR,
               dst, tag, comm_);
    }
    size_t remain = count % kChunkSize;
    MPI_Send(buf + chunk_num * kChunkSize, static_cast<int>(remain), MPI_CHAR,
             dst, tag, comm_);
  }

  void send(int dst, const std::vector<char>& vec, int tag) {
    send(dst, vec.data(), vec.size(), tag);
  }

  void send(int dst, std::vector<char>&& vec, int tag) {
    size_t chunk_num = vec.size() / kChunkSize;
    for (size_t i = 0; i < chunk_num; ++i) {
      MPI_Request req;
      MPI_Isend(vec.data() + i * kChunkSize, static_cast<int>(kChunkSize),
                MPI_CHAR, dst, tag, comm_, &req);
      reqs_.push_back(req);
    }
    size_t remain = vec.size() % kChunkSize;
    MPI_Request req;
    MPI_Isend(vec.data() + chunk_num * kChunkSize, static_cast<int>(remain),
              MPI_CHAR, dst, tag, comm_, &req);
    reqs_.push_back(req);
    bufs_.emplace_back(std::move(vec));
  }

  void send_empty(int dst, int tag) {
    MPI_Request req;
    MPI_Isend(NULL, 0, MPI_CHAR, dst, tag, comm_, &req);
    reqs_.push_back(req);
  }

  void wait_send() {
    if (!reqs_.empty()) {
      MPI_Waitall(static_cast<int>(reqs_.size()), reqs_.data(),
                  MPI_STATUSES_IGNORE);
      reqs_.clear();
      bufs_.clear();
    }
  }

  void recv(int& src, std::vector<char>& vec, int& tag) {
    vec.clear();
    MPI_Status status;
    MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, comm_, &status);
    src = status.MPI_SOURCE;
    tag = status.MPI_TAG;
    while (true) {
      int count;
      MPI_Get_count(&status, MPI_CHAR, &count);
      size_t old_size = vec.size();
      vec.resize(count + old_size);
      MPI_Recv(vec.data() + old_size, count, MPI_CHAR, src, tag, comm_,
               MPI_STATUS_IGNORE);
      if (count < static_cast<int>(kChunkSize)) {
        break;
      }
      MPI_Probe(src, tag, comm_, &status);
    }
  }

  void recv_from(int src, std::vector<char>& vec, int& tag) {
    vec.clear();
    MPI_Status status;
    MPI_Probe(src, MPI_ANY_TAG, comm_, &status);
    tag = status.MPI_TAG;
    while (true) {
      int count;
      MPI_Get_count(&status, MPI_CHAR, &count);
      size_t old_size = vec.size();
      vec.resize(count + old_size);
      MPI_Recv(vec.data() + old_size, count, MPI_CHAR, src, tag, comm_,
               MPI_STATUS_IGNORE);
      if (count < static_cast<int>(kChunkSize)) {
        break;
      }
      MPI_Probe(src, tag, comm_, &status);
    }
  }

  void recv_tagged(int& src, std::vector<char>& vec, int tag) {
    vec.clear();
    MPI_Status status;
    MPI_Probe(MPI_ANY_SOURCE, tag, comm_, &status);
    src = status.MPI_SOURCE;

    while (true) {
      int count;
      MPI_Get_count(&status, MPI_CHAR, &count);
      size_t old_size = vec.size();
      vec.resize(count + old_size);
      MPI_Recv(vec.data() + old_size, count, MPI_CHAR, src, tag, comm_,
               MPI_STATUS_IGNORE);
      if (count < static_cast<int>(kChunkSize)) {
        break;
      }
      MPI_Probe(src, tag, comm_, &status);
    }
  }

  void recv_from_tagged(int src, std::vector<char>& vec, int tag) {
    vec.clear();
    MPI_Status status;
    MPI_Probe(src, tag, comm_, &status);

    while (true) {
      int count;
      MPI_Get_count(&status, MPI_CHAR, &count);
      size_t old_size = vec.size();
      vec.resize(count + old_size);
      MPI_Recv(vec.data() + old_size, count, MPI_CHAR, src, tag, comm_,
               MPI_STATUS_IGNORE);
      if (count < static_cast<int>(kChunkSize)) {
        break;
      }
      MPI_Probe(src, tag, comm_, &status);
    }
  }

  int64_t sum(int64_t input) {
    int64_t ret;
    MPI_Allreduce(&input, &ret, 1, MPI_INT64_T, MPI_SUM, comm_);
    return ret;
  }

  void sum(int64_t* input, int64_t* output, size_t count) {
    size_t chunk_num = count / kChunkSize;
    size_t remain = count % kChunkSize;
    if (input == output) {
      for (size_t i = 0; i < chunk_num; ++i) {
        MPI_Allreduce(MPI_IN_PLACE, output + i * kChunkSize,
                      static_cast<int>(kChunkSize), MPI_INT64_T, MPI_SUM,
                      comm_);
      }
      if (remain > 0) {
        MPI_Allreduce(MPI_IN_PLACE, output + chunk_num * kChunkSize,
                      static_cast<int>(remain), MPI_INT64_T, MPI_SUM, comm_);
      }
    } else {
      for (size_t i = 0; i < chunk_num; ++i) {
        MPI_Allreduce(input + i * kChunkSize, output + i * kChunkSize,
                      static_cast<int>(kChunkSize), MPI_INT64_T, MPI_SUM,
                      comm_);
      }
      if (remain > 0) {
        MPI_Allreduce(input + chunk_num * kChunkSize,
                      output + chunk_num * kChunkSize, static_cast<int>(remain),
                      MPI_INT64_T, MPI_SUM, comm_);
      }
    }
  }

  template <typename T>
  void gather(const T& input, std::vector<T>& output) {
    static const int gather_tag = 10001;
    int worker_num = size();
    int worker_id = rank();
    std::thread send_thread([&]() {
      InArchive arc;
      arc << input;
      for (int i = 1; i < worker_num; ++i) {
        int dst_worker_id = (worker_id + i) % worker_num;
        send(dst_worker_id, arc.GetBuffer(), arc.GetSize(), gather_tag);
      }
    });
    std::thread recv_thread([&]() {
      output.clear();
      output.resize(worker_num);
      output[worker_id] = input;
      for (int i = 1; i < worker_num; ++i) {
        int src_worker_id = (worker_id + worker_num - i) % worker_num;
        std::vector<char> buf;
        recv_from_tagged(src_worker_id, buf, gather_tag);
        OutArchive arc;
        arc.SetSlice(buf.data(), buf.size());
        arc >> output[src_worker_id];
      }
    });
    send_thread.join();
    recv_thread.join();
  }

  template <typename T>
  void all_to_all(const std::vector<T>& input, std::vector<T>& output) {
    static const int all_to_all_tag = 10002;
    int worker_num = size();
    int worker_id = rank();

    CHECK_EQ(input.size(), worker_num);
    output.clear();
    output.resize(worker_num);

    std::thread send_thread([&]() {
      for (int i = 1; i < worker_num; ++i) {
        int dst_worker_id = (worker_id + i) % worker_num;
        InArchive arc;
        arc << input[dst_worker_id];
        send(dst_worker_id, std::move(arc.GetBufferVector()), all_to_all_tag);
      }
    });
    std::thread recv_thread([&]() {
      output[worker_id] = input[worker_id];
      for (int i = 1; i < worker_num; ++i) {
        int src_worker_id = (worker_id + worker_num - i) % worker_num;
        std::vector<char> buf;
        recv_from_tagged(src_worker_id, buf, all_to_all_tag);
        OutArchive arc;
        arc.SetSlice(buf.data(), buf.size());
        arc >> output[src_worker_id];
      }
    });

    send_thread.join();
    recv_thread.join();
  }

  template <class T>
  void bcast(T& val, int root) {
    static const int bcast_tag = 10003;
    InArchive arc;
    arc << val;
    int worker_num = size();
    int worker_id = rank();
    if (worker_id == root) {
      for (int i = 1; i < worker_num; ++i) {
        int dst_worker_id = (worker_id + i) % worker_num;
        send(dst_worker_id, arc.GetBuffer(), arc.GetSize(), bcast_tag);
      }
    } else {
      std::vector<char> buf;
      recv_from_tagged(root, buf, bcast_tag);
      OutArchive arc;
      arc.SetSlice(buf.data(), buf.size());
      arc >> val;
    }
  }

  MPI_Comm comm() const { return comm_; }

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
  MPICommAllocator()
      : comm_(NULL_COMM), rank_(0), size_(1), local_rank_(0), local_size_(1) {}
  ~MPICommAllocator() {
    if (comm_ != NULL_COMM) {
      MPI_Comm_free(&comm_);
    }
    int flag;
    MPI_Initialized(&flag);
    if (flag) {
      MPI_Finalize();
    }
  }

  void init() {
    int flag;
    MPI_Initialized(&flag);
    if (!flag) {
      int provided;
      MPI_Init_thread(NULL, NULL, MPI_THREAD_MULTIPLE, &provided);
    }

    MPI_Comm_dup(MPI_COMM_WORLD, &comm_);
    MPI_Comm_rank(comm_, &rank_);
    MPI_Comm_size(comm_, &size_);

    initLocalInfo();
  }

  static MPICommAllocator& get() {
    static MPICommAllocator allocator;
    return allocator;
  }

  MPIComm allocate() {
    MPI_Comm new_comm;
    MPI_Comm_dup(comm_, &new_comm);
    return MPIComm(new_comm, rank_, size_, local_rank_, local_size_);
  }

  int rank() const { return rank_; }
  int size() const { return size_; }
  int local_rank() const { return local_rank_; }
  int local_size() const { return local_size_; }

 private:
  __attribute__((no_sanitize_address)) void initLocalInfo() {
    char hn[MPI_MAX_PROCESSOR_NAME];
    int hn_len;
    MPI_Get_processor_name(hn, &hn_len);

    char* recv_buf = reinterpret_cast<char*>(calloc(size_, sizeof(hn)));
    MPI_Allgather(hn, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, recv_buf,
                  MPI_MAX_PROCESSOR_NAME, MPI_CHAR, comm_);

    std::vector<std::string> worker_host_names(size_);
    for (int i = 0; i < size_; ++i) {
      worker_host_names[i].assign(
          &recv_buf[i * MPI_MAX_PROCESSOR_NAME],
          strlen(&recv_buf[i * MPI_MAX_PROCESSOR_NAME]));
    }
    free(recv_buf);

    std::map<std::string, int> hostname2id;
    std::vector<int> worker_host_id(size_);
    std::vector<std::vector<int>> host_worker_list;

    for (int i = 0; i < size_; ++i) {
      auto iter = hostname2id.find(worker_host_names[i]);
      if (iter == hostname2id.end()) {
        int new_id = hostname2id.size();
        worker_host_id[i] = new_id;
        hostname2id[worker_host_names[i]] = new_id;

        std::vector<int> vec;
        vec.push_back(i);
        host_worker_list.emplace_back(std::move(vec));
      } else {
        worker_host_id[i] = iter->second;
        host_worker_list[iter->second].push_back(i);
      }
    }

    MPI_Comm local_comm;
    MPI_Comm_split(comm_, worker_host_id[size_], rank_, &local_comm);
    MPI_Comm_size(local_comm, &local_size_);
    MPI_Comm_rank(local_comm, &local_rank_);
  }

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
