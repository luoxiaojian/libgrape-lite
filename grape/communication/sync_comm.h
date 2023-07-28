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

  static void bcast(CommType& comm, T& value, int root_worker_id) {
    std::vector<char> buf;
    if (comm.rank() == root_worker_id) {
      InArchive arc;
      arc << value;
      buf = arc.GetBufferVector();
    }
    comm.bcast(buf, root_worker_id);
    if (comm.rank() != root_worker_id) {
      OutArchive arc(std::move(buf));
      arc >> value;
    }
  }

  static void gather(CommType& comm, const T& value, std::vector<T>& vec) {
    vec.clear();
    vec.resize(comm.size());
    InArchive arc;
    arc << value;
    std::vector<char> buf = std::move(arc.GetBufferVector());
    std::vector<std::vector<char>> buf_vec;
    comm.gather(std::move(buf), buf_vec);

    for (int i = 0; i < comm.size(); ++i) {
      OutArchive arc(std::move(buf_vec[i]));
      arc >> vec[i];
    }
  }

  static void all_to_all(CommType& comm, const std::vector<T>& input,
                         std::vector<T>& output) {
    CHECK_EQ(comm.size(), input.size());
    std::vector<std::vector<char>> input_buf_vec;
    for (int i = 0; i < comm.size(); ++i) {
      InArchive arc;
      arc << input[i];
      input_buf_vec.emplace_back(std::move(arc.GetBufferVector()));
    }
    std::vector<std::vector<char>> output_buf_vec;
    comm.all_to_all(input_buf_vec, output_buf_vec);
    output.clear();
    output.resize(comm.size());
    for (int i = 0; i < comm.size(); ++i) {
      OutArchive arc;
      arc.SetSlice(output_buf_vec[i].data(), output_buf_vec[i].size());
      arc >> output[i];
    }
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

  static void bcast(CommType& comm, T& value, int root_worker_id) {
    std::vector<char> buf(sizeof(T));
    if (comm.rank() == root_worker_id) {
      memcpy(buf.data(), &value, sizeof(T));
    }
    comm.bcast(buf, root_worker_id);
    if (comm.rank() != root_worker_id) {
      memcpy(&value, buf.data(), sizeof(T));
    }
  }

  static void gather(CommType& comm, const T& value, std::vector<T>& vec) {
    std::vector<char> input_buf(sizeof(T));
    memcpy(input_buf.data(), &value, sizeof(T));
    std::vector<std::vector<char>> buf_vec;
    comm.gather(std::move(input_buf), buf_vec);

    vec.clear();
    vec.resize(comm.size());

    for (int i = 0; i < comm.size(); ++i) {
      memcpy(&vec[i], buf_vec[i].data(), sizeof(T));
    }
  }

  static void all_to_all(CommType& comm, const std::vector<T>& input,
                         std::vector<T>& output) {
    CHECK_EQ(comm.size(), input.size());
    std::vector<std::vector<char>> input_buf_vec;
    for (int i = 0; i < comm.size(); ++i) {
      input_buf_vec[i].resize(sizeof(T));
      memcpy(input_buf_vec[i].data(), &input[i], sizeof(T));
    }
    std::vector<std::vector<char>> output_buf_vec;
    comm.all_to_all(input_buf_vec, output_buf_vec);
    output.clear();
    output.resize(comm.size());
    for (int i = 0; i < comm.size(); ++i) {
      memcpy(&output[i], output_buf_vec[i].data(), sizeof(T));
    }
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

  static void bcast(CommType& comm, std::vector<T>& vec, int root_worker_id) {
    std::vector<char> buf;
    if (comm.rank() == root_worker_id) {
      buf.resize(vec.size() * sizeof(T));
      memcpy(buf.data(), vec.data(), buf.size());
    }
    comm.bcast(buf, root_worker_id);
    if (comm.rank() != root_worker_id) {
      vec.resize(buf.size() / sizeof(T));
      memcpy(vec.data(), buf.data(), buf.size());
    }
  }

  static void gather(CommType& comm, const std::vector<T>& value,
                     std::vector<std::vector<T>>& vec) {
    std::vector<char> input_buf(sizeof(T) * value.size());
    memcpy(input_buf.data(), value.data(), sizeof(T) * value.size());
    std::vector<std::vector<char>> buf_vec;
    comm.gather(std::move(input_buf), buf_vec);

    vec.clear();
    vec.resize(comm.size());

    for (int i = 0; i < comm.size(); ++i) {
      vec[i].resize(buf_vec[i].size() / sizeof(T));
      memcpy(vec[i].data(), buf_vec[i].data(), buf_vec[i].size());
    }
  }

  static void all_to_all(CommType& comm,
                         const std::vector<std::vector<T>>& input,
                         std::vector<std::vector<T>>& output) {
    std::vector<std::vector<char>> input_buf_vec;
    for (int i = 0; i < comm.size(); ++i) {
      input_buf_vec[i].resize(sizeof(T) * input[i].size());
      memcpy(input_buf_vec[i].data(), input[i].data(),
             sizeof(T) * input[i].size());
    }
    std::vector<std::vector<char>> output_buf_vec;
    comm.all_to_all(input_buf_vec, output_buf_vec);
    output.clear();
    output.resize(comm.size());
    for (int i = 0; i < comm.size(); ++i) {
      output[i].resize(output_buf_vec[i].size() / sizeof(T));
      memcpy(output[i].data(), output_buf_vec[i].data(),
             output_buf_vec[i].size());
    }
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

  static void bcast(CommType& comm, InArchive& arc, int root_worker_id) {
    CommImpl<std::vector<char>>::bcast(comm, arc.GetBufferVector(),
                                       root_worker_id);
  }

  static void gather(CommType& comm, const InArchive& in,
                     std::vector<InArchive>& out) {
    std::vector<std::vector<char>> buf_vec;
    CommImpl<std::vector<char>>::gather(comm, in.GetBufferVector(), buf_vec);
    out.clear();
    out.resize(comm.size());
    for (int i = 0; i < comm.size(); ++i) {
      out[i].GetBufferVector() = std::move(buf_vec[i]);
    }
  }

  static void all_to_all(CommType& comm, const std::vector<InArchive>& in,
                         std::vector<InArchive>& out) {
    std::vector<std::vector<char>> input_buf_vec;
    for (int i = 0; i < comm.size(); ++i) {
      input_buf_vec.emplace_back(in[i].GetBufferVector());
    }
    std::vector<std::vector<char>> output_buf_vec;
    comm.all_to_all(input_buf_vec, output_buf_vec);
    out.clear();
    out.resize(comm.size());
    for (int i = 0; i < comm.size(); ++i) {
      out[i].GetBufferVector() = std::move(output_buf_vec[i]);
    }
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

  static void bcast(CommType& comm, OutArchive& arc, int root_worker_id) {
    std::vector<char> buf;
    if (comm.rank() == root_worker_id) {
      buf.resize(arc.GetSize());
      memcpy(buf.data(), arc.GetBuffer(), buf.size());
    }
    comm.bcast(buf, root_worker_id);
    if (comm.rank() != root_worker_id) {
      arc = OutArchive(std::move(buf));
    }
  }

  static void gather(CommType& comm, const OutArchive& in,
                     std::vector<OutArchive>& out) {
    std::vector<std::vector<char>> buf_vec;
    std::vector<char> in_buf(in.GetSize());
    memcpy(in_buf.data(), in.GetBuffer(), in.GetSize());
    CommImpl<std::vector<char>>::gather(comm, in_buf, buf_vec);
    out.clear();
    for (int i = 0; i < comm.size(); ++i) {
      out.push_back(OutArchive(std::move(buf_vec[i])));
    }
  }

  static void all_to_all(CommType& comm, const std::vector<OutArchive>& in,
                         std::vector<OutArchive>& out) {
    std::vector<std::vector<char>> input_buf_vec;
    for (int i = 0; i < comm.size(); ++i) {
      std::vector<char> buf(in[i].GetSize());
      memcpy(buf.data(), in[i].GetBuffer(), in[i].GetSize());
      input_buf_vec.emplace_back(std::move(buf));
    }
    std::vector<std::vector<char>> output_buf_vec;
    comm.all_to_all(input_buf_vec, output_buf_vec);
    out.clear();
    out.resize(comm.size());
    for (int i = 0; i < comm.size(); ++i) {
      out[i] = OutArchive(std::move(output_buf_vec[i]));
    }
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

  static void bcast(CommType& comm, StringViewVector& vec, int root) {
    CommImpl<std::vector<char>>::bcast(comm, vec.content_buffer(), root);
    CommImpl<std::vector<size_t>>::bcast(comm, vec.offset_buffer(), root);
  }

  static void gather(CommType& comm, const StringViewVector& in,
                     std::vector<StringViewVector>& out) {
    std::vector<std::vector<char>> content_buf_vec;
    std::vector<std::vector<size_t>> offset_buf_vec;
    CommImpl<std::vector<char>>::gather(comm, in.content_buffer(),
                                        content_buf_vec);
    CommImpl<std::vector<size_t>>::gather(comm, in.offset_buffer(),
                                          offset_buf_vec);
    out.clear();
    out.resize(comm.size());
    for (int i = 0; i < comm.size(); ++i) {
      out[i].content_buffer() = std::move(content_buf_vec[i]);
      out[i].offset_buffer() = std::move(offset_buf_vec[i]);
    }
  }

  static void all_to_all(CommType& comm,
                         const std::vector<StringViewVector>& in,
                         std::vector<StringViewVector>& out) {
    std::vector<std::vector<char>> content_buf_vec;
    std::vector<std::vector<size_t>> offset_buf_vec;
    for (int i = 0; i < comm.size(); ++i) {
      content_buf_vec.emplace_back(in[i].content_buffer());
      offset_buf_vec.emplace_back(in[i].offset_buffer());
    }

    std::vector<std::vector<char>> output_content_buf_vec;
    std::vector<std::vector<size_t>> output_offset_buf_vec;

    CommImpl<std::vector<char>>::all_to_all(comm, content_buf_vec,
                                            output_content_buf_vec);
    CommImpl<std::vector<size_t>>::all_to_all(comm, offset_buf_vec,
                                              output_offset_buf_vec);

    out.clear();
    out.resize(comm.size());
    for (int i = 0; i < comm.size(); ++i) {
      out[i].content_buffer() = std::move(output_content_buf_vec[i]);
      out[i].offset_buffer() = std::move(output_offset_buf_vec[i]);
    }
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

  static void bcast(CommType& comm, std::vector<T>& vec, int root) {
    InArchive arc;
    if (comm.rank() == root) {
      arc << vec;
    }
    CommImpl<InArchive>::bcast(comm, arc, root);
    if (comm.rank() != root) {
      OutArchive oarc;
      oarc.SetSlice(arc.GetBuffer(), arc.GetSize());
      oarc >> vec;
    }
  }

  static void gather(CommType& comm, const std::vector<T>& in,
                     std::vector<std::vector<T>>& out) {
    InArchive arc;
    arc << in;
    std::vector<char> in_buf = std::move(arc.GetBufferVector());
    std::vector<std::vector<char>> out_buf_vec;
    comm.gather(std::move(in_buf), out_buf_vec);

    out.clear();
    out.resize(comm.size());
    for (int i = 0; i < comm.size(); ++i) {
      OutArchive oarc(std::move(out_buf_vec[i]));
      oarc >> out[i];
    }
  }

  static void all_to_all(CommType& comm, const std::vector<std::vector<T>>& in,
                         std::vector<std::vector<T>>& out) {
    std::vector<std::vector<char>> input_buf_vec;
    for (int i = 0; i < comm.size(); ++i) {
      InArchive arc;
      arc << in[i];
      input_buf_vec.emplace_back(std::move(arc.GetBufferVector()));
    }
    std::vector<std::vector<char>> output_buf_vec;
    comm.all_to_all(input_buf_vec, output_buf_vec);
    out.clear();
    out.resize(comm.size());
    for (int i = 0; i < comm.size(); ++i) {
      OutArchive arc(std::move(output_buf_vec[i]));
      arc >> out[i];
    }
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

template <typename T>
void Bcast(CommType& comm, T& obj, int root_worker_id) {
  CommImpl<T>::bcast(comm, obj, root_worker_id);
}

template <typename T>
void Gather(CommType& comm, const T& in, std::vector<T>& out) {
  CommImpl<T>::gather(comm, in, out);
}

template <typename T>
void AllToAll(CommType& comm, const std::vector<T>& in, std::vector<T>& out) {
  CommImpl<T>::all_to_all(comm, in, out);
}

}  // namespace sync_comm

}  // namespace grape

#endif  // GRAPE_COMMUNICATION_SYNC_COMM_H_
