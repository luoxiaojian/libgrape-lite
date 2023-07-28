#ifdef USE_ASIO

#include "grape/communication/asio_comm_protocol.h"
#include "glog/logging.h"

namespace grape {

namespace asio_comm {

void AsyncReader::Read() {
  offset_ = 0;
  read_header();
}

void AsyncReader::read_header() {
  auto self = shared_from_this();
  socket_.async_read_some(
      boost::asio::buffer(reinterpret_cast<char*>(&header_) + offset_,
                          sizeof(header_) - offset_),
      [self, this](boost::system::error_code ec, size_t length) {
        if (ec) {
          LOG(ERROR) << "recv crash, " << ec.message();
        }
        offset_ += length;
        if (offset_ < sizeof(Header)) {
          read_header();
        } else {
          CHECK_EQ(offset_, sizeof(Header));
          process_header();
        }
      });
}

void AsyncReader::process_header() {
  if (header_.type == MsgType::kBarrier) {
    pool_.barrier(header_.comm_id, src_);
    Read();
  } else if (header_.type == MsgType::kExit) {
    return;
  } else {
    if (header_.length > 0) {
      buf_.resize(header_.length);
      offset_ = 0;
      read_content();
    } else {
      pool_.put(header_.comm_id, src_, header_.tag, std::vector<char>());
      Read();
    }
  }
}

void AsyncReader::read_content() {
  auto self = shared_from_this();
  socket_.async_read_some(
      boost::asio::buffer(buf_) + offset_,
      [this, self](boost::system::error_code ec, size_t length) {
        if (ec) {
          LOG(ERROR) << ec.message() << std::endl;
          return;
        }
        offset_ += length;
        if (offset_ < header_.length) {
          read_content();
        } else {
          pool_.put(header_.comm_id, src_, header_.tag, std::move(buf_));
          Read();
        }
      });
}

void AsyncWriter::Write() {
  std::tuple<MsgType, int, int, std::vector<char>> item;
  if (que_.Get(item)) {
    MsgType type = std::get<0>(item);
    int comm_id = std::get<1>(item);
    int tag = std::get<2>(item);
    buf_ = std::move(std::get<3>(item));

    header_.type = type;
    header_.comm_id = comm_id;
    if (type == MsgType::kBcast || type == MsgType::kData) {
      header_.tag = tag;
      header_.length = buf_.size();
    }
  } else {
    header_.type = MsgType::kExit;
  }
  offset_ = 0;
  write_header();
}

void AsyncWriter::write_header() {
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
        if (offset_ < sizeof(Header)) {
          write_header();
        } else {
          CHECK_EQ(offset_, sizeof(Header));
          if ((header_.type == MsgType::kData ||
               header_.type == MsgType::kBcast) &&
              header_.length != 0) {
            offset_ = 0;
            write_content();
          } else if (header_.type != MsgType::kExit) {
            Write();
          }
        }
      });
}

void AsyncWriter::write_content() {
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
          Write();
        }
      });
}

}  // namespace asio_comm

}  // namespace grape

#endif