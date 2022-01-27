#include <mpi.h>

#include <iostream>
#include <limits>
#include <vector>

int main(int argc, char** argv) {
  {
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided != MPI_THREAD_MULTIPLE) {
      std::cout << "init mpi failed, got " << provided << ", "
                << MPI_THREAD_MULTIPLE << " is required" << std::endl;
      MPI_Finalize();
      return -1;
    } else {
      std::cout << "init mpi success" << std::endl;
    }
  }

  size_t int_max = static_cast<size_t>(std::numeric_limits<int>::max());
  if (argc >= 2) {
    int_max = atol(argv[1]);
  }
  std::vector<int64_t> vec(int_max);
  int worker_id, worker_num;
  MPI_Comm_rank(MPI_COMM_WORLD, &worker_id);
  MPI_Comm_size(MPI_COMM_WORLD, &worker_num);
  if (worker_id == 0) {
    for (size_t i = 0; i < int_max; ++i) {
      vec[i] = static_cast<int64_t>(i);
    }
    size_t len = 1024;
    while (len < int_max) {
      int ret = MPI_Send(vec.data(), static_cast<int>(len), MPI_INT64_T, 1, 0, MPI_COMM_WORLD);
      if (ret == MPI_SUCCESS) {
        std::cout << "Send " << len << " int64_t success" << std::endl;
      }
      len = len * 2;
    }
  } else {
    size_t len = 1024;
    while (len < int_max) {
      for (size_t i = 0; i < len; ++i) {
        vec[i] = 0;
      }
      int ret = MPI_Recv(vec.data(), static_cast<int>(len), MPI_INT64_T, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      if (ret == MPI_SUCCESS) {
        std::cout << "Recv " << len << " int64_t success" << std::endl;
      }
      for (size_t i = 0; i < len; ++i) {
        if (vec[i] != static_cast<int64_t>(i)) {
          std::cout << " Recv wrong content, " << vec[i] << " v.s. " << i << std::endl;
          break;
        }
      }
      len = len * 2;
    }
  }

  MPI_Finalize();
  return 0;
}
