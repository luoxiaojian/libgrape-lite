#include "benchmark_app.h"

#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>
#include <glog/logging.h>

int main(int argc, char* argv[]) {
  FLAGS_stderrthreshold = 0;

  grape::gflags::SetUsageMessage(
      "Usage: mpiexec [mpi_opts] ./run_mutable_app [grape_opts]");
  if (argc == 1) {
    grape::gflags::ShowUsageWithFlagsRestrict(argv[0], "analytical_apps");
    exit(1);
  }
  grape::gflags::ParseCommandLineFlags(&argc, &argv, true);
  grape::gflags::ShutDownCommandLineFlags();

  google::InitGoogleLogging("analytical_apps");
  google::InstallFailureSignalHandler();

  grape::Init();

  grape::RunPageRankBenchmark<int64_t, uint32_t, grape::EmptyType, grape::EmptyType>();
  // grape::RunBenchmark<int64_t, uint32_t, grape::EmptyType, double>();
  // grape::RunTraverse<int64_t, uint32_t, grape::EmptyType, double>();

  grape::Finalize();

  google::ShutdownGoogleLogging();
}
