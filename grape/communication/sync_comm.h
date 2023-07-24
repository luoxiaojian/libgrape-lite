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

#include <assert.h>
#include <glog/logging.h>

#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "grape/communication/comm.h"
#include "grape/serialization/in_archive.h"
#include "grape/serialization/out_archive.h"
#include "grape/utils/string_view_vector.h"

namespace grape {

namespace sync_comm {

template <class T, class Enable = void>
struct CommImpl {
  static void send(CommType& comm, const T& value, int dst_worker_id, int tag) {
    InArchive arc;
    arc << value;
    comm.send(dst_worker_id, std::move(arc.GetBufferVector()), tag);
  }

  static void recv(CommType& comm, T& value, int src_worker_id, int tag) {
    std::vector<char> buf;
    comm.recv_from_tagged(src_worker_id, buf, tag);
    OutArchive arc(std::move(buf));
    arc >> value;
  }
};

template <class T>
struct CommImpl<T, typename std::enable_if<std::is_pod<T>::value>::type> {
  static void send(CommType& comm, const T& value, int dst_worker_id, int tag) {
    comm.send(dst_worker_id, reinterpret_cast<const char*>(&value), sizeof(T),
              tag);
  }

  static void recv(CommType& comm, T& value, int src_worker_id, int tag) {
    std::vector<char> buf;
    comm.recv_from_tagged(src_worker_id, buf, tag);
    memcpy(&value, buf.data(), sizeof(T));
  }
};

template <class T>
struct CommImpl<std::vector<T>,
                typename std::enable_if<std::is_pod<T>::value>::type> {
  static void send(CommType& comm, const std::vector<T>& vec, int dst_worker_id,
                   int tag) {
    comm.send(dst_worker_id, reinterpret_cast<const char*>(vec.data()),
              vec.size() * sizeof(T), tag);
  }

  static void recv(CommType& comm, std::vector<T>& vec, int src_worker_id,
                   int tag) {
    std::vector<char> buf;
    comm.recv_from_tagged(src_worker_id, buf, tag);
    vec.resize(buf.size() / sizeof(T));
    memcpy(vec.data(), buf.data(), buf.size());
  }
};

template <>
struct CommImpl<InArchive, void> {
  static void send(CommType& comm, const InArchive& arc, int dst_worker_id,
                   int tag) {
    CommImpl<std::vector<char>>::send(comm, arc.GetBufferVector(),
                                      dst_worker_id, tag);
  }

  static void recv(CommType& comm, InArchive& arc, int src_worker_id, int tag) {
    CommImpl<std::vector<char>>::recv(comm, arc.GetBufferVector(),
                                      src_worker_id, tag);
  }
};

template <>
struct CommImpl<OutArchive, void> {
  static void send(CommType& comm, const OutArchive& arc, int dst_worker_id,
                   int tag) {
    comm.send(dst_worker_id, arc.GetBuffer(), arc.GetSize(), tag);
  }

  static void recv(CommType& comm, OutArchive& arc, int src_worker_id,
                   int tag) {
    std::vector<char> buf;
    comm.recv_from_tagged(src_worker_id, buf, tag);
    arc = OutArchive(std::move(buf));
  }
};

template <>
struct CommImpl<StringViewVector, void> {
  static void send(CommType& comm, const StringViewVector& vec,
                   int dst_worker_id, int tag) {
    CommImpl<std::vector<char>>::send(comm, vec.content_buffer(), dst_worker_id,
                                      tag);
    CommImpl<std::vector<size_t>>::send(comm, vec.offset_buffer(),
                                        dst_worker_id, tag);
  }

  static void recv(CommType& comm, StringViewVector& vec, int src_worker_id,
                   int tag) {
    CommImpl<std::vector<char>>::recv(comm, vec.content_buffer(), src_worker_id,
                                      tag);
    CommImpl<std::vector<size_t>>::recv(comm, vec.offset_buffer(),
                                        src_worker_id, tag);
  }
};

template <class T>
struct CommImpl<std::vector<T>,
                typename std::enable_if<!std::is_pod<T>::value>::type> {
  static void send(CommType& comm, const std::vector<T>& vec, int dst_worker_id,
                   int tag) {
    InArchive arc;
    arc << vec;
    CommImpl<InArchive>::send(comm, arc, dst_worker_id, tag);
  }

  static void recv(CommType& comm, std::vector<T>& vec, int src_worker_id,
                   int tag) {
    OutArchive arc;
    CommImpl<OutArchive>::recv(comm, arc, src_worker_id, tag);
    arc >> vec;
  }
};

template <typename T>
void Send(CommType& comm, const T& obj, int dst_worker_id, int tag) {
  CommImpl<T>::send(comm, obj, dst_worker_id, tag);
}

template <typename T>
void Recv(CommType& comm, T& obj, int src_worker_id, int tag) {
  CommImpl<T>::recv(comm, obj, src_worker_id, tag);
}

}  // namespace sync_comm

}  // namespace grape

#endif  // GRAPE_COMMUNICATION_SYNC_COMM_H_
