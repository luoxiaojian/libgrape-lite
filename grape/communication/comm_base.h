#ifndef GRAPE_COMMUNICATION_COMM_BASE_H_
#define GRAPE_COMMUNICATION_COMM_BASE_H_

#include <vector>

namespace grape {

class CommBase {
 public:
  CommBase() = default;
  virtual ~CommBase() = default;

  virtual int rank() const = 0;
  virtual int size() const = 0;
  virtual int local_rank() const = 0;
  virtual int local_size() const = 0;

  virtual void barrier() = 0;

  virtual void send(int dst, std::vector<char>&& buf, int tag) = 0;
  virtual void send(int dst, const std::vector<char>& buf, int tag) = 0;
  virtual void send(int dst, const char* buf, size_t size, int tag) = 0;
  virtual void send_empty(int dst, int tag) = 0;

  virtual void wait_send() = 0;

  virtual void recv(int& src, std::vector<char>& buf, int& tag) = 0;
  virtual void recv_from(int src, std::vector<char>& buf, int& tag) = 0;
  virtual void recv_tagged(int& src, std::vector<char>& buf, int tag) = 0;
  virtual void recv_from_tagged(int src, std::vector<char>& buf, int tag) = 0;

  virtual int64_t sum(int64_t input) = 0;
  virtual void sum(int64_t* input, int64_t* output, size_t count) = 0;
};

}  // namespace grape

#endif  // GRAPE_COMMUNICATION_COMM_BASE_H_
