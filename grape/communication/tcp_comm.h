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

#ifndef GRAPE_COMMUNICATION_TCP_COMM_H_
#define GRAPE_COMMUNICATION_TCP_COMM_H_

#ifndef USE_MPI

#include "boost/asio.hpp"

#include <condition_variable>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "glog/logging.h"

#include "grape/serialization/in_archive.h"
#include "grape/serialization/out_archive.h"
#include "grape/utils/concurrent_queue.h"

namespace grape {

class MessagePoll {
 public:
  MessagePoll() {}
  ~MessagePoll() {}

  void init(int n) {
    pool_.resize(n);
    barrier_count_.resize(n, 0);
  }

  void put(int src, int tag, std::vector<char>&& buf) {
    std::lock_guard<std::mutex> lock(mutex_);
    pool_[src][tag].emplace_back(std::move(buf));
    cond_.notify_all();
  }

  void take(int& src, int& tag, std::vector<char>& buf) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] {
      for (auto& m : pool_) {
        if (!m.empty()) {
          return true;
        }
      }
      return false;
    });
    for (int i = 0; i < pool_.size(); ++i) {
      if (!pool_[i].empty()) {
        src = i;
        auto it = pool_[i].begin();
        tag = it->first;
        buf = std::move(it->second.front());
        it->second.pop_front();
        if (it->second.empty()) {
          pool_[i].erase(it);
        }
        return;
      }
    }
  }

  void take_from(int src, int& tag, std::vector<char>& buf) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this, src] { return !pool_[src].empty(); });
    auto it = pool_[src].begin();
    tag = it->first;
    buf = std::move(it->second.front());
    it->second.pop_front();
    if (it->second.empty()) {
      pool_[src].erase(it);
    }
  }

  void take_tagged(int& src, int tag, std::vector<char>& buf) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this, tag] {
      for (auto& m : pool_) {
        if (m.find(tag) != m.end()) {
          return true;
        }
      }
      return false;
    });
    for (int i = 0; i < pool_.size(); ++i) {
      if (pool_[i].find(tag) != pool_[i].end()) {
        src = i;
        auto it = pool_[i].find(tag);
        buf = std::move(it->second.front());
        it->second.pop_front();
        if (it->second.empty()) {
          pool_[i].erase(it);
        }
        return;
      }
    }
  }

  void take_from_tagged(int src, int tag, std::vector<char>& buf) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this, src, tag] {
      return pool_[src].find(tag) != pool_[src].end();
    });
    auto it = pool_[src].find(tag);
    buf = std::move(it->second.front());
    it->second.pop_front();
    if (it->second.empty()) {
      pool_[src].erase(it);
    }
  }

  void barrier(int src) {
    std::lock_guard<std::mutex> lock(barrier_mutex_);
    barrier_count_[src]++;
    barrier_cond_.notify_all();
  }

  void wait_barrier_slave(int expected) {
    std::unique_lock<std::mutex> lock(barrier_mutex_);
    barrier_cond_.wait(
        lock, [this, expected] { return barrier_count_[0] >= expected; });
  }

  void wait_barrier_master(int expected) {
    std::unique_lock<std::mutex> lock(barrier_mutex_);
    barrier_cond_.wait(lock, [this, expected] {
      for (int i = 1; i < barrier_count_.size(); ++i) {
        if (barrier_count_[i] < expected) {
          return false;
        }
      }
      return true;
    });
  }

 private:
  std::mutex mutex_;
  std::condition_variable cond_;
  std::vector<std::map<int, std::deque<std::vector<char>>>> pool_;

  std::mutex barrier_mutex_;
  std::condition_variable barrier_cond_;
  std::vector<int> barrier_count_;
};

struct Header {
  size_t length;
  int tag;
  int type;
};

class Reader : public std::enable_shared_from_this<Reader> {
 public:
  Reader(int src, boost::asio::ip::tcp::socket socket, MessagePoll& pool)
      : socket_(std::move(socket)), pool_(pool), src_(src) {}

  void read() {
    auto self = shared_from_this();
    socket_.async_read_some(
        boost::asio::buffer(&header_, sizeof(header_)),
        [self, this](boost::system::error_code ec, size_t length) {
          if (ec) {
            std::cerr << ec.to_string() << std::endl;
            return;
          }
          if (length != sizeof(header_)) {
            return;
          }
          if (header_.type == 1) {
            pool_.barrier(src_);
            read();
          } else if (header_.type == 2) {
            return;
          } else {
            buf_.resize(header_.length);
            offset_ = 0;
            read_content();
          }
        });
  }

