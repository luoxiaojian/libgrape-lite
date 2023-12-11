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

#ifndef GRAPE_PARALLEL_THREAD_LOCAL_MESSAGE_BUFFER_OPT_H_
#define GRAPE_PARALLEL_THREAD_LOCAL_MESSAGE_BUFFER_OPT_H_

#include <memory>
#include <utility>
#include <vector>

#include "grape/graph/adj_list.h"
#include "grape/serialization/in_archive.h"
#include "grape/utils/message_buffer_pool.h"

namespace grape {

template <typename MM_T>
class ThreadLocalMessageBufferOpt {
 public:
  /**
   * @brief Initialize thread local message buffer.
   *
   * @param fnum Number of fragments.
   * @param mm MessageManager pointer.
   * @param block_size Size of thread local message buffer.
   * @param block_cap Capacity of thread local message buffer.
   */
  void Init(fid_t fnum, MM_T* mm, MessageBufferPool* pool) {
    fnum_ = fnum;
    mm_ = mm;
    pool_ = pool;

    to_send_.clear();
    to_send_.resize(fnum_);

    for (auto& arc : to_send_) {
      arc = std::move(pool_->take());
    }

    block_size_ = pool->chunk_size();
    sent_size_ = 0;
  }

  /**
   * @brief Communication by synchronizing the status on outer vertices, for
   * edge-cut fragments.
   *
   * Assume a fragment F_1, a crossing edge a->b' in F_1 and a is an inner
   * vertex in F_1. This function invoked on F_1 send status on b' to b on
   * F_2, where b is an inner vertex.
   *
   * @tparam GRAPH_T Graph type.
   * @tparam MESSAGE_T Message type,
   * @param frag Source fragment.
   * @param v: a
   * @param msg
   */
  template <typename GRAPH_T, typename MESSAGE_T>
  inline typename std::enable_if<std::is_pod<MESSAGE_T>::value, void>::type
  SyncStateOnOuterVertex(const GRAPH_T& frag,
                         const typename GRAPH_T::vertex_t& v,
                         const MESSAGE_T& msg) {
    fid_t fid = frag.GetFragId(v);
    constexpr size_t msg_size =
        sizeof(MESSAGE_T) + sizeof(typename GRAPH_T::vid_t);
    if (to_send_[fid].GetSize() + msg_size > block_size_) {
      flushLocalBuffer(fid);
    }
    to_send_[fid] << frag.GetOuterVertexGid(v) << msg;
  }

  template <typename GRAPH_T, typename MESSAGE_T>
  inline typename std::enable_if<!std::is_pod<MESSAGE_T>::value, void>::type
  SyncStateOnOuterVertex(const GRAPH_T& frag,
                         const typename GRAPH_T::vertex_t& v,
                         const MESSAGE_T& msg) {
    fid_t fid = frag.GetFragId(v);
    size_t msg_size =
        sizeof(typename GRAPH_T::vid_t) + SerializedSize<MESSAGE_T>::size(msg);
    if (to_send_[fid].GetSize() + msg_size > block_size_) {
      flushLocalBuffer(fid);
    }
    to_send_[fid] << frag.GetOuterVertexGid(v) << msg;
  }

  template <typename GRAPH_T>
  inline void SyncStateOnOuterVertex(const GRAPH_T& frag,
                                     const typename GRAPH_T::vertex_t& v) {
    fid_t fid = frag.GetFragId(v);
    size_t msg_size = sizeof(typename GRAPH_T::vid_t);
    if (to_send_[fid].GetSize() + msg_size > block_size_) {
      flushLocalBuffer(fid);
    }
    to_send_[fid] << frag.GetOuterVertexGid(v);
  }

