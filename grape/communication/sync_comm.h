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

static const int chunk_size = 409600;

template <typename T>
inline void send_buffer(const T* ptr, size_t len, int dst_worker_id,
                        MPI_Comm comm, int tag) {
  const size_t chunk_size_in_bytes = chunk_size * sizeof(T);
  int iter = len / chunk_size;
  size_t remaining = (len % chunk_size) * sizeof(T);
  for (int i = 0; i < iter; ++i) {
    MPI_Send(ptr, chunk_size_in_bytes, MPI_CHAR, dst_worker_id, tag, comm);
    ptr += chunk_size;
  }
  if (remaining != 0) {
    MPI_Send(ptr, remaining, MPI_CHAR, dst_worker_id, tag, comm);
  }
}

template <typename T>
inline void recv_buffer(T* ptr, size_t len, int src_worker_id, MPI_Comm comm,
                        int tag) {
  const size_t chunk_size_in_bytes = chunk_size * sizeof(T);
  int iter = len / chunk_size;
  size_t remaining = (len % chunk_size) * sizeof(T);
  for (int i = 0; i < iter; ++i) {
    MPI_Recv(ptr, chunk_size_in_bytes, MPI_CHAR, src_worker_id, tag, comm,
             MPI_STATUS_IGNORE);
    ptr += chunk_size;
  }
  if (remaining != 0) {
    MPI_Recv(ptr, remaining, MPI_CHAR, src_worker_id, tag, comm,
             MPI_STATUS_IGNORE);
  }
}

template <typename T>
inline void all_gather_buffers(const T* buffer, size_t size,
                               std::vector<std::pair<T*, size_t>>& recv_buffers,
                               MPI_Comm comm, bool to_self = true) {
  int worker_id, worker_num;
  MPI_Comm_rank(comm, &worker_id);
  MPI_Comm_size(comm, &worker_num);
  std::thread send_thread([&]() {
    for (int i = 1; i < worker_num; ++i) {
      int dst_worker_id = (worker_id + i) % worker_num;
      send_buffer<T>(buffer, size, dst_worker_id, comm, 0);
    }
  });
  std::thread recv_thread([&]() {
    for (int i = 1; i < worker_num; ++i) {
      int src_worker_id = (worker_id + worker_num - i) % worker_num;
      recv_buffer<T>(recv_buffers[src_worker_id].first,
                     recv_buffers[src_worker_id].second, src_worker_id, comm,
                     0);
    }
    if (to_self) {
      memcpy(recv_buffers[worker_id].first, buffer, size * sizeof(T));
    }
  });
  recv_thread.join();
  send_thread.join();
}

template <typename T>
inline void all_to_all_buffers(
    std::vector<std::pair<const T*, size_t>>& send_buffers,
    std::vector<std::pair<T*, size_t>>& recv_buffers, MPI_Comm comm) {
  int worker_id, worker_num;
  MPI_Comm_rank(comm, &worker_id);
  MPI_Comm_size(comm, &worker_num);
  std::thread send_thread([&]() {
    for (int i = 1; i < worker_num; ++i) {
      int dst_worker_id = (worker_id + i) % worker_num;
      send_buffer<T>(send_buffers[dst_worker_id].first,
                     send_buffers[dst_worker_id].second, dst_worker_id, comm,
                     0);
    }
  });
  std::thread recv_thread([&]() {
    for (int i = 1; i < worker_num; ++i) {
      int src_worker_id = (worker_id + worker_num - i) % worker_num;
      recv_buffer<T>(recv_buffers[src_worker_id].first,
                     recv_buffers[src_worker_id].second, src_worker_id, comm,
                     0);
    }
    memcpy(recv_buffers[worker_id].first, send_buffers[worker_id].first,
           recv_buffers[worker_id].second * sizeof(T));
  });
  recv_thread.join();
  send_thread.join();
}

template <typename T>
inline void SendVector(const std::vector<T>& vec, int dst_worker_id,
                       MPI_Comm comm, int tag = 0) {
  size_t len = vec.size();
  MPI_Send(&len, sizeof(size_t), MPI_CHAR, dst_worker_id, tag, comm);
  send_buffer<T>(&vec[0], len, dst_worker_id, comm, tag);
}

