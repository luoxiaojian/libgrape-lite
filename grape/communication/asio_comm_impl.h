#ifndef GRAPE_COMMUNICATION_ASIO_COMM_IMPL_H_
#define GRAPE_COMMUNICATION_ASIO_COMM_IMPL_H_

#include <array>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#ifdef USE_ASIO

#include <boost/asio.hpp>

#include "grape/communication/asio_comm_types.h"

namespace grape {

namespace asio_comm {

class MessagePool {
 public:
  MessagePool() : worker_num_(0), comm_num_(0) {}
  ~MessagePool() {}

  void init(int worker_num);

  int allocate_comm();

  void put(int comm_id, int src, int tag, std::vector<char>&& buf);

  void take(int comm_id, int& src, int& tag, std::vector<char>& buf);

  void take_from(int comm_id, int src, int& tag, std::vector<char>& buf);

  void take_tagged(int comm_id, int& src, int tag, std::vector<char>& buf);

  void take_tagged_reserved(int comm_id, int& src, int tag,
                            std::vector<char>& buf);

  void take_from_tagged(int comm_id, int src, int tag, std::vector<char>& buf);

  void take_from_tagged_reserved(int comm_id, int src, int tag,
                                 std::vector<char>& buf);

  void barrier(int comm_id, int src);

  void wait_barrier_slave(int comm_id, int expected);

  void wait_barrier_master(int comm_id, int expected);

 private:
  std::mutex mutex_;
  std::condition_variable cond_;
  std::vector<std::map<int, std::deque<std::vector<char>>>> pool_;

  std::mutex reserved_mutex_;
  std::condition_variable reserved_cond_;
  std::vector<std::array<std::deque<std::vector<char>>, reserved_tag_num>>
      reserved_pool_;

  std::mutex barrier_mutex_;
  std::condition_variable barrier_cond_;
  std::vector<int> barrier_count_;

  int worker_num_;
  std::atomic<int> comm_num_;
};

std::vector<std::shared_ptr<boost::asio::ip::tcp::socket>> create_connections(
    boost::asio::io_context& ioc, const std::vector<std::string>& hosts,
    const std::vector<std::string>& ports, int rank, int size);

}  // namespace asio_comm

}  // namespace grape

#endif

#endif  // GRAPE_COMMUNICATION_ASIO_COMM_IMPL_H_
