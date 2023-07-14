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
#include <mpi.h>

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
  static void send(const T& value, int dst_worker_id, int tag) {
    InArchive arc;
    arc << value;
    CommType::get().send(dst_worker_id, std::move(arc.GetBufferVector()), tag);
  }

  static void recv(T& value, int src_worker_id, int tag) {
    std::vector<char> buf;
    CommType::get().recv_from_tagged(src_worker_id, buf, tag);
    OutArchive arc(std::move(buf));
    arc >> value;
  }
};

template <class T>
struct CommImpl<T, typename std::enable_if<std::is_pod<T>::value>::type> {
  static void send(const T& value, int dst_worker_id, int tag) {
    CommType::get().send(dst_worker_id, reinterpret_cast<const char*>(&value),
                         sizeof(T), tag);
  }

  static void recv(T& value, int src_worker_id, int tag) {
    std::vector<char> buf;
    CommType::get().recv_from_tagged(src_worker_id, buf, tag);
    memcpy(&value, buf.data(), sizeof(T));
  }
};

template <class T>
struct CommImpl<std::vector<T>,
                typename std::enable_if<std::is_pod<T>::value>::type> {
  static void send(const std::vector<T>& vec, int dst_worker_id, int tag) {
    CommType::get().send(dst_worker_id,
                         reinterpret_cast<const char*>(vec.data()),
                         vec.size() * sizeof(T), tag);
  }

  static void recv(std::vector<T>& vec, int src_worker_id, int tag) {
    std::vector<char> buf;
    CommType::get().recv_from_tagged(src_worker_id, buf, tag);
    vec.resize(buf.size() / sizeof(T));
    memcpy(vec.data(), buf.data(), buf.size());
  }
};

template <>
struct CommImpl<InArchive, void> {
  static void send(const InArchive& arc, int dst_worker_id, int tag) {
    CommImpl<std::vector<char>>::send(arc.GetBufferVector(), dst_worker_id,
                                      tag);
  }

  static void recv(InArchive& arc, int src_worker_id, int tag) {
    CommImpl<std::vector<char>>::recv(arc.GetBufferVector(), src_worker_id,
                                      tag);
  }
};

template <>
struct CommImpl<OutArchive, void> {
  static void send(const OutArchive& arc, int dst_worker_id, int tag) {
    CommType::get().send(dst_worker_id, arc.GetBuffer(), arc.GetSize(), tag);
  }

  static void recv(OutArchive& arc, int src_worker_id, int tag) {
    std::vector<char> buf;
    CommType::get().recv_from_tagged(src_worker_id, buf, tag);
    arc = OutArchive(std::move(buf));
  }
};

template <>
struct CommImpl<StringViewVector, void> {
  static void send(const StringViewVector& vec, int dst_worker_id, int tag) {
    CommImpl<std::vector<char>>::send(vec.content_buffer(), dst_worker_id, tag);
    CommImpl<std::vector<size_t>>::send(vec.offset_buffer(), dst_worker_id,
                                        tag);
  }

  static void recv(StringViewVector& vec, int src_worker_id, int tag) {
    CommImpl<std::vector<char>>::recv(vec.content_buffer(), src_worker_id, tag);
    CommImpl<std::vector<size_t>>::recv(vec.offset_buffer(), src_worker_id,
                                        tag);
  }
};

template <class T>
struct CommImpl<std::vector<T>,
                typename std::enable_if<!std::is_pod<T>::value>::type> {
  static void send(const std::vector<T>& vec, int dst_worker_id, int tag) {
    InArchive arc;
    arc << vec;
    CommImpl<InArchive>::send(arc, dst_worker_id, tag);
  }

  static void recv(std::vector<T>& vec, int src_worker_id, int tag) {
    OutArchive arc;
    CommImpl<OutArchive>::recv(arc, src_worker_id, tag);
    arc >> vec;
  }
};

template <typename T>
void Send(const T& obj, int dst_worker_id, int tag) {
  CommImpl<T>::send(obj, dst_worker_id, tag);
}

template <typename T>
void Recv(T& obj, int src_worker_id, int tag) {
  CommImpl<T>::recv(obj, src_worker_id, tag);
}

}  // namespace sync_comm

}  // namespace grape

#endif  // GRAPE_COMMUNICATION_SYNC_COMM_H_