template <typename T>
inline void RecvVector(std::vector<T>& vec, int src_worker_id, MPI_Comm comm,
                       int tag = 0) {
  size_t len;
  MPI_Recv(&len, sizeof(size_t), MPI_CHAR, src_worker_id, tag, comm,
           MPI_STATUS_IGNORE);
  vec.resize(len);
  recv_buffer<T>(&vec[0], len, src_worker_id, comm, tag);
}

inline void SendArchive(const InArchive& archive, int dst_worker_id,
                        MPI_Comm comm, int tag = 0) {
  size_t len = archive.GetSize();
  MPI_Send(&len, sizeof(size_t), MPI_CHAR, dst_worker_id, tag, comm);
  int iter = len / chunk_size;
  int remaining = len % chunk_size;
  const char* ptr = archive.GetBuffer();
  for (int i = 0; i < iter; ++i) {
    MPI_Send(ptr, chunk_size, MPI_CHAR, dst_worker_id, tag, comm);
    ptr += chunk_size;
  }
  if (remaining != 0) {
    MPI_Send(ptr, remaining, MPI_CHAR, dst_worker_id, tag, comm);
  }
}

inline void RecvArchive(OutArchive& archive, int src_worker_id, MPI_Comm comm,
                        int tag = 0) {
  size_t len;
  MPI_Recv(&len, sizeof(size_t), MPI_CHAR, src_worker_id, tag, comm,
           MPI_STATUS_IGNORE);
  archive.Clear();
  archive.Allocate(len);
  int iter = len / chunk_size;
  int remaining = len % chunk_size;
  char* ptr = archive.GetBuffer();
  for (int i = 0; i < iter; ++i) {
    MPI_Recv(ptr, chunk_size, MPI_CHAR, src_worker_id, tag, comm,
             MPI_STATUS_IGNORE);
    ptr += chunk_size;
  }
  if (remaining != 0) {
    MPI_Recv(ptr, remaining, MPI_CHAR, src_worker_id, tag, comm,
             MPI_STATUS_IGNORE);
  }
}

template <class T>
void BcastSend(const T& object, MPI_Comm comm) {
  InArchive ia;
  ia << object;
  size_t buf_size = ia.GetSize();
  int root;
  MPI_Comm_rank(comm, &root);
  MPI_Bcast(&buf_size, sizeof(size_t), MPI_CHAR, root, comm);
  CHECK_LT(buf_size, std::numeric_limits<int>::max());
  MPI_Bcast(ia.GetBuffer(), buf_size, MPI_CHAR, root, comm);
}

template <class T>
void BcastRecv(T& object, MPI_Comm comm, int root) {
  size_t buf_size;
  MPI_Bcast(&buf_size, sizeof(size_t), MPI_CHAR, root, comm);
  OutArchive oa(buf_size);
  CHECK_LT(buf_size, std::numeric_limits<int>::max());
  MPI_Bcast(oa.GetBuffer(), buf_size, MPI_CHAR, root, comm);
  oa >> object;
}

template <class T>
inline void AllToAll(const std::vector<T>& out, std::vector<T>& objects,
                     MPI_Comm comm) {
  int worker_id, worker_num;
  MPI_Comm_rank(comm, &worker_id);
  MPI_Comm_size(comm, &worker_num);
  std::thread send_thread([&]() {
    InArchive arc;
    for (int i = 1; i < worker_num; ++i) {
      int dst_worker_id = (worker_id + i) % worker_num;
      arc.Clear();
      arc << out[dst_worker_id];
      SendArchive(arc, dst_worker_id, comm);
    }
  });
  std::thread recv_thread([&]() {
    for (int i = 1; i < worker_num; ++i) {
      int src_worker_id = (worker_id + worker_num - i) % worker_num;
      OutArchive arc;
      RecvArchive(arc, src_worker_id, comm);
      arc >> objects[src_worker_id];
    }
  });

  send_thread.join();
  recv_thread.join();
}