  void read_content() {
    auto self = shared_from_this();
    socket_.async_read_some(
        boost::asio::buffer(buf_) + offset_,
        [this, self](boost::system::error_code ec, size_t length) {
          if (ec) {
            std::cerr << ec.to_string() << std::endl;
            return;
          }
          offset_ += length;
          if (offset_ < header_.length) {
            read_content();
          } else {
            pool_.put(src_, header_.tag, std::move(buf_));
            read();
          }
        });
  }

 private:
  boost::asio::ip::tcp::socket socket_;
  MessagePoll& pool_;
  Header header_;
  std::vector<char> buf_;
  int src_;
  size_t offset_;
};

static inline void trim(std::string& str) {
  str.erase(0, str.find_first_not_of(" \t\r\n"));
  str.erase(str.find_last_not_of(" \t\r\n") + 1);
}

class TCPComm {
 private:
  TCPComm()
      : rank_(0), size_(1), local_rank_(0), local_size_(0), barrier_count_(0) {}

 public:
  ~TCPComm() {
    queue_.DecProducerNum();
    read_thread_.join();
    write_thread_.join();
  }

  static TCPComm& get() {
    static TCPComm comm;
    return comm;
  }

  void init(const std::string& hostfile, int self_id) {
    if (hostfile.empty()) {
      CHECK_EQ(self_id, 0) << "self_id must be 0 if hostfile is empty";
      init({}, {}, 0);
      return;
    }
    std::ifstream fin(hostfile);
    std::vector<std::string> addresses, ports;
    for (std::string line; std::getline(fin, line);) {
      trim(line);
      if (line.empty()) {
        continue;
      }
      const char* colon = std::strrchr(line.c_str(), ':');
      if (colon == nullptr) {
        addresses.push_back(line);
        ports.push_back("10000");
      } else {
        std::string addr = std::string(line.c_str(), colon);
        std::string port = std::string(colon + 1);
        trim(addr);
        trim(port);
        addresses.push_back(addr);
        ports.push_back(port);
      }
    }
    init(addresses, ports, self_id);
  }

  void init(const std::vector<std::string>& addresses,
            const std::vector<std::string>& ports, int self_id) {
    size_ = addresses.size();
    rank_ = self_id;
    barrier_count_ = 0;
    pool_.init(size_);

    local_rank_ = 0;
    local_size_ = 0;
    for (int i = 0; i < size_; ++i) {
      if (addresses[i] == addresses[rank_]) {
        ++local_size_;
      }
      if (i == rank_) {
        local_rank_ = local_size_ - 1;
      }
    }

    queue_.SetProducerNum(1);

    std::string self_port = ports[rank_];
    std::mutex read_mutex, write_mutex;
    std::condition_variable read_ready, write_ready;

    read_thread_ = std::thread([this, self_port, &read_mutex, &read_ready]() {
      std::vector<std::shared_ptr<Reader>> readers;
      boost::asio::ip::tcp::acceptor acceptor(
          ioc_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(),
                                               std::stoi(self_port)));
      for (int i = 0; i < size_ - 1; ++i) {
        boost::asio::ip::tcp::socket socket(ioc_);
        acceptor.accept(socket);
        int src;
        socket.read_some(boost::asio::buffer(&src, sizeof(src)));
        readers.emplace_back(
            std::make_shared<Reader>(src, std::move(socket), pool_));
        readers.back()->read();
      }
      {
        std::lock_guard<std::mutex> lock(read_mutex);
        read_ready.notify_all();
      }
      ioc_.run();
    });

