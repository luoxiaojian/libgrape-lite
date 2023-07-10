#include <fstream>

#ifdef USE_ASIO

#include "grape/communication/asio_comm.h"

namespace grape {

AsioComm::AsioComm()
    : pool_(nullptr),
      queue_(nullptr),
      comm_id_(-1),
      rank_(0),
      size_(1),
      local_rank_(0),
      local_size_(1),
      barrier_count_(0) {}

AsioComm::AsioComm(AsioComm&& rhs)
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

AsioComm& AsioComm::operator=(AsioComm&& rhs) {
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

AsioComm::AsioComm(
    asio_comm::MessagePool* pool,
    BlockingQueue<std::tuple<asio_comm::MsgType, int, int, std::vector<char>>>*
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

int AsioComm::rank() const { return rank_; }
int AsioComm::size() const { return size_; }

int AsioComm::local_rank() const { return local_rank_; }
int AsioComm::local_size() const { return local_size_; }

void AsioComm::barrier() {
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

void AsioComm::send(int dst, std::vector<char>&& buf, int tag) {
  CHECK_LT(tag, asio_comm::reserved_tag_base);
  if (dst == rank_) {
    pool_->put(comm_id_, rank_, tag, std::move(buf));
  } else {
    push_data(dst, tag, std::move(buf));
  }
}

void AsioComm::send(int dst, const char* buf, size_t count, int tag) {
  std::vector<char> vec(count);
  memcpy(vec.data(), buf, count);
  send(dst, std::move(vec), tag);
}

void AsioComm::send(int dst, const std::vector<char>& buf, int tag) {
  send(dst, buf.data(), buf.size(), tag);
}

void AsioComm::send_empty(int dst, int tag) {
  CHECK_LT(tag, asio_comm::reserved_tag_base);
  if (dst == rank_) {
    pool_->put(comm_id_, rank_, tag, std::vector<char>());
  } else {
    push_empty(dst, tag);
  }
}

void AsioComm::wait_send() {}

void AsioComm::recv(int& src, std::vector<char>& buf, int& tag) {
  pool_->take(comm_id_, src, tag, buf);
  CHECK_LT(tag, asio_comm::reserved_tag_base);
}

void AsioComm::recv_from(int src, std::vector<char>& buf, int& tag) {
  pool_->take_from(comm_id_, src, tag, buf);
  CHECK_LT(tag, asio_comm::reserved_tag_base);
}

void AsioComm::recv_tagged(int& src, std::vector<char>& buf, int tag) {
  CHECK_LT(tag, asio_comm::reserved_tag_base);
  pool_->take_tagged(comm_id_, src, tag, buf);
}

void AsioComm::recv_from_tagged(int src, std::vector<char>& buf, int tag) {
  CHECK_LT(tag, asio_comm::reserved_tag_base);
  pool_->take_from_tagged(comm_id_, src, tag, buf);
}

int64_t AsioComm::sum(int64_t input) {
  int64_t ret;
  sum(&input, &ret, 1);
  return ret;
}

void AsioComm::sum(int64_t* input, int64_t* output, size_t count) {
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

void AsioComm::gather(std::vector<char>&& input,
                      std::vector<std::vector<char>>& output) {
  output.clear();
  output.resize(size_);
  output[rank_] = input;
  static const int gather_tag = std::numeric_limits<int>::max() - 14;
  push_bcast(gather_tag, std::move(input));
  for (int i = 1; i < size_; ++i) {
    int src_worker_id;
    std::vector<char> recv_buf;
    recv_tagged_reserved(src_worker_id, recv_buf, gather_tag);
    output[src_worker_id] = std::move(recv_buf);
  }
}

void AsioComm::gather(const std::vector<char>& input,
                      std::vector<std::vector<char>>& output) {
  gather(std::vector<char>(input), output);
}

void AsioComm::all_to_all(std::vector<std::vector<char>>&& input,
                          std::vector<std::vector<char>>& output) {
  static const int all_to_all_tag = std::numeric_limits<int>::max() - 13;
  output[rank_] = std::move(input[rank_]);
  for (int i = 1; i < size_; ++i) {
    int dst_worker_id = (rank_ + i) % size_;
    send_reserved(dst_worker_id, std::move(input[dst_worker_id]),
                  all_to_all_tag);
  }
  for (int i = 1; i < size_; ++i) {
    int src_worker_id;
    std::vector<char> buf;
    recv_tagged_reserved(src_worker_id, buf, all_to_all_tag);
    output[src_worker_id] = std::move(buf);
  }

  input.clear();
}

void AsioComm::all_to_all(const std::vector<std::vector<char>>& input,
                          std::vector<std::vector<char>>& output) {
  all_to_all(std::vector<std::vector<char>>(input), output);
}

void AsioComm::bcast(std::vector<char>& buf, int root) {
  static const int bcast_tag = std::numeric_limits<int>::max() - 12;
  if (rank_ == root) {
    std::vector<char> copy_buf(buf);
    push_bcast(bcast_tag, std::move(copy_buf));
  } else {
    recv_from_tagged_reserved(root, buf, bcast_tag);
  }
}

void AsioComm::send_reserved(int dst, std::vector<char>&& buf, int tag) {
  CHECK_NE(dst, rank_);
  CHECK_GE(tag, asio_comm::reserved_tag_base);
  push_data(dst, tag, std::move(buf));
}

void AsioComm::send_reserved(int dst, const char* buf, size_t count, int tag) {
  std::vector<char> vec(count);
  memcpy(vec.data(), buf, count);
  send_reserved(dst, std::move(vec), tag);
}

void AsioComm::recv_tagged_reserved(int& src, std::vector<char>& buf, int tag) {
  CHECK_GE(tag, asio_comm::reserved_tag_base);
  pool_->take_tagged_reserved(comm_id_, src, tag, buf);
}

void AsioComm::recv_from_tagged_reserved(int src, std::vector<char>& buf,
                                         int tag) {
  CHECK_GE(tag, asio_comm::reserved_tag_base);
  pool_->take_from_tagged_reserved(comm_id_, src, tag, buf);
}

void AsioComm::push_empty(int dst, int tag) {
  queue_[dst].Put(std::make_tuple(asio_comm::MsgType::kData, comm_id_, tag,
                                  std::vector<char>()));
}

void AsioComm::push_data(int dst, int tag, std::vector<char>&& data) {
  queue_[dst].Put(std::make_tuple(asio_comm::MsgType::kData, comm_id_, tag,
                                  std::move(data)));
}

void AsioComm::push_bcast(int tag, std::vector<char>&& data) {
  for (int i = 2; i < size_; ++i) {
    int target = (rank_ + i) % size_;
    queue_[target].Put(
        std::make_tuple(asio_comm::MsgType::kBcast, comm_id_, tag, data));
  }
  if (size_ > 1) {
    queue_[(1 + rank_) % size_].Put(std::make_tuple(
        asio_comm::MsgType::kBcast, comm_id_, tag, std::move(data)));
  }
}

void AsioComm::push_barrier(int dst) {
  queue_[dst].Put(std::make_tuple(asio_comm::MsgType::kBarrier, comm_id_, -1,
                                  std::vector<char>()));
}

static void trim(std::string& str) {
  str.erase(0, str.find_first_not_of(" \t\r\n"));
  str.erase(str.find_last_not_of(" \t\r\n") + 1);
}

AsioCommAllocator::~AsioCommAllocator() {
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

void AsioCommAllocator::init(const std::string& hostfile, int self_id,
                             int worker_num) {
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

void AsioCommAllocator::init(const std::vector<std::string>& addresses,
                             const std::vector<std::string>& ports,
                             int self_id) {
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
      std::tuple<asio_comm::MsgType, int, int, std::vector<char>>>[size_];
  for (int i = 0; i < size_; ++i) {
    send_queue_[i].SetProducerNum(1);
  }
  pool_.init(size_);

  if (size_ > 1) {
    sockets_ =
        asio_comm::create_connections(ioc_, addresses, ports, rank_, size_);
  } else {
    sockets_.clear();
    sockets_.resize(size_, nullptr);
  }

  for (int i = 1; i < size_; ++i) {
    int dst_worker_id = (i + rank_) % size_;
    send_threads_.emplace_back(
        [&, this](int target) {
          auto& que = send_queue_[target];
          auto& socket = *sockets_[target];
          auto writer = std::make_shared<asio_comm::AsyncWriter>(socket, que);
          writer->Write();
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
          auto reader =
              std::make_shared<asio_comm::AsyncReader>(source, socket, pool_);
          reader->Read();
          ioc_.run();
          VLOG(2) << "recv thread returned..";
        },
        src_worker_id);
  }
}

AsioComm AsioCommAllocator::allocate() {
  int comm_id = pool_.allocate_comm();
  return AsioComm(&pool_, send_queue_, comm_id, rank_, size_, local_rank_,
                  local_size_);
}

int AsioCommAllocator::rank() const { return rank_; }
int AsioCommAllocator::size() const { return size_; }
int AsioCommAllocator::local_rank() const { return local_rank_; }
int AsioCommAllocator::local_size() const { return local_size_; }

}  // namespace grape

#endif