template <class T>
inline void AllGather(std::vector<T>& objects, MPI_Comm comm) {
  int worker_id, worker_num;
  MPI_Comm_rank(comm, &worker_id);
  MPI_Comm_size(comm, &worker_num);
  std::thread send_thread([&]() {
    InArchive arc;
    arc << objects[worker_id];
    for (int i = 1; i < worker_num; ++i) {
      int dst_worker_id = (worker_id + i) % worker_num;
      SendArchive(arc, dst_worker_id, comm);
    }
  });
  std::thread recv_thread([&]() {
    for (int i = 1; i < worker_num; ++i) {
      int src_worker_id = (worker_id + worker_num - i) % worker_num;
      OutArchive arc;
      RecvArchive(arc, src_worker_id, comm);
      arc >> objects[src_worker_id];
    }
  });

  send_thread.join();
  recv_thread.join();
}

template <typename T>
inline typename std::enable_if<std::is_pod<T>::value>::type AllGatherList(
    const std::vector<T>& local, std::vector<T>& global, MPI_Comm comm) {
  int worker_id, worker_num;
  MPI_Comm_rank(comm, &worker_id);
  MPI_Comm_size(comm, &worker_num);
  uint64_t local_size = sizeof(T) * local.size();
  std::vector<uint64_t> sizes(worker_num);
  MPI_Allgather(&local_size, 1, MPI_UINT64_T, sizes.data(), 1, MPI_UINT64_T,
                comm);
  uint64_t total_size = 0;
  for (auto s : sizes) {
    total_size += s;
  }
  global.resize(total_size / sizeof(T));
  uint64_t threshold = static_cast<uint64_t>(std::numeric_limits<int>::max());
  if (total_size >= threshold) {
    std::vector<std::pair<char*, size_t>> recv_buffers(worker_num);
    char* ptr = reinterpret_cast<char*>(global.data());
    for (int i = 0; i < worker_num; ++i) {
      recv_buffers[i].first = ptr;
      recv_buffers[i].second = sizes[i];
      ptr += sizes[i];
    }
    all_gather_buffers<char>(reinterpret_cast<const char*>(local.data()),
                             local_size, recv_buffers, comm);
  } else {
    std::vector<int> counts(worker_num);
    std::vector<int> displs(worker_num);
    int cur_size = 0;
    for (int i = 0; i < worker_num; ++i) {
      counts[i] = static_cast<int>(sizes[i]);
      displs[i] = cur_size;
      cur_size += counts[i];
    }
    MPI_Allgatherv(local.data(), local_size, MPI_CHAR, global.data(),
                   counts.data(), displs.data(), MPI_CHAR, comm);
  }
}

template <typename T>
inline typename std::enable_if<!std::is_pod<T>::value>::type AllGatherList(
    const std::vector<T>& local, std::vector<T>& global, MPI_Comm comm) {
  int worker_id, worker_num;
  MPI_Comm_rank(comm, &worker_id);
  MPI_Comm_size(comm, &worker_num);
  InArchive local_arc;
  for (auto& v : local) {
    local_arc << v;
  }
  uint64_t local_sizes[2];
  local_sizes[0] = local.size();
  local_sizes[1] = local_arc.GetSize();
  std::vector<uint64_t> sizes(2 * worker_num);
  MPI_Allgather(local_sizes, 2, MPI_UINT64_T, sizes.data(), 2, MPI_UINT64_T,
                comm);
  std::vector<OutArchive> received_arcs(worker_num);
  std::vector<std::pair<char*, size_t>> recv_buffers(worker_num);
  size_t total_size = 0;
  for (int i = 0; i < worker_num; ++i) {
    total_size += sizes[i * 2];
    if (i == worker_id) {
      continue;
    }
    received_arcs[i].Allocate(sizes[i * 2 + 1]);
    recv_buffers[i].first = received_arcs[i].GetBuffer();
    recv_buffers[i].second = received_arcs[i].GetSize();
  }
  all_gather_buffers(local_arc.GetBuffer(), local_arc.GetSize(), recv_buffers,
                     comm, false);
  global.resize(total_size);
  T* ptr = global.data();
  for (int i = 0; i < worker_num; ++i) {
    size_t num = sizes[i * 2];
    if (i == worker_id) {
      std::copy(local.data(), local.data() + num, ptr);
    } else {
      for (size_t k = 0; k < num; ++k) {
        received_arcs[i] >> ptr[k];
      }
    }
    ptr += num;
  }
}

}  // namespace grape

#endif  // GRAPE_COMMUNICATION_SYNC_COMM_H_
