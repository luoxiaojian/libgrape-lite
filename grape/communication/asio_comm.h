#ifndef GRAPE_COMMUNICATION_ASIO_COMM_H_
#define GRAPE_COMMUNICATION_ASIO_COMM_H_

#ifndef USE_MPI

#include <boost/asio.hpp>

#include <condition_variable>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "glog/logging.h"

#include "grape/communication/comm_base.h"
#include "grape/serialization/in_archive.h"
#include "grape/serialization/out_archive.h"
#include "grape/utils/concurrent_queue.h"

#include "flat_hash_map/flat_hash_map.hpp"

namespace grape {

namespace asio_comm_constants {

static constexpr int reserved_tag_num = 16;
static constexpr int reserved_tag_base =
    std::numeric_limits<int>::max() - reserved_tag_num;
static constexpr int sum_tag = reserved_tag_base;
static constexpr int sum_ack_tag = reserved_tag_base + 1;
static constexpr int gather_tag = reserved_tag_base + 2;
static constexpr int all_to_all_tag = reserved_tag_base + 3;
static constexpr int bcast_tag = reserved_tag_base + 4;

}  // namespace asio_comm_constants

class AsioMessagePool {
 public:
  AsioMessagePool() : worker_num_(0), comm_num_(0) {}
  ~AsioMessagePool() {}

  void init(int worker_num) {
    worker_num_ = worker_num;
    comm_num_ = 1;

    size_t n = worker_num;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (pool_.size() < n) {
        pool_.resize(n);
      }
    }
    {
      std::lock_guard<std::mutex> lock(reserved_mutex_);
      if (reserved_pool_.size() < n) {
        reserved_pool_.resize(n);
      }
    }
    {
      std::lock_guard<std::mutex> lock(barrier_mutex_);
      if (barrier_count_.size() < n) {
        barrier_count_.resize(n, 0);
      }
    }
  }

  int allocate_comm() {
    int cn = comm_num_.fetch_add(1);
    size_t new_size = static_cast<size_t>((cn + 1) * worker_num_);

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (pool_.size() < new_size) {
        pool_.resize(new_size);
      }
    }
    {
      std::lock_guard<std::mutex> lock(reserved_mutex_);
      if (reserved_pool_.size() < new_size) {
        reserved_pool_.resize(new_size);
      }
    }
    {
      std::lock_guard<std::mutex> lock(barrier_mutex_);
      if (barrier_count_.size() < new_size) {
        barrier_count_.resize(new_size, 0);
      }
    }