    write_thread_ = std::thread([this, addresses, ports, &write_mutex,
                                 &write_ready]() {
      std::vector<std::shared_ptr<boost::asio::ip::tcp::socket>> writers;
      boost::asio::ip::tcp::resolver resolver_(ioc_);
      for (int i = 0; i < size_; ++i) {
        if (i == rank_) {
          writers.emplace_back(nullptr);
          continue;
        }
        auto endpoints = resolver_.resolve(addresses[i], ports[i]);
        boost::asio::ip::tcp::socket socket(ioc_);
        boost::asio::connect(socket, endpoints);
        boost::asio::write(socket, boost::asio::buffer(&rank_, sizeof(rank_)));
        writers.emplace_back(
            std::make_shared<boost::asio::ip::tcp::socket>(std::move(socket)));
      }
      {
        std::lock_guard<std::mutex> lock(write_mutex);
        write_ready.notify_all();
      }

      Header header;
      std::tuple<int, int, int, std::vector<char>> item;
      while (queue_.Get(item)) {
        int type = std::get<0>(item);
        int dst = std::get<1>(item);
        int tag = std::get<2>(item);
        std::vector<char>& buf = std::get<3>(item);
        if (type == 1) {
          // barrier
          header.type = 1;
          boost::asio::write(*writers[dst],
                             boost::asio::buffer(&header, sizeof(header)));
        } else if (type == 3) {
          // bcast
          header.type = 0;
          header.tag = tag;
          header.length = buf.size();
          for (int i = 1; i < size_; ++i) {
            int dst_worker_id = (rank_ + i) % size_;
            boost::asio::write(*writers[dst_worker_id],
                               boost::asio::buffer(&header, sizeof(header)));
            if (header.length > 0) {
              boost::asio::write(*writers[dst_worker_id],
                                 boost::asio::buffer(buf));
            }
          }
        } else if (type == 0) {
          // normal
          header.type = 0;
          header.tag = tag;
          header.length = buf.size();
          boost::asio::write(*writers[dst],
                             boost::asio::buffer(&header, sizeof(header)));
          if (header.length > 0) {
            boost::asio::write(*writers[dst], boost::asio::buffer(buf));
          }
        } else {
          // unexpected
          LOG(INFO) << "Unexpected message type - " << type;
        }
      }
      // exit
      header.type = 2;
      for (int i = 0; i < size_; ++i) {
        if (i == rank_) {
          continue;
        }
        boost::asio::write(*writers[i],
                           boost::asio::buffer(&header, sizeof(header)));
      }
    });

