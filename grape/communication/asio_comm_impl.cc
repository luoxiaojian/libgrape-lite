#ifdef USE_ASIO

#include "grape/communication/asio_comm_impl.h"
#include "grape/communication/asio_comm_protocol.h"

namespace grape {

namespace asio_comm {

void MessagePool::init(int worker_num) {
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

int MessagePool::allocate_comm() {
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

void MessagePool::put(int comm_id, int src, int tag, std::vector<char>&& buf) {
  size_t idx = comm_id * worker_num_ + src;
  if (tag < reserved_tag_base) {
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
    reserved_pool_[idx][tag - reserved_tag_base].emplace_back(std::move(buf));
    reserved_cond_.notify_all();
  }
}

void MessagePool::take(int comm_id, int& src, int& tag,
                       std::vector<char>& buf) {
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

void MessagePool::take_from(int comm_id, int src, int& tag,
                            std::vector<char>& buf) {
  size_t idx = comm_id * worker_num_ + src;
  std::unique_lock<std::mutex> lock(mutex_);
  cond_.wait(lock,
             [this, idx] { return pool_.size() > idx && !pool_[idx].empty(); });
  auto it = pool_[idx].begin();
  tag = it->first;
  buf = std::move(it->second.front());
  it->second.pop_front();
  if (it->second.empty()) {
    pool_[idx].erase(it);
  }
}

void MessagePool::take_tagged(int comm_id, int& src, int tag,
                              std::vector<char>& buf) {
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

void MessagePool::take_tagged_reserved(int comm_id, int& src, int tag,
                                       std::vector<char>& buf) {
  size_t min_size = (comm_id + 1) * worker_num_;
  std::unique_lock<std::mutex> lock(reserved_mutex_);
  reserved_cond_.wait(lock, [this, tag, comm_id, &src, min_size] {
    if (reserved_pool_.size() < min_size) {
      return false;
    }
    for (int i = 0; i != worker_num_; ++i) {
      int idx = comm_id * worker_num_ + i;
      auto& deq = reserved_pool_[idx][tag - reserved_tag_base];
      if (!deq.empty()) {
        src = i;
        return true;
      }
    }
    return false;
  });
  int idx = comm_id * worker_num_ + src;
  auto& deq = reserved_pool_[idx][tag - reserved_tag_base];
  buf = std::move(deq.front());
  deq.pop_front();
}

void MessagePool::take_from_tagged(int comm_id, int src, int tag,
                                   std::vector<char>& buf) {
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

void MessagePool::take_from_tagged_reserved(int comm_id, int src, int tag,
                                            std::vector<char>& buf) {
  size_t idx = comm_id * worker_num_ + src;
  std::unique_lock<std::mutex> lock(reserved_mutex_);
  reserved_cond_.wait(lock, [this, idx, tag] {
    return reserved_pool_.size() > idx &&
           !reserved_pool_[idx][tag - reserved_tag_base].empty();
  });
  auto& deq = reserved_pool_[idx][tag - reserved_tag_base];
  buf = std::move(deq.front());
  deq.pop_front();
}

void MessagePool::barrier(int comm_id, int src) {
  size_t min_size = (comm_id + 1) * worker_num_;
  std::lock_guard<std::mutex> lock(barrier_mutex_);
  if (barrier_count_.size() < min_size) {
    barrier_count_.resize(min_size, 0);
  }
  ++barrier_count_[comm_id * worker_num_ + src];
  barrier_cond_.notify_all();
}

void MessagePool::wait_barrier_slave(int comm_id, int expected) {
  size_t idx = comm_id * worker_num_;
  std::unique_lock<std::mutex> lock(barrier_mutex_);
  barrier_cond_.wait(lock, [this, expected, idx] {
    return barrier_count_.size() > idx && barrier_count_[idx] >= expected;
  });
}

void MessagePool::wait_barrier_master(int comm_id, int expected) {
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

static std::shared_ptr<boost::asio::ip::tcp::socket> loop_connect(
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
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  return nullptr;
}

std::vector<std::shared_ptr<boost::asio::ip::tcp::socket>> create_connections(
    boost::asio::io_context& ioc, const std::vector<std::string>& hosts,
    const std::vector<std::string>& ports, int rank, int size) {
  std::vector<std::shared_ptr<boost::asio::ip::tcp::socket>> sockets(size,
                                                                     nullptr);
  std::thread connect_thread([&]() {
    boost::asio::ip::tcp::resolver resolver(ioc);
    for (int dst_worker_id = 0; dst_worker_id != rank; ++dst_worker_id) {
      auto endpoints =
          resolver.resolve(boost::asio::ip::tcp::v4(), hosts[dst_worker_id],
                           ports[dst_worker_id]);
      auto socket = loop_connect(ioc, endpoints);
      socket->set_option(boost::asio::ip::tcp::no_delay(true));
      socket->write_some(boost::asio::buffer(&rank, sizeof(int)));
      sockets[dst_worker_id] = socket;
    }
  });
  std::thread accept_thread([&]() {
    boost::asio::ip::tcp::acceptor acceptor(
        ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(),
                                            std::stoi(ports[rank])));
    acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    for (int src_worker_id = rank + 1; src_worker_id != size; ++src_worker_id) {
      auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioc);
      acceptor.accept(*socket);
      socket->set_option(boost::asio::ip::tcp::no_delay(true));
      int src;
      socket->read_some(boost::asio::buffer(&src, sizeof(int)));
      sockets[src] = socket;
    }
  });

  connect_thread.join();
  accept_thread.join();

  return sockets;
}

}  // namespace asio_comm

}  // namespace grape

#endif
