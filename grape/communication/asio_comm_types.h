#ifndef GRAPE_COMMUNICATIONN_ASIO_COMM_TYPES_H_
#define GRAPE_COMMUNICATIONN_ASIO_COMM_TYPES_H_

#ifdef USE_ASIO

#include <limits>

namespace grape {

namespace asio_comm {

static constexpr int reserved_tag_num = 16;
static constexpr int reserved_tag_base =
    std::numeric_limits<int>::max() - reserved_tag_num;
static constexpr int sum_tag = reserved_tag_base;
static constexpr int sum_ack_tag = reserved_tag_base + 1;
static constexpr int gather_tag = reserved_tag_base + 2;
static constexpr int all_to_all_tag = reserved_tag_base + 3;
static constexpr int bcast_tag = reserved_tag_base + 4;

enum class MsgType {
  kData,
  kBarrier,
  kBcast,
  kExit,
};

struct Header {
  size_t length;
  int tag;
  int comm_id;
  MsgType type;
};

}  // namespace asio_comm

}  // namespace grape

#endif

#endif  // GRAPE_COMMUNICATIONN_ASIO_COMM_TYPES_H_
