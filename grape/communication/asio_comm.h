#ifndef GRAPE_COMMUNICATION_ASIO_COMM_H_
#define GRAPE_COMMUNICATION_ASIO_COMM_H_

#ifdef USE_ASIO

#include <boost/asio.hpp>

#include <iostream>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include "glog/logging.h"

#include "grape/communication/asio_comm_protocol.h"
#include "grape/utils/concurrent_queue.h"

namespace grape {

class AsioCommAllocator;

class AsioComm {
 public:
  AsioComm();
  AsioComm(AsioComm&& rhs);

  AsioComm& operator=(AsioComm&& rhs);

  AsioComm(const AsioComm&) = delete;
  ~AsioComm() {}

 private:
  AsioComm(asio_comm::MessagePool* pool,
           BlockingQueue<std::tuple<asio_comm::MsgType, int, int,
                                    std::vector<char>>>* queue,
           int comm_id, int rank, int size, int local_rank, int local_size);

 public:
  int rank() const;
  int size() const;

  int local_rank() const;
  int local_size() const;

  void barrier();

  void send(int dst, std::vector<char>&& buf, int tag);

  void send(int dst, const char* buf, size_t count, int tag);

  void send(int dst, const std::vector<char>& buf, int tag);

  void send_empty(int dst, int tag);

  void wait_send();

  void recv(int& src, std::vector<char>& buf, int& tag);

  void recv_from(int src, std::vector<char>& buf, int& tag);

  void recv_tagged(int& src, std::vector<char>& buf, int tag);

  void recv_from_tagged(int src, std::vector<char>& buf, int tag);

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

  void bcast(std::vector<char>& buf, int root);

 private:
  void send_reserved(int dst, std::vector<char>&& buf, int tag);

  void send_reserved(int dst, const char* buf, size_t count, int tag);

  void recv_tagged_reserved(int& src, std::vector<char>& buf, int tag);

  void recv_from_tagged_reserved(int src, std::vector<char>& buf, int tag);

  void push_empty(int dst, int tag);

  void push_data(int dst, int tag, std::vector<char>&& data);

  void push_bcast(int tag, std::vector<char>&& data);

  void push_barrier(int dst);

  asio_comm::MessagePool* pool_;
  // type, comm_id, dst, tag, data
  // data: kData, comm_id, dst, tag, data
  // barrier: kBarrier, comm_id
  // exit: kExit
  // bcast: kBcast, comm_id, root, tag, data
  BlockingQueue<std::tuple<asio_comm::MsgType, int, int, std::vector<char>>>*
      queue_;
  int comm_id_;

  int rank_;
  int size_;
  int local_rank_;
  int local_size_;
  int barrier_count_;

  friend class AsioCommAllocator;
};

class AsioCommAllocator {
 public:
  AsioCommAllocator() {}
  ~AsioCommAllocator();

  void init(const std::string& hostfile, int self_id, int worker_num);

  void init(const std::vector<std::string>& addresses,
            const std::vector<std::string>& ports, int self_id);

  static AsioCommAllocator& get() {
    static AsioCommAllocator allocator;
    return allocator;
  }

  AsioComm allocate();

  int rank() const;
  int size() const;
  int local_rank() const;
  int local_size() const;

 private:
  int rank_;
  int size_;
  int local_rank_;
  int local_size_;

  std::vector<std::thread> send_threads_;
  std::vector<std::thread> recv_threads_;

  boost::asio::io_context ioc_;

  asio_comm::MessagePool pool_;
  // type, comm_id, dst, tag, data
  BlockingQueue<std::tuple<asio_comm::MsgType, int, int, std::vector<char>>>*
      send_queue_;

  std::vector<std::shared_ptr<boost::asio::ip::tcp::socket>> sockets_;
};

using CommAllocatorType = AsioCommAllocator;
using CommType = AsioComm;

}  // namespace grape

#endif

#endif  // GRAPE_COMMUNICATION_ASIO_COMM_H_
