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

#ifndef GRAPE_PARALLEL_DEFAULT_MESSAGE_MANAGER_H_
#define GRAPE_PARALLEL_DEFAULT_MESSAGE_MANAGER_H_

#include <memory>
#include <utility>
#include <vector>

#include "grape/communication/sync_comm.h"
#include "grape/parallel/message_manager_base.h"
#include "grape/serialization/in_archive.h"
#include "grape/serialization/out_archive.h"

namespace grape {

/**
 * @brief Default message manager.
 *
 * The send and recv methods are not thread-safe.
 */
class DefaultMessageManager : public MessageManagerBase {
 public:
  DefaultMessageManager() : comm_() {}
  ~DefaultMessageManager() override {}

  /**
   * @brief Inherit
   */
  void Init(CommType&& comm) override {
    comm_ = std::move(comm);

    fid_ = comm_.rank();
    fnum_ = comm_.size();

    force_terminate_ = false;
    terminate_info_.Init(fnum_);

    lengths_out_.resize(fnum_);
    lengths_in_.resize(fnum_);

    to_send_.resize(fnum_);
    to_recv_.resize(fnum_);
  }

  /**
   * @brief Inherit
   */
  void Start() override {}

  /**
   * @brief Inherit
   */
  void StartARound() override {
    sent_size_ = 0;
    for (auto& arc : to_send_) {
      arc.Clear();
    }
    force_continue_ = false;
    cur_ = 0;
  }

  /**
   * @brief Inherit
   */
  void FinishARound() override {
    to_terminate_ = syncLengths();
    if (to_terminate_) {
      return;
    }

    for (fid_t i = 1; i < fnum_; ++i) {
      fid_t dst_fid = (fid_ + fnum_ - i) % fnum_;
      auto& arc = to_send_[dst_fid];
      if (arc.Empty()) {
        continue;
      }
      comm_.send(dst_fid, std::move(arc.GetBufferVector()), 0);
    }

    for (fid_t i = 1; i < fnum_; ++i) {
      fid_t src_fid = (fid_ + i) % fnum_;
      size_t length = lengths_in_[src_fid][fid_];
      if (length == 0) {
        continue;
      }
      std::vector<char> buf;
      comm_.recv_from_tagged(src_fid, buf, 0);
      auto& arc = to_recv_[src_fid];
      arc = std::move(buf);
    }

    to_recv_[fid_].Clear();
    if (!to_send_[fid_].Empty()) {
      to_recv_[fid_] = std::move(to_send_[fid_]);
    }
  }

  /**
   * @brief Inherit
   */
  bool ToTerminate() override { return to_terminate_; }

  /**
   * @brief Inherit
   */
  void Finalize() override {}

  /**
   * @brief Inherit
   */
  size_t GetMsgSize() const override { return sent_size_; }

  /**
   * @brief Inherit
   */
  void ForceContinue() override { force_continue_ = true; }

  /**
   * @brief Inherit
   */
  void ForceTerminate(const std::string& terminate_info) override {
    force_terminate_ = true;
    terminate_info_.info[fid_] = terminate_info;
  }

  /**
   * @brief Inherit
   */
  const TerminateInfo& GetTerminateInfo() const override {
    return terminate_info_;
  }

  /**
   * @brief Send message to a fragment.
   *
   * @tparam MESSAGE_T Message type.
   * @param dst_fid Destination fragment id.
   * @param msg
   */
  template <typename MESSAGE_T>
  inline void SendToFragment(fid_t dst_fid, const MESSAGE_T& msg) {
    to_send_[dst_fid] << msg;
  }

  /**
   * @brief Communication by synchronizing the status on outer vertices, for
   * edge-cut fragments.
   *
   * Assume a fragment F_1, a crossing edge a->b' in F_1 and a is an inner
   * vertex in F_1. This function invoked on F_1 send status on b' to b on F_2,
   * where b is an inner vertex.
   *
   * @tparam GRAPH_T
   * @tparam MESSAGE_T
   * @param frag
   * @param v: a
   * @param msg
   */
  template <typename GRAPH_T, typename MESSAGE_T>
  inline void SyncStateOnOuterVertex(const GRAPH_T& frag,
                                     const typename GRAPH_T::vertex_t& v,
                                     const MESSAGE_T& msg) {
    fid_t fid = frag.GetFragId(v);
    to_send_[fid] << frag.GetOuterVertexGid(v) << msg;
  }

  /**
   * @brief Communication via a crossing edge a<-c. It sends message
   * from a to c.
   *
   * @tparam GRAPH_T
   * @tparam MESSAGE_T
   * @param frag
   * @param v: a
   * @param msg
   */
  template <typename GRAPH_T, typename MESSAGE_T>
  inline void SendMsgThroughIEdges(const GRAPH_T& frag,
                                   const typename GRAPH_T::vertex_t& v,
                                   const MESSAGE_T& msg) {
    auto dsts = frag.IEDests(v);
    const fid_t* ptr = dsts.begin;
    typename GRAPH_T::vid_t gid = frag.GetInnerVertexGid(v);
    while (ptr != dsts.end) {
      fid_t fid = *(ptr++);
      to_send_[fid] << gid << msg;
    }
  }