    return cn;
  }

  void put(int comm_id, int src, int tag, std::vector<char>&& buf) {
    size_t idx = comm_id * worker_num_ + src;
    if (tag < asio_comm_constants::reserved_tag_base) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (pool_.size() <= idx) {
        pool_.resize((comm_id + 1) * worker_num_);
      }
      pool_[idx][tag].emplace_back(std::move(buf));
      cond_.notify_all();
    } else {
      std::lock_guard<std::mutex> lock(reserved_mutex_);
      if (reserved_pool_.size() <= idx) {
        reserved_pool_.resize((comm_id + 1) * worker_num_);
      }
      reserved_pool_[idx][tag - asio_comm_constants::reserved_tag_base]
          .emplace_back(std::move(buf));
      reserved_cond_.notify_all();
    }
  }

  void take(int comm_id, int& src, int& tag, std::vector<char>& buf) {
    size_t min_size = (comm_id + 1) * worker_num_;
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this, comm_id, &src, min_size] {
      if (pool_.size() < min_size) {
        return false;
      }
      for (int i = 0; i < worker_num_; ++i) {
        if (!pool_[comm_id * worker_num_ + i].empty()) {
          src = i;
          return true;
        }
      }
      return false;
    });
    int idx = comm_id * worker_num_ + src;
    auto it = pool_[idx].begin();
    tag = it->first;
    buf = std::move(it->second.front());
    it->second.pop_front();
    if (it->second.empty()) {
      pool_[idx].erase(it);
    }
  }

  void take_from(int comm_id, int src, int& tag, std::vector<char>& buf) {
    size_t idx = comm_id * worker_num_ + src;
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this, idx] {
      return pool_.size() > idx && !pool_[idx].empty();
    });
    auto it = pool_[idx].begin();
    tag = it->first;
    buf = std::move(it->second.front());
    it->second.pop_front();
    if (it->second.empty()) {
      pool_[idx].erase(it);
    }
  }

  void take_tagged(int comm_id, int& src, int tag, std::vector<char>& buf) {
    size_t min_size = (comm_id + 1) * worker_num_;
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this, tag, comm_id, &src, min_size] {
      if (pool_.size() < min_size) {
        return false;
      }
      for (int i = 0; i != worker_num_; ++i) {
        int idx = comm_id * worker_num_ + i;
        if (pool_[idx].find(tag) != pool_[idx].end()) {
          src = i;
          return true;
        }
      }
      return false;
    });
    int idx = comm_id * worker_num_ + src;
    auto it = pool_[idx].find(tag);
    buf = std::move(it->second.front());
    it->second.pop_front();
    if (it->second.empty()) {
      pool_[idx].erase(it);
    }
  }

  void take_tagged_reserved(int comm_id, int& src, int tag,
                            std::vector<char>& buf) {
    size_t min_size = (comm_id + 1) * worker_num_;
    std::unique_lock<std::mutex> lock(reserved_mutex_);
    reserved_cond_.wait(lock, [this, tag, comm_id, &src, min_size] {
      if (reserved_pool_.size() < min_size) {
        return false;
      }
      for (int i = 0; i != worker_num_; ++i) {
        int idx = comm_id * worker_num_ + i;
        auto& deq =
            reserved_pool_[idx][tag - asio_comm_constants::reserved_tag_base];
        if (!deq.empty()) {
          src = i;
          return true;
        }
      }
      return false;
    });
    int idx = comm_id * worker_num_ + src;
    auto& deq =
        reserved_pool_[idx][tag - asio_comm_constants::reserved_tag_base];
    buf = std::move(deq.front());
    deq.pop_front();
  }

  void take_from_tagged(int comm_id, int src, int tag, std::vector<char>& buf) {
    size_t idx = comm_id * worker_num_ + src;
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this, idx, tag] {
      return pool_.size() > idx && pool_[idx].find(tag) != pool_[idx].end();
    });
    auto it = pool_[idx].find(tag);
    buf = std::move(it->second.front());
    it->second.pop_front();
    if (it->second.empty()) {
      pool_[idx].erase(it);
    }
  }

  void take_from_tagged_reserved(int comm_id, int src, int tag,
                                 std::vector<char>& buf) {
    size_t idx = comm_id * worker_num_ + src;
    std::unique_lock<std::mutex> lock(reserved_mutex_);
    reserved_cond_.wait(lock, [this, idx, tag] {
      return reserved_pool_.size() > idx &&
             !reserved_pool_[idx][tag - asio_comm_constants::reserved_tag_base]
                  .empty();
    });
    auto& deq =
        reserved_pool_[idx][tag - asio_comm_constants::reserved_tag_base];
    buf = std::move(deq.front());
    deq.pop_front();
  }

  void barrier(int comm_id, int src) {
    size_t min_size = (comm_id + 1) * worker_num_;
    std::lock_guard<std::mutex> lock(barrier_mutex_);
    if (barrier_count_.size() < min_size) {
      barrier_count_.resize(min_size, 0);
    }
    ++barrier_count_[comm_id * worker_num_ + src];
    barrier_cond_.notify_all();
  }

  void wait_barrier_slave(int comm_id, int expected) {
    size_t idx = comm_id * worker_num_;
    std::unique_lock<std::mutex> lock(barrier_mutex_);
    barrier_cond_.wait(lock, [this, expected, idx] {
      return barrier_count_.size() > idx && barrier_count_[idx] >= expected;
    });
  }

  void wait_barrier_master(int comm_id, int expected) {
    size_t min_size = (comm_id + 1) * worker_num_;
    std::unique_lock<std::mutex> lock(barrier_mutex_);
    barrier_cond_.wait(lock, [this, expected, comm_id, min_size] {
      if (barrier_count_.size() < min_size) {
        return false;
      }
      for (int i = 1; i < worker_num_; ++i) {
        if (barrier_count_[comm_id * worker_num_ + i] < expected) {
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

  std::mutex reserved_mutex_;
  std::condition_variable reserved_cond_;
  std::vector<std::array<std::deque<std::vector<char>>,
                         asio_comm_constants::reserved_tag_num>>
      reserved_pool_;

  std::mutex barrier_mutex_;
  std::condition_variable barrier_cond_;
  std::vector<int> barrier_count_;

  int worker_num_;
  std::atomic<int> comm_num_;
};

class AsioCommAllocator;

enum AsioMsgType {
  kData,
  kBarrier,
  kBcast,
  kExit,
};

struct AsioHeader {
  size_t length;
  int tag;
  int comm_id;
  AsioMsgType type;
};

class AsioReader : public std::enable_shared_from_this<AsioReader> {
 public:
  AsioReader(int src, boost::asio::ip::tcp::socket& socket,
             AsioMessagePool& pool)
      : socket_(socket), pool_(pool), src_(src) {}

  void read() {
    offset_ = 0;
    read_header();
  }

  void read_header() {
    auto self = shared_from_this();
    socket_.async_read_some(
        boost::asio::buffer(reinterpret_cast<char*>(&header_) + offset_,
                            sizeof(header_) - offset_),
        [self, this](boost::system::error_code ec, size_t length) {
          if (ec) {
            LOG(ERROR) << "recv crash, " << ec.message();
          }
          offset_ += length;
          if (offset_ < sizeof(AsioHeader)) {
            read_header();
          } else {
            CHECK_EQ(offset_, sizeof(AsioHeader));
            process_header();
          }
        });
  }

  void process_header() {
    if (header_.type == kBarrier) {
      pool_.barrier(header_.comm_id, src_);
      read();
    } else if (header_.type == kExit) {
      return;
    } else {
      if (header_.length > 0) {
        buf_.resize(header_.length);
        offset_ = 0;
        read_content();
      } else {
        pool_.put(header_.comm_id, src_, header_.tag, std::vector<char>());
        read();
      }
    }
  }

  void read_content() {
    auto self = shared_from_this();
    socket_.async_read_some(
        boost::asio::buffer(buf_) + offset_,
        [this, self](boost::system::error_code ec, size_t length) {
          if (ec) {
            std::cerr << ec.message() << std::endl;
            return;
          }
          offset_ += length;
          if (offset_ < header_.length) {
            read_content();
          } else {
            pool_.put(header_.comm_id, src_, header_.tag, std::move(buf_));
            read();
          }
        });
  }

 private:
  boost::asio::ip::tcp::socket& socket_;
  AsioMessagePool& pool_;
  AsioHeader header_;
  std::vector<char> buf_;
  int src_;
  size_t offset_;
};

class AsioWriter : public std::enable_shared_from_this<AsioWriter> {
 public:
  AsioWriter(
      boost::asio::ip::tcp::socket& socket,
      BlockingQueue<std::tuple<AsioMsgType, int, int, std::vector<char>>>& que)
      : socket_(socket), que_(que) {}

  void write() {
    std::tuple<AsioMsgType, int, int, std::vector<char>> item;
    if (que_.Get(item)) {
      AsioMsgType type = std::get<0>(item);
      int comm_id = std::get<1>(item);
      int tag = std::get<2>(item);
      buf_ = std::move(std::get<3>(item));

      if (type == kBarrier) {
        header_.type = kBarrier;
        header_.comm_id = comm_id;
      } else if (type == kBcast) {
        header_.type = kBcast;
        header_.comm_id = comm_id;
        header_.tag = tag;
        header_.length = buf_.size();
      } else if (type == kData) {
        header_.type = kData;
        header_.comm_id = comm_id;
        header_.tag = tag;
        header_.length = buf_.size();
      } else {
        LOG(ERROR) << "Unexpected message type - " << type;
      }
    } else {
      header_.type = kExit;
    }
    offset_ = 0;
    write_header();
  }

  void write_header() {
    auto self = shared_from_this();
    socket_.async_write_some(
        boost::asio::buffer(reinterpret_cast<char*>(&header_) + offset_,
                            sizeof(header_) - offset_),
        [self, this](boost::system::error_code ec, size_t length) {
          if (ec) {
            LOG(ERROR) << "send crash, " << ec.message();
            return;
          }
          offset_ += length;
          if (offset_ < sizeof(AsioHeader)) {
            write_header();
          } else {
            CHECK_EQ(offset_, sizeof(AsioHeader));
            if ((header_.type == kData || header_.type == kBcast) &&
                header_.length != 0) {
              offset_ = 0;
              write_content();
            } else if (header_.type != kExit) {
              write();
            }
          }
        });
  }

  void write_content() {
    auto self = shared_from_this();
    socket_.async_write_some(
        boost::asio::buffer(buf_) + offset_,
        [this, self](boost::system::error_code ec, size_t length) {
          if (ec) {
            LOG(ERROR) << "send crash, " << ec.message();
            return;
          }
          offset_ += length;
          if (offset_ < header_.length) {
            write_content();
          } else {
            write();
          }
        });
  }

 private:
  boost::asio::ip::tcp::socket& socket_;
  BlockingQueue<std::tuple<AsioMsgType, int, int, std::vector<char>>>& que_;

  std::vector<char> buf_;
  AsioHeader header_;
  size_t offset_;
};

class AsioComm : public CommBase {
 public:
  AsioComm()
      : pool_(nullptr),
        queue_(nullptr),
        comm_id_(-1),
        rank_(0),
        size_(1),
        local_rank_(0),
        local_size_(1),
        barrier_count_(0) {}
  AsioComm(AsioComm&& rhs)
      : pool_(rhs.pool_),
        queue_(rhs.queue_),
        comm_id_(rhs.comm_id_),
        rank_(rhs.rank_),
        size_(rhs.size_),
        local_rank_(rhs.local_rank_),
        local_size_(rhs.local_size_) {
    rhs.pool_ = nullptr;
    rhs.queue_ = nullptr;
    rhs.comm_id_ = -1;
    rhs.rank_ = 0;
    rhs.size_ = 1;
    rhs.local_rank_ = 0;
    rhs.local_size_ = 1;
    rhs.barrier_count_ = 0;
  }

  AsioComm& operator=(AsioComm&& rhs) {
    if (this == &rhs) {
      return *this;
    }
    pool_ = rhs.pool_;
    queue_ = rhs.queue_;
    comm_id_ = rhs.comm_id_;
    rank_ = rhs.rank_;
    size_ = rhs.size_;
    local_rank_ = rhs.local_rank_;
    local_size_ = rhs.local_size_;
    barrier_count_ = rhs.barrier_count_;
    rhs.pool_ = nullptr;
    rhs.queue_ = nullptr;
    rhs.comm_id_ = -1;
    rhs.rank_ = 0;
    rhs.size_ = 1;
    rhs.local_rank_ = 0;
    rhs.local_size_ = 1;
    rhs.barrier_count_ = 0;

    return *this;
  }

  AsioComm(const AsioComm&) = delete;
  ~AsioComm() {}

 private:
  AsioComm(AsioMessagePool* pool,
           BlockingQueue<std::tuple<AsioMsgType, int, int, std::vector<char>>>*
               queue,
           int comm_id, int rank, int size, int local_rank, int local_size)
      : pool_(pool),
        queue_(queue),
        comm_id_(comm_id),
        rank_(rank),
        size_(size),
        local_rank_(local_rank),
        local_size_(local_size),
        barrier_count_(0) {}

 public:
  int rank() const override { return rank_; }
  int size() const override { return size_; }

  int local_rank() const override { return local_rank_; }
  int local_size() const override { return local_size_; }

  void barrier() override {
    ++barrier_count_;
    if (size_ == 1) {
      return;
    } else {
      if (rank_ == 0) {
        pool_->wait_barrier_master(comm_id_, barrier_count_);
        for (int i = 1; i < size_; ++i) {
          push_barrier((i + rank_) % size_);
        }
      } else {
        push_barrier(0);
        pool_->wait_barrier_slave(comm_id_, barrier_count_);
      }
    }
  }

  void send(int dst, std::vector<char>&& buf, int tag) override {
    CHECK_LT(tag, asio_comm_constants::reserved_tag_base);
    if (dst == rank_) {
      pool_->put(comm_id_, rank_, tag, std::move(buf));
    } else {
      push_data(dst, tag, std::move(buf));
    }
  }

  void send(int dst, const char* buf, size_t count, int tag) override {
    std::vector<char> vec(count);
    memcpy(vec.data(), buf, count);
    send(dst, std::move(vec), tag);
  }

  void send(int dst, const std::vector<char>& buf, int tag) override {
    send(dst, buf.data(), buf.size(), tag);
  }

  void send_empty(int dst, int tag) override {
    CHECK_LT(tag, asio_comm_constants::reserved_tag_base);
    if (dst == rank_) {
      pool_->put(comm_id_, rank_, tag, std::vector<char>());
    } else {
      push_empty(dst, tag);
    }
  }

  void wait_send() override {}

  void recv(int& src, std::vector<char>& buf, int& tag) override {
    pool_->take(comm_id_, src, tag, buf);
    CHECK_LT(tag, asio_comm_constants::reserved_tag_base);
  }

  void recv_from(int src, std::vector<char>& buf, int& tag) override {
    pool_->take_from(comm_id_, src, tag, buf);
    CHECK_LT(tag, asio_comm_constants::reserved_tag_base);
  }

  void recv_tagged(int& src, std::vector<char>& buf, int tag) override {
    CHECK_LT(tag, asio_comm_constants::reserved_tag_base);
    pool_->take_tagged(comm_id_, src, tag, buf);
  }

  void recv_from_tagged(int src, std::vector<char>& buf, int tag) override {
    CHECK_LT(tag, asio_comm_constants::reserved_tag_base);
    pool_->take_from_tagged(comm_id_, src, tag, buf);
  }

  int64_t sum(int64_t input) override {
    int64_t ret;
    sum(&input, &ret, 1);
    return ret;
  }

  void sum(int64_t* input, int64_t* output, size_t count) override {
    static constexpr int sum_tag = std::numeric_limits<int>::max() - 16;
    static constexpr int sum_ack_tag = std::numeric_limits<int>::max() - 15;
    if (rank_ == 0) {
      if (input != output) {
        memcpy(output, input, count * sizeof(int64_t));
      }
      for (int i = 1; i < size_; ++i) {
        std::vector<char> recv_buf;
        int src_worker_id;
        recv_tagged_reserved(src_worker_id, recv_buf, sum_tag);
        int64_t* recv_ptr = reinterpret_cast<int64_t*>(recv_buf.data());
        for (size_t j = 0; j < count; ++j) {
          output[j] += recv_ptr[j];
        }
      }
      std::vector<char> send_buf(count * sizeof(int64_t));
      memcpy(send_buf.data(), output, count * sizeof(int64_t));
      push_bcast(sum_ack_tag, std::move(send_buf));
    } else {
      send_reserved(0, reinterpret_cast<char*>(input), count * sizeof(int64_t),
                    sum_tag);
      std::vector<char> recv_buf;
      recv_from_tagged_reserved(0, recv_buf, sum_ack_tag);
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
      int src_worker_id;
      std::vector<char> recv_buf;
      recv_tagged_reserved(src_worker_id, recv_buf, gather_tag);
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
      send_reserved(dst_worker_id, std::move(arc.GetBufferVector()),
                    all_to_all_tag);
    }
    output[worker_id] = input[worker_id];
    for (int i = 1; i < worker_num; ++i) {
      int src_worker_id;
      std::vector<char> buf;
      recv_tagged_reserved(src_worker_id, buf, all_to_all_tag);
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
      recv_from_tagged_reserved(root, buf, bcast_tag);
      OutArchive arc;
      arc.SetSlice(buf.data(), buf.size());
      arc >> val;
    }
  }

 private:
  void send_reserved(int dst, std::vector<char>&& buf, int tag) {
    CHECK_NE(dst, rank_);
    CHECK_GE(tag, asio_comm_constants::reserved_tag_base);
    push_data(dst, tag, std::move(buf));
  }

  void send_reserved(int dst, const char* buf, size_t count, int tag) {
    std::vector<char> vec(count);
    memcpy(vec.data(), buf, count);
    send_reserved(dst, std::move(vec), tag);
  }

  void recv_tagged_reserved(int& src, std::vector<char>& buf, int tag) {
    CHECK_GE(tag, asio_comm_constants::reserved_tag_base);
    pool_->take_tagged_reserved(comm_id_, src, tag, buf);
  }

  void recv_from_tagged_reserved(int src, std::vector<char>& buf, int tag) {
    CHECK_GE(tag, asio_comm_constants::reserved_tag_base);
    pool_->take_from_tagged_reserved(comm_id_, src, tag, buf);
  }

  void push_exit() {
    for (int i = 1; i < size_; ++i) {
      int target = (rank_ + i) % size_;
      queue_[target].Put(std::make_tuple(kExit, -1, -1, std::vector<char>()));
    }
  }

  void push_empty(int dst, int tag) {
    queue_[dst].Put(std::make_tuple(kData, comm_id_, tag, std::vector<char>()));
  }

  void push_data(int dst, int tag, std::vector<char>&& data) {
    queue_[dst].Put(std::make_tuple(kData, comm_id_, tag, std::move(data)));
  }

  void push_bcast(int tag, std::vector<char>&& data) {
    for (int i = 2; i < size_; ++i) {
      int target = (rank_ + i) % size_;
      queue_[target].Put(std::make_tuple(kBcast, comm_id_, tag, data));
    }
    if (size_ > 1) {
      queue_[(1 + rank_) % size_].Put(
          std::make_tuple(kBcast, comm_id_, tag, std::move(data)));
    }
  }

  void push_barrier(int dst) {
    queue_[dst].Put(
        std::make_tuple(kBarrier, comm_id_, -1, std::vector<char>()));
  }

  AsioMessagePool* pool_;
  // type, comm_id, dst, tag, data
  // data: kData, comm_id, dst, tag, data
  // barrier: kBarrier, comm_id
  // exit: kExit
  // bcast: kBcast, comm_id, root, tag, data
  BlockingQueue<std::tuple<AsioMsgType, int, int, std::vector<char>>>* queue_;
  int comm_id_;

  int rank_;
  int size_;
  int local_rank_;
  int local_size_;
  int barrier_count_;

  friend class AsioCommAllocator;
};

static inline void trim(std::string& str) {
  str.erase(0, str.find_first_not_of(" \t\r\n"));
  str.erase(str.find_last_not_of(" \t\r\n") + 1);
}

static inline std::shared_ptr<boost::asio::ip::tcp::socket> loop_connect(
    boost::asio::io_context& ioc,
    const boost::asio::ip::basic_resolver_results<boost::asio::ip::tcp>&
        endpoints) {
  boost::system::error_code ec;
  while (true) {
    for (auto& endpoint : endpoints) {
      auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioc);
      socket->connect(endpoint, ec);
      if (!ec) {
        return socket;
      }
      VLOG(2) << "connect failed: " << ec.message();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  return nullptr;
}

class AsioCommAllocator {
 public:
  AsioCommAllocator() {}
  ~AsioCommAllocator() {
    for (int i = 0; i < size_; ++i) {
      send_queue_[i].DecProducerNum();
    }
    for (auto& thrd : send_threads_) {
      thrd.join();
    }
    for (auto& thrd : recv_threads_) {
      thrd.join();
    }

    delete[] send_queue_;
  }

  void init(const std::string& hostfile, int self_id, int worker_num) {
    if (hostfile.empty()) {
      std::vector<std::string> addresses, ports;
      for (int i = 0; i < worker_num; ++i) {
        addresses.emplace_back("localhost");
        ports.emplace_back(std::to_string(10000 + i));
      }
      init(addresses, ports, self_id);
      return;
    }
    std::ifstream fin(hostfile);
    std::vector<std::string> addresses, ports;
    std::map<std::string, int> address_to_port;
    for (std::string line; std::getline(fin, line);) {
      trim(line);
      if (line.empty()) {
        continue;
      }
      const char* colon = std::strrchr(line.c_str(), ':');
      if (colon == nullptr) {
        addresses.push_back(line);
        auto iter = address_to_port.find(addresses.back());
        if (iter == address_to_port.end()) {
          address_to_port.emplace(addresses.back(), 10000);
          ports.push_back("10000");
        } else {
          ports.emplace_back(std::to_string(++iter->second));
        }
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
    int cur_size = addresses.size();
    if (cur_size > worker_num) {
      addresses.resize(worker_num);
      ports.resize(worker_num);
    } else {
      int remaining = worker_num - cur_size;
      for (int i = 0; i < remaining; ++i) {
        std::string cur_addr = addresses[i % worker_num];
        int cur_port = std::stoi(ports[i % worker_num]) + (i / worker_num) + 1;
        addresses.push_back(cur_addr);
        ports.push_back(std::to_string(cur_port));
      }
    }
    init(addresses, ports, self_id);
  }

  void init(const std::vector<std::string>& addresses,
            const std::vector<std::string>& ports, int self_id) {
    size_ = addresses.size();
    rank_ = self_id;

    local_rank_ = 0;
    local_size_ = 0;
    for (int i = 0; i < size_; ++i) {
      if (addresses[i] == addresses[rank_]) {
        ++local_size_;
        if (i == rank_) {
          local_rank_ = local_size_ - 1;
        }
      }
    }

    send_queue_ = new BlockingQueue<
        std::tuple<AsioMsgType, int, int, std::vector<char>>>[size_];
    for (int i = 0; i < size_; ++i) {
      send_queue_[i].SetProducerNum(1);
    }
    pool_.init(size_);

    sockets_.clear();
    sockets_.resize(size_, nullptr);
    if (size_ > 1) {
      init_sockets(addresses, ports);
    }

    for (int i = 1; i < size_; ++i) {
      int dst_worker_id = (i + rank_) % size_;
      send_threads_.emplace_back(
          [&, this](int target) {
            auto& que = send_queue_[target];
            auto& socket = *sockets_[target];
            auto writer = std::make_shared<AsioWriter>(socket, que);
            writer->write();
            ioc_.run();
            VLOG(2) << "send thread returned..";
          },
          dst_worker_id);
    }

    for (int i = 1; i < size_; ++i) {
      int src_worker_id = (i + rank_) % size_;
      recv_threads_.emplace_back(
          [&, this](int source) {
            auto& socket = *sockets_[source];
            auto reader = std::make_shared<AsioReader>(source, socket, pool_);
            reader->read();
            ioc_.run();
            VLOG(2) << "recv thread returned..";
          },
          src_worker_id);
    }
  }

  static AsioCommAllocator& get() {
    static AsioCommAllocator allocator;
    return allocator;
  }

  AsioComm allocate() {
    int comm_id = pool_.allocate_comm();
    return AsioComm(&pool_, send_queue_, comm_id, rank_, size_, local_rank_,
                    local_size_);
  }

  int rank() const { return rank_; }
  int size() const { return size_; }
  int local_rank() const { return local_rank_; }
  int local_size() const { return local_size_; }

 private:
  void init_sockets(const std::vector<std::string>& addresses,
                    const std::vector<std::string>& ports) {
    std::thread connect_thread([&, this]() {
      boost::asio::ip::tcp::resolver resolver(ioc_);
      for (int dst_worker_id = 0; dst_worker_id != rank_; ++dst_worker_id) {
        auto endpoints =
            resolver.resolve(boost::asio::ip::tcp::v4(),
                             addresses[dst_worker_id], ports[dst_worker_id]);
        auto socket = loop_connect(ioc_, endpoints);
        socket->set_option(boost::asio::ip::tcp::no_delay(true));
        VLOG(2) << "[worker-" << rank_ << "] connected to [worker-"
                << dst_worker_id << "]";
        socket->write_some(boost::asio::buffer(&rank_, sizeof(int)));
        sockets_[dst_worker_id] = socket;
      }
    });
    std::thread accept_thread([&, this]() {
      boost::asio::ip::tcp::acceptor acceptor(
          ioc_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(),
                                               std::stoi(ports[rank_])));
      acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
      for (int src_worker_id = rank_ + 1; src_worker_id != size_;
           ++src_worker_id) {
        auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioc_);
        acceptor.accept(*socket);
        socket->set_option(boost::asio::ip::tcp::no_delay(true));
        int src;
        socket->read_some(boost::asio::buffer(&src, sizeof(int)));
        VLOG(2) << "[worker-" << rank_ << "] accepted from [worker-" << src
                << "]";
        sockets_[src] = socket;
      }
    });

    connect_thread.join();
    accept_thread.join();
  }

  int rank_;
  int size_;
  int local_rank_;
  int local_size_;

  std::vector<std::thread> send_threads_;
  std::vector<std::thread> recv_threads_;

  boost::asio::io_context ioc_;

  AsioMessagePool pool_;
  // type, comm_id, dst, tag, data
  BlockingQueue<std::tuple<AsioMsgType, int, int, std::vector<char>>>*
      send_queue_;

  std::vector<std::shared_ptr<boost::asio::ip::tcp::socket>> sockets_;
};

using CommAllocatorType = AsioCommAllocator;
using CommType = AsioComm;

}  // namespace grape

#endif

#endif  // GRAPE_COMMUNICATION_ASIO_COMM_H_