  /**
   * @brief Communication via a crossing edge a<-c. It sends message
   * from a to c.
   *
   * @tparam GRAPH_T Graph type.
   * @tparam MESSAGE_T Message type,
   * @param frag Source fragment.
   * @param v: a
   * @param msg
   */
  template <typename GRAPH_T, typename MESSAGE_T>
  inline typename std::enable_if<std::is_pod<MESSAGE_T>::value, void>::type
  SendMsgThroughIEdges(const GRAPH_T& frag, const typename GRAPH_T::vertex_t& v,
                       const MESSAGE_T& msg) {
    auto dsts = frag.IEDests(v);
    const fid_t* ptr = dsts.begin;
    size_t msg_size = sizeof(typename GRAPH_T::vid_t) + sizeof(MESSAGE_T);
    typename GRAPH_T::vid_t gid = frag.GetInnerVertexGid(v);
    while (ptr != dsts.end) {
      fid_t fid = *(ptr++);
      if (to_send_[fid].GetSize() + msg_size > block_size_) {
        flushLocalBuffer(fid);
      }
      to_send_[fid] << gid << msg;
    }
  }

  template <typename GRAPH_T, typename MESSAGE_T>
  inline typename std::enable_if<!std::is_pod<MESSAGE_T>::value, void>::type
  SendMsgThroughIEdges(const GRAPH_T& frag, const typename GRAPH_T::vertex_t& v,
                       const MESSAGE_T& msg) {
    auto dsts = frag.IEDests(v);
    const fid_t* ptr = dsts.begin;
    size_t msg_size =
        sizeof(typename GRAPH_T::vid_t) + SerializedSize<MESSAGE_T>::size(msg);
    typename GRAPH_T::vid_t gid = frag.GetInnerVertexGid(v);
    while (ptr != dsts.end) {
      fid_t fid = *(ptr++);
      if (to_send_[fid].GetSize() + msg_size > block_size_) {
        flushLocalBuffer(fid);
      }
      to_send_[fid] << gid << msg;
    }
  }

  /**
   * @brief Communication via a crossing edge a->b. It sends message
   * from a to b.
   *
   * @tparam GRAPH_T Graph type.
   * @tparam MESSAGE_T Message type.
   * @param frag Source fragment.
   * @param v: a
   * @param msg
   */
  template <typename GRAPH_T, typename MESSAGE_T>
  inline typename std::enable_if<std::is_pod<MESSAGE_T>::value, void>::type
  SendMsgThroughOEdges(const GRAPH_T& frag, const typename GRAPH_T::vertex_t& v,
                       const MESSAGE_T& msg) {
    auto dsts = frag.OEDests(v);
    const fid_t* ptr = dsts.begin;
    typename GRAPH_T::vid_t gid = frag.GetInnerVertexGid(v);
    constexpr size_t msg_size =
        sizeof(MESSAGE_T) + sizeof(typename GRAPH_T::vid_t);
    while (ptr != dsts.end) {
      fid_t fid = *(ptr++);
      if (to_send_[fid].GetSize() + msg_size > block_size_) {
        flushLocalBuffer(fid);
      }
      to_send_[fid] << gid << msg;
    }
  }

  template <typename GRAPH_T, typename MESSAGE_T>
  inline typename std::enable_if<!std::is_pod<MESSAGE_T>::value, void>::type
  SendMsgThroughOEdges(const GRAPH_T& frag, const typename GRAPH_T::vertex_t& v,
                       const MESSAGE_T& msg) {
    auto dsts = frag.OEDests(v);
    const fid_t* ptr = dsts.begin;
    typename GRAPH_T::vid_t gid = frag.GetInnerVertexGid(v);
    size_t msg_size =
        sizeof(typename GRAPH_T::vid_t) + SerializedSize<MESSAGE_T>::size(msg);
    while (ptr != dsts.end) {
      fid_t fid = *(ptr++);
      if (to_send_[fid].GetSize() + msg_size > block_size_) {
        flushLocalBuffer(fid);
      }
      to_send_[fid] << gid << msg;
    }
  }