  /**
   * @brief Communication via a crossing edge a->b. It sends message
   * from a to b.
   *
   * @tparam GRAPH_T
   * @tparam MESSAGE_T
   * @param frag
   * @param v: a
   * @param msg
   */
  template <typename GRAPH_T, typename MESSAGE_T>
  inline void SendMsgThroughOEdges(const GRAPH_T& frag,
                                   const typename GRAPH_T::vertex_t& v,
                                   const MESSAGE_T& msg) {
    auto dsts = frag.OEDests(v);
    const fid_t* ptr = dsts.begin;
    typename GRAPH_T::vid_t gid = frag.GetInnerVertexGid(v);
    while (ptr != dsts.end) {
      fid_t fid = *(ptr++);
      to_send_[fid] << gid << msg;
    }
  }

  /**
   * @brief Communication via crossing edges a->b and a<-c. It sends message
   * from a to b and c.
   *
   * @tparam GRAPH_T
   * @tparam MESSAGE_T
   * @param frag
   * @param v: a
   * @param msg
   */
  template <typename GRAPH_T, typename MESSAGE_T>
  inline void SendMsgThroughEdges(const GRAPH_T& frag,
                                  const typename GRAPH_T::vertex_t& v,
                                  const MESSAGE_T& msg) {
    auto dsts = frag.IOEDests(v);
    const fid_t* ptr = dsts.begin;
    typename GRAPH_T::vid_t gid = frag.GetInnerVertexGid(v);
    while (ptr != dsts.end) {
      fid_t fid = *(ptr++);
      to_send_[fid] << gid << msg;
    }
  }

  /**
   * @brief Get a message from message buffer.
   *
   * @tparam MESSAGE_T
   * @param msg
   *
   * @return Return true if got a message, and false if no message left.
   */
  template <typename MESSAGE_T>
  inline bool GetMessage(MESSAGE_T& msg) {
    while (cur_ != fnum_ && to_recv_[cur_].Empty()) {
      ++cur_;
    }
    if (cur_ == fnum_) {
      return false;
    }
    to_recv_[cur_] >> msg;
    return true;
  }

  /**
   * @brief Get a message and its target vertex from message buffer.
   *
   * @tparam GRAPH_T
   * @tparam MESSAGE_T
   * @param frag
   * @param v
   * @param msg
   *
   * @return Return true if got a message, and false if no message left.
   */
  template <typename GRAPH_T, typename MESSAGE_T>
  inline bool GetMessage(const GRAPH_T& frag, typename GRAPH_T::vertex_t& v,
                         MESSAGE_T& msg) {
    while (cur_ != fnum_ && to_recv_[cur_].Empty()) {
      ++cur_;
    }
    if (cur_ == fnum_) {
      return false;
    }
    typename GRAPH_T::vid_t gid;
    to_recv_[cur_] >> gid >> msg;
    frag.Gid2Vertex(gid, v);
    return true;
  }

 protected:
  fid_t fid() const { return fid_; }
  fid_t fnum() const { return fnum_; }

  std::vector<InArchive> to_send_;

 private:
  bool syncLengths() {
    for (fid_t i = 0; i < fnum_; ++i) {
      sent_size_ += to_send_[i].GetSize();
      lengths_out_[i] = to_send_[i].GetSize();
    }
    if (force_continue_) {
      ++lengths_out_[fid_];
    }
    int terminate_flag = force_terminate_ ? 1 : 0;
    int terminate_flag_sum = comm_.sum(terminate_flag);
    if (terminate_flag_sum > 0) {
      terminate_info_.success = false;
      comm_.gather(terminate_info_.info[fid_], terminate_info_.info);
      return true;
    } else {
      comm_.gather(lengths_out_, lengths_in_);
      for (auto& vec : lengths_in_) {
        for (auto s : vec) {
          if (s != 0) {
            return false;
          }
        }
      }
      return true;
    }
  }

  std::vector<OutArchive> to_recv_;
  fid_t cur_;

  std::vector<size_t> lengths_out_;
  std::vector<std::vector<size_t>> lengths_in_;

  fid_t fid_;
  fid_t fnum_;

  size_t sent_size_;
  bool to_terminate_;
  bool force_continue_;
  bool force_terminate_;

  TerminateInfo terminate_info_;
  CommType comm_;
};

}  // namespace grape

#endif  // GRAPE_PARALLEL_DEFAULT_MESSAGE_MANAGER_H_