    {
      std::unique_lock<std::mutex> lock(read_mutex);
      read_ready.wait(lock);
    }
    {
      std::unique_lock<std::mutex> lock(write_mutex);
      write_ready.wait(lock);
    }
  }

  int rank() const { return rank_; }
  int size() const { return size_; }

  int local_rank() const { return local_rank_; }
  int local_size() const { return local_size_; }

  void barrier() {
    ++barrier_count_;
    if (size_ == 1) {
      return;
    } else {
      if (rank_ == 0) {
        pool_.wait_barrier_master(barrier_count_);
        for (int i = 1; i < size_; ++i) {
          push_barrier((i + rank_) % size_);
        }
      } else {
        push_barrier(0);
        pool_.wait_barrier_slave(barrier_count_);
      }
    }
  }

  void send(int dst, std::vector<char>&& buf, int tag) {
    if (dst == rank_) {
      pool_.put(rank_, tag, std::move(buf));
    } else {
      push_data(dst, tag, std::move(buf));
    }
  }

  void send(int dst, const char* buf, size_t count, int tag) {
    std::vector<char> vec(count);
    memcpy(vec.data(), buf, count);
    send(dst, std::move(vec), tag);
  }

  void send(int dst, const std::vector<char>& buf, int tag) {
    send(dst, buf.data(), buf.size(), tag);
  }

  void send_empty(int dst, int tag) {
    if (dst == rank_) {
      pool_.put(rank_, tag, std::vector<char>());
    } else {
      push_empty(dst, tag);
    }
  }

  void wait_send() {}

  void recv(int& src, std::vector<char>& buf, int& tag) {
    pool_.take(src, tag, buf);
  }

  void recv_from(int src, std::vector<char>& buf, int& tag) {
    pool_.take_from(src, tag, buf);
  }

  void recv_tagged(int& src, std::vector<char>& buf, int tag) {
    pool_.take_tagged(src, tag, buf);
  }

  void recv_from_tagged(int src, std::vector<char>& buf, int tag) {
    pool_.take_from_tagged(src, tag, buf);
  }

  int64_t sum(int64_t input) {
    int64_t ret;
    sum(&input, &ret, 1);
    return ret;
  }

  void sum(int64_t* input, int64_t* output, size_t count) {
    static constexpr int sum_tag = std::numeric_limits<int>::max() - 16;
    static constexpr int sum_ack_tag = std::numeric_limits<int>::max() - 15;
    if (rank_ == 0) {
      if (input != output) {
        memcpy(output, input, count * sizeof(int64_t));
      }
      for (int i = 1; i < size_; ++i) {
        std::vector<char> recv_buf;
        recv_from_tagged(i, recv_buf, sum_tag);
        int64_t* recv_ptr = reinterpret_cast<int64_t*>(recv_buf.data());
        for (int j = 0; j < count; ++j) {
          output[j] += recv_ptr[j];
        }
      }
      for (int i = 1; i < size_; ++i) {
        send(i, reinterpret_cast<char*>(output), count * sizeof(int64_t),
             sum_ack_tag);
      }
    } else {
      send(0, reinterpret_cast<char*>(input), count * sizeof(int64_t), sum_tag);
      std::vector<char> recv_buf;
      recv_from_tagged(0, recv_buf, sum_ack_tag);
      memcpy(output, recv_buf.data(), count * sizeof(int64_t));
    }
  }

  template <typename T>
  void gather(const T& input, std::vector<T>& output) {
    static const int gather_tag = std::numeric_limits<int>::max() - 14;
    {
      InArchive iarc;
      iarc << input;
      push_bcast(gather_tag, std::move(iarc.GetBufferVector()));
    }
    output[rank_] = input;
    for (int i = 1; i < size_; ++i) {
      int src_worker_id = (rank_ + size_ - 1) % size_;
      std::vector<char> recv_buf;
      recv_from_tagged(src_worker_id, recv_buf, gather_tag);
      OutArchive arc;
      arc.SetSlice(recv_buf.data(), recv_buf.size());
      arc >> output[src_worker_id];
    }
  }

  template <typename T>
  void all_to_all(const std::vector<T>& input, std::vector<T>& output) {
    static const int all_to_all_tag = std::numeric_limits<int>::max() - 13;
    int worker_num = size();
    int worker_id = rank();

    CHECK_EQ(input.size(), worker_num);
    output.resize(worker_num);

    for (int i = 1; i < worker_num; ++i) {
      int dst_worker_id = (worker_id + i) % worker_num;
      InArchive arc;
      arc << input[dst_worker_id];
      send(dst_worker_id, std::move(arc.GetBufferVector()), all_to_all_tag);
    }
    output[worker_id] = input[worker_id];
    for (int i = 1; i < worker_num; ++i) {
      int src_worker_id = (worker_id + worker_num - i) % worker_num;
      std::vector<char> buf;
      recv_from_tagged(src_worker_id, buf, all_to_all_tag);
      OutArchive arc;
      arc.SetSlice(buf.data(), buf.size());
      arc >> output[src_worker_id];
    }
  }

  template <typename T>
  void bcast(T& val, int root) {
    static const int bcast_tag = std::numeric_limits<int>::max() - 12;
    int worker_id = rank();
    if (worker_id == root) {
      InArchive arc;
      arc << val;
      push_bcast(bcast_tag, std::move(arc.GetBufferVector()));
    } else {
      std::vector<char> buf;
      recv_from_tagged(root, buf, bcast_tag);
      OutArchive arc;
      arc.SetSlice(buf.data(), buf.size());
      arc >> val;
    }
  }

 private:
  void push_exit() {
    queue_.Put(std::make_tuple(2, 0, 0, std::vector<char>()));
  }

  void push_empty(int dst, int tag) {
    queue_.Put(std::make_tuple(0, dst, tag, std::vector<char>()));
  }

  void push_data(int dst, int tag, std::vector<char>&& data) {
    queue_.Put(std::make_tuple(0, dst, tag, std::move(data)));
  }

  void push_bcast(int tag, std::vector<char>&& data) {
    queue_.Put(std::make_tuple(3, 0, tag, std::move(data)));
  }

  void push_barrier(int dst) {
    queue_.Put(std::make_tuple(1, dst, 0, std::vector<char>()));
  }

  boost::asio::io_context ioc_;
  MessagePoll pool_;
  // BlockingQueue queue_;
  BlockingQueue<std::tuple<int, int, int, std::vector<char>>> queue_;

  std::thread read_thread_;
  std::thread write_thread_;

  int rank_;
  int size_;

  int local_rank_;
  int local_size_;

  int barrier_count_;
};

using CommType = TCPComm;

}  // namespace grape

#endif

#endif  // GRAPE_COMMUNICATION_TCP_COMM_H_
