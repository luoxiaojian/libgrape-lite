#ifndef GRAPE_COMMUNICATION_MPI_COMM_H_
#define GRAPE_COMMUNICATION_MPI_COMM_H_

#include <mpi.h>

#include <vector>

namespace grape {

#ifdef OPEN_MPI
#define NULL_COMM NULL
#else
#define NULL_COMM -1
#endif
#define ValidComm(comm) ((comm) != NULL_COMM)

class MPICommAllocator;

class MPIComm {
 public:
  MPIComm();
  MPIComm(MPIComm&& rhs);

  MPIComm& operator=(MPIComm&& rhs);
  MPIComm& operator=(const MPIComm& rhs) = delete;

 private:
  MPIComm(MPI_Comm comm, int rank, int size, int local_rank, int local_size);

 public:
  static constexpr size_t kChunkSize = 1 << 20;
  ~MPIComm();

  int rank() const;
  int size() const;

  int local_rank() const;

  int local_size() const;

  void barrier();

  void send(int dst, const char* buf, size_t count, int tag);

  void send(int dst, const std::vector<char>& vec, int tag);

  void send(int dst, std::vector<char>&& vec, int tag);

  void send_empty(int dst, int tag);

  void wait_send();

  void recv(int& src, std::vector<char>& vec, int& tag);

  void recv_from(int src, std::vector<char>& vec, int& tag);

  void recv_tagged(int& src, std::vector<char>& vec, int tag);

  void recv_from_tagged(int src, std::vector<char>& vec, int tag);

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

  void bcast(std::vector<char>& val, int root);

  MPI_Comm comm() const;

 private:
  MPI_Comm comm_;
  int rank_;
  int size_;
  std::vector<MPI_Request> reqs_;
  std::vector<std::vector<char>> bufs_;

  int local_rank_;
  int local_size_;

  friend class MPICommAllocator;
};

class MPICommAllocator {
 public:
  MPICommAllocator();
  ~MPICommAllocator();

  void init();

  static MPICommAllocator& get() {
    static MPICommAllocator allocator;
    return allocator;
  }

  MPIComm allocate();

  int rank() const;
  int size() const;
  int local_rank() const;
  int local_size() const;

 private:
  __attribute__((no_sanitize_address)) void initLocalInfo();

  MPI_Comm comm_;
  int rank_;
  int size_;

  int local_rank_;
  int local_size_;
};

}  // namespace grape

#endif  // GRAPE_COMMUNICATION_MPI_COMM_H_