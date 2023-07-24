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

#ifndef GRAPE_PARALLEL_PARALLEL_MESSAGE_MANAGER_OPT_H_
#define GRAPE_PARALLEL_PARALLEL_MESSAGE_MANAGER_OPT_H_

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "grape/communication/mpi_comm.h"
#include "grape/parallel/message_in_buffer.h"
#include "grape/parallel/message_manager_base.h"
#include "grape/parallel/thread_local_message_buffer.h"
#include "grape/serialization/in_archive.h"
#include "grape/serialization/out_archive.h"
#include "grape/utils/concurrent_queue.h"

namespace grape {

/**
 * @brief A optimized version of parallel message manager.
 *
 * ParallelMessageManagerOpt support multi-threads to send messages concurrently
 * with channels. Each channel contains a thread local message buffer.
 *
 * For each thread local message buffer, when accumulated a given amount of
 * messages, the buffer will be sent through MPI.
 *
 * After a round of evaluation, there is a global barrier to determine whether
 * the fixed point is reached.
 *
 */

class ParallelMessageManagerOpt : public MessageManagerBase {
  static constexpr size_t default_msg_send_block_size = 2 * 1023 * 1024;
  static constexpr size_t default_msg_send_block_capacity = 2 * 1024 * 1024;

 public:
  ParallelMessageManagerOpt() {}
  ~ParallelMessageManagerOpt() override {}

  /**
   * @brief Inherit
   */
  void Init(CommType&& comm) override {
    comm_ = std::move(comm);

    fid_ = comm_.rank();
    fnum_ = comm_.size();

    force_terminate_ = false;
    terminate_info_.Init(fnum_);

    recv_queues_[0].SetProducerNum(fnum_);
    recv_queues_[1].SetProducerNum(fnum_);

    round_ = 0;

    sent_size_ = 0;
    total_sent_size_ = 0;
  }

  /**
   * @brief Inherit
   */
  void Start() override { startRecvThread(); }

  /**
   * @brief Inherit
   */
  void StartARound() override {
    if (round_ != 0) {
      waitSend();
      auto& rq = recv_queues_[round_ % 2];
      if (!to_self_.empty()) {
        for (auto& iarc : to_self_) {
          OutArchive oarc(std::move(iarc));
          rq.Put(std::move(oarc));
        }
        to_self_.clear();
      }
      rq.DecProducerNum();
    }
    sent_size_ = 0;
    startSendThread();
  }

  /**
   * @brief Inherit
   */
  void FinishARound() override {
    sent_size_ = finishMsgFilling();
    resetRecvQueue();
    round_++;
    total_sent_size_ += sent_size_;
  }

  /**
   * @brief Inherit
   */
  bool ToTerminate() override {
    int64_t flag[2];
    flag[0] = 1;
    if (sent_size_ == 0 && !force_continue_) {
      flag[0] = 0;
    }
    flag[1] = force_terminate_ ? 1 : 0;
    int64_t ret[2];
    comm_.sum(flag, ret, 2);
    if (ret[1] > 0) {
      terminate_info_.success = false;
      std::string info = terminate_info_.info[fid_];
      comm_.gather(info, terminate_info_.info);
      return true;
    }
    return (ret[0] == 0);
  }

  /**
   * @brief Inherit
   */
  void Finalize() override {
    waitSend();
    comm_.barrier();
    stopRecvThread();
  }

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
   * @brief Inherit
   */
  size_t GetMsgSize() const override { return sent_size_; }

  /**
   * @brief Init a set of channels, each channel is a thread local message
   * buffer.
   *
   * @param channel_num Number of channels.
   * @param block_size Size of each channel.
   * @param block_cap Capacity of each channel.
   */
  void InitChannels(int channel_num = 1,
                    size_t block_size = default_msg_send_block_size,
                    size_t block_cap = default_msg_send_block_capacity) {
    channels_.resize(channel_num);
    for (auto& channel : channels_) {
      channel.Init(fnum_, this, block_size, block_cap);
    }
  }

  std::vector<ThreadLocalMessageBuffer<ParallelMessageManagerOpt>>& Channels() {
    return channels_;
  }

  /**
   * @brief Send a buffer to a fragment.
   *
   * @param fid Destination fragment id.
   * @param arc Message buffer.
   */
  inline void SendRawMsgByFid(fid_t fid, InArchive&& arc) {
    std::pair<fid_t, InArchive> item;
    item.first = fid;
    item.second = std::move(arc);
    sending_queue_.Put(std::move(item));
  }

  /**
   * @brief Send message to a fragment.
   *
   * @tparam MESSAGE_T Message type.
   * @param dst_fid Destination fragment id.
   * @param msg
   * @param channelId
   */
  template <typename MESSAGE_T>
  inline void SendToFragment(fid_t dst_fid, const MESSAGE_T& msg,
                             int channel_id = 0) {
    channels_[channel_id].SendToFragment<MESSAGE_T>(dst_fid, msg);
  }