  /**
   * @brief Communication via crossing edges a->b and a<-c. It sends message
   * from a to b and c.
   *
   * @tparam GRAPH_T Graph type.
   * @tparam MESSAGE_T Message type.
   * @param frag Source fragment.
   * @param v: a
   * @param msg
   */
  template <typename GRAPH_T, typename MESSAGE_T>
  inline typename std::enable_if<std::is_pod<MESSAGE_T>::value, void>::type
  SendMsgThroughEdges(const GRAPH_T& frag, const typename GRAPH_T::vertex_t& v,
                      const MESSAGE_T& msg) {
    auto dsts = frag.IOEDests(v);
    const fid_t* ptr = dsts.begin;
    typename GRAPH_T::vid_t gid = frag.GetInnerVertexGid(v);
    constexpr size_t msg_size =
        sizeof(MESSAGE_T) + sizeof(typename GRAPH_T::vid_t);
    while (ptr != dsts.end) {
      fid_t fid = *(ptr++);
      if (to_send_[fid].GetSize() + msg_size > block_size_) {
        flushLocalBuffer(fid);
      }
      to_send_[fid] << gid << msg;
    }
  }

  template <typename GRAPH_T, typename MESSAGE_T>
  inline typename std::enable_if<!std::is_pod<MESSAGE_T>::value, void>::type
  SendMsgThroughEdges(const GRAPH_T& frag, const typename GRAPH_T::vertex_t& v,
                      const MESSAGE_T& msg) {
    auto dsts = frag.IOEDests(v);
    const fid_t* ptr = dsts.begin;
    typename GRAPH_T::vid_t gid = frag.GetInnerVertexGid(v);
    size_t msg_size =
        sizeof(typename GRAPH_T::vid_t) + SerializedSize<MESSAGE_T>::size(msg);
    while (ptr != dsts.end) {
      fid_t fid = *(ptr++);
      if (to_send_[fid].GetSize() + msg_size > block_size_) {
        flushLocalBuffer(fid);
      }
      to_send_[fid] << gid << msg;
    }
  }

  /**
   * @brief Send message to a fragment.
   *
   * @tparam MESSAGE_T Message type.
   * @param dst_fid Destination fragment id.
   * @param msg
   */
  template <typename MESSAGE_T>
  inline typename std::enable_if<std::is_pod<MESSAGE_T>::value, void>::type
  SendToFragment(fid_t dst_fid, const MESSAGE_T& msg) {
    size_t msg_size = sizeof(MESSAGE_T);
    if (to_send_[dst_fid].GetSize() + msg_size > block_size_) {
      flushLocalBuffer(dst_fid);
    }
    to_send_[dst_fid] << msg;
  }

  template <typename MESSAGE_T>
  inline typename std::enable_if<!std::is_pod<MESSAGE_T>::value, void>::type
  SendToFragment(fid_t dst_fid, const MESSAGE_T& msg) {
    size_t msg_size = SerializedSize<MESSAGE_T>::size(msg);
    if (to_send_[dst_fid].GetSize() + msg_size > block_size_) {
      flushLocalBuffer(dst_fid);
    }
    to_send_[dst_fid] << msg;
  }

  /**
   * @brief Flush messages to message manager.
   */
  inline void FlushMessages() {
    for (fid_t fid = 0; fid < fnum_; ++fid) {
      if (to_send_[fid].GetSize() > 0) {
        flushLocalBuffer(fid);
      }
    }
  }

  size_t SentMsgSize() const { return sent_size_; }

  inline void Reset() { sent_size_ = 0; }

 private:
  inline void flushLocalBuffer(fid_t fid) {
    sent_size_ += to_send_[fid].GetSize();
    mm_->SendRawMsgByFid(fid, std::move(to_send_[fid].buffer()));
    to_send_[fid] = std::move(pool_->take());
  }

  std::vector<InArchive> to_send_;
  MM_T* mm_;
  fid_t fnum_;

  size_t block_size_;

  size_t sent_size_;
  MessageBufferPool* pool_;
};

}  // namespace grape

#endif  // GRAPE_PARALLEL_THREAD_LOCAL_MESSAGE_BUFFER_OPT_H_
