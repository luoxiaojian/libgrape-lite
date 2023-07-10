#ifndef GRAPE_COMMUNICATION_ASIO_COMM_PROTOCOL_H_
#define GRAPE_COMMUNICATION_ASIO_COMM_PROTOCOL_H_

#ifdef USE_ASIO

#include <boost/asio.hpp>
#include <memory>

#include "grape/communication/asio_comm_impl.h"
#include "grape/communication/asio_comm_types.h"
#include "grape/utils/concurrent_queue.h"

namespace grape {

namespace asio_comm {

class AsyncReader : public std::enable_shared_from_this<AsyncReader> {
 public:
  AsyncReader(int src, boost::asio::ip::tcp::socket& socket, MessagePool& pool)
      : src_(src), socket_(socket), pool_(pool) {}

  void Read();

 private:
  void read_header();

  void process_header();

  void read_content();

  int src_;
  boost::asio::ip::tcp::socket& socket_;
  MessagePool& pool_;

  Header header_;
  std::vector<char> buf_;
  size_t offset_;
};

class AsyncWriter : public std::enable_shared_from_this<AsyncWriter> {
 public:
  AsyncWriter(
      boost::asio::ip::tcp::socket& socket,
      BlockingQueue<std::tuple<MsgType, int, int, std::vector<char>>>& que)
      : socket_(socket), que_(que) {}

  void Write();

 private:
  void write_header();

  void write_content();

  boost::asio::ip::tcp::socket& socket_;
  BlockingQueue<std::tuple<MsgType, int, int, std::vector<char>>>& que_;

  Header header_;
  std::vector<char> buf_;
  size_t offset_;
};

}  // namespace asio_comm

}  // namespace grape

#endif

#endif  // GRAPE_COMMUNICATION_ASIO_COMM_PROTOCOL_H_