  /**
   * @brief SyncStateOnOuterVertex on a channel.
   *
   * @tparam GRAPH_T Graph type.
   * @tparam MESSAGE_T Message type.
   * @param frag Source fragment.
   * @param v Source vertex.
   * @param msg
   * @param channel_id
   */
  template <typename GRAPH_T, typename MESSAGE_T>
  inline void SyncStateOnOuterVertex(const GRAPH_T& frag,
                                     const typename GRAPH_T::vertex_t& v,
                                     const MESSAGE_T& msg, int channel_id = 0) {
    channels_[channel_id].SyncStateOnOuterVertex<GRAPH_T, MESSAGE_T>(frag, v,
                                                                     msg);
  }

  template <typename GRAPH_T>
  inline void SyncStateOnOuterVertex(const GRAPH_T& frag,
                                     const typename GRAPH_T::vertex_t& v,
                                     int channel_id = 0) {
    channels_[channel_id].SyncStateOnOuterVertex<GRAPH_T>(frag, v);
  }

  /**
   * @brief SendMsgThroughIEdges on a channel.
   *
   * @tparam GRAPH_T Graph type.
   * @tparam MESSAGE_T Message type.
   * @param frag Source fragment.
   * @param v Source vertex.
   * @param msg
   * @param channel_id
   */
  template <typename GRAPH_T, typename MESSAGE_T>
  inline void SendMsgThroughIEdges(const GRAPH_T& frag,
                                   const typename GRAPH_T::vertex_t& v,
                                   const MESSAGE_T& msg, int channel_id = 0) {
    channels_[channel_id].SendMsgThroughIEdges<GRAPH_T, MESSAGE_T>(frag, v,
                                                                   msg);
  }

  /**
   * @brief SendMsgThroughOEdges on a channel.
   *
   * @tparam GRAPH_T Graph type.
   * @tparam MESSAGE_T Message type.
   * @param frag Source fragment.
   * @param v Source vertex.
   * @param msg
   * @param channel_id
   */
  template <typename GRAPH_T, typename MESSAGE_T>
  inline void SendMsgThroughOEdges(const GRAPH_T& frag,
                                   const typename GRAPH_T::vertex_t& v,
                                   const MESSAGE_T& msg, int channel_id = 0) {
    channels_[channel_id].SendMsgThroughOEdges<GRAPH_T, MESSAGE_T>(frag, v,
                                                                   msg);
  }

  /**
   * @brief SendMsgThroughEdges on a channel.
   *
   * @tparam GRAPH_T Graph type.
   * @tparam MESSAGE_T Message type.
   * @param frag Source fragment.
   * @param v Source vertex.
   * @param msg
   * @param channel_id
   */
  template <typename GRAPH_T, typename MESSAGE_T>
  inline void SendMsgThroughEdges(const GRAPH_T& frag,
                                  const typename GRAPH_T::vertex_t& v,
                                  const MESSAGE_T& msg, int channel_id = 0) {
    channels_[channel_id].SendMsgThroughEdges<GRAPH_T, MESSAGE_T>(frag, v, msg);
  }

  /**
   * @brief Get a bunch of messages, stored in a MessageInBuffer.
   *
   * @param buf Message buffer which holds a grape::OutArchive.
   */
  inline bool GetMessageInBuffer(MessageInBuffer& buf) {
    grape::OutArchive arc;
    auto& que = recv_queues_[round_ % 2];
    if (que.Get(arc)) {
      buf.Init(std::move(arc));
      return true;
    } else {
      return false;
    }
  }

  /**
   * @brief Parallel process all incoming messages with given function of last
   * round.
   *
   * @tparam GRAPH_T Graph type.
   * @tparam MESSAGE_T Message type.
   * @tparam FUNC_T Function type.
   * @param thread_num Number of threads.
   * @param frag
   * @param func
   */
  template <typename GRAPH_T, typename MESSAGE_T, typename FUNC_T>
  inline void ParallelProcess(int thread_num, const GRAPH_T& frag,
                              const FUNC_T& func) {
    std::vector<std::thread> threads(thread_num);

    for (int i = 0; i < thread_num; ++i) {
      threads[i] = std::thread(
          [&](int tid) {
            typename GRAPH_T::vid_t id;
            typename GRAPH_T::vertex_t vertex(0);
            MESSAGE_T msg;
            auto& que = recv_queues_[round_ % 2];
            OutArchive arc;
            while (que.Get(arc)) {
              while (!arc.Empty()) {
                arc >> id >> msg;
                frag.Gid2Vertex(id, vertex);
                func(tid, vertex, msg);
              }
            }
          },
          i);
    }

    for (auto& thrd : threads) {
      thrd.join();
    }
  }

  template <typename GRAPH_T, typename MESSAGE_T, typename FUNC_T>
  inline size_t ParallelProcessCount(int thread_num, const GRAPH_T& frag,
                                     const FUNC_T& func) {
    std::vector<std::thread> threads(thread_num);
    std::atomic<size_t> ret(0);

    for (int i = 0; i < thread_num; ++i) {
      threads[i] = std::thread(
          [&](int tid) {
            typename GRAPH_T::vid_t id;
            typename GRAPH_T::vertex_t vertex(0);
            MESSAGE_T msg;
            auto& que = recv_queues_[round_ % 2];
            OutArchive arc;
            size_t local_count = 0;
            while (que.Get(arc)) {
              while (!arc.Empty()) {
                arc >> id >> msg;
                frag.Gid2Vertex(id, vertex);
                func(tid, vertex, msg);
                ++local_count;
              }
            }
            ret.fetch_add(local_count, std::memory_order_relaxed);
          },
          i);
    }

    for (auto& thrd : threads) {
      thrd.join();
    }
    return ret.load();
  }

  /**
   * @brief Parallel process all incoming messages with given function of last
   * round.
   *
   * @tparam GRAPH_T Graph type.
   * @tparam MESSAGE_T Message type.
   * @tparam FUNC_T Function type.
   * @param thread_num Number of threads.
   * @param frag
   * @param func
   */
  template <typename MESSAGE_T, typename FUNC_T>
  inline void ParallelProcess(int thread_num, const FUNC_T& func) {
    std::vector<std::thread> threads(thread_num);

    for (int i = 0; i < thread_num; ++i) {
      threads[i] = std::thread(
          [&](int tid) {
            MESSAGE_T msg;
            auto& que = recv_queues_[round_ % 2];
            OutArchive arc;
            while (que.Get(arc)) {
              while (!arc.Empty()) {
                arc >> msg;
                func(tid, msg);
              }
            }
          },
          i);
    }

    for (auto& thrd : threads) {
      thrd.join();
    }
  }

 private:
  void startSendThread() {
    force_continue_ = false;
    int round = round_;

    CHECK_EQ(sending_queue_.Size(), 0);
    sending_queue_.SetProducerNum(1);
    send_thread_ = std::thread(
        [this](int msg_round) {
          std::pair<fid_t, InArchive> item;
          while (sending_queue_.Get(item)) {
            if (item.second.GetSize() == 0) {
              continue;
            }
            if (item.first == fid_) {
              to_self_.emplace_back(std::move(item.second));
            } else {
              std::vector<char> buf = std::move(item.second.GetBufferVector());
              comm_.send(item.first, std::move(buf), msg_round);
            }
          }
          for (fid_t i = 0; i < fnum_; ++i) {
            if (i == fid_) {
              continue;
            }
            comm_.send_empty(i, msg_round);
          }
          comm_.wait_send();
        },
        round + 1);
  }

  void probeAllIncomingMessages() {
    int self_worker_id = comm_.rank();
    while (true) {
      int src_worker_id, tag;
      std::vector<char> buf;
      comm_.recv(src_worker_id, buf, tag);
      if (src_worker_id == self_worker_id) {
        break;
      }
      if (buf.empty()) {
        recv_queues_[tag % 2].DecProducerNum();
      } else {
        OutArchive arc(std::move(buf));
        recv_queues_[tag % 2].Put(std::move(arc));
      }
    }
  }

  void startRecvThread() {
    recv_thread_ = std::thread([this]() { probeAllIncomingMessages(); });
  }

  void stopRecvThread() {
    comm_.send_empty(comm_.rank(), 0);
    recv_thread_.join();
  }

  inline size_t finishMsgFilling() {
    size_t ret = 0;
    for (auto& channel : channels_) {
      channel.FlushMessages();
      ret += channel.SentMsgSize();
      channel.Reset();
    }
    sending_queue_.DecProducerNum();
    return ret;
  }

  void resetRecvQueue() {
    auto& curr_recv_queue = recv_queues_[round_ % 2];
    if (round_) {
      OutArchive arc;
      while (curr_recv_queue.Get(arc)) {}
    }
    curr_recv_queue.SetProducerNum(fnum_);
  }

  void waitSend() { send_thread_.join(); }

  fid_t fid_;
  fid_t fnum_;

  std::vector<InArchive> to_self_;
  std::vector<InArchive> to_others_;

  std::vector<ThreadLocalMessageBuffer<ParallelMessageManagerOpt>> channels_;
  int round_;

  BlockingQueue<std::pair<fid_t, InArchive>> sending_queue_;
  std::thread send_thread_;

  std::array<BlockingQueue<OutArchive>, 2> recv_queues_;
  std::thread recv_thread_;

  bool force_continue_;
  size_t sent_size_;
  size_t total_sent_size_;

  bool force_terminate_;
  TerminateInfo terminate_info_;
  CommType comm_;
};

}  // namespace grape

#endif  // GRAPE_PARALLEL_PARALLEL_MESSAGE_MANAGER_OPT_H_
