/** Copyright 2020 Alibaba Group Holding Limited.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <sys/stat.h>

#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>
#include <glog/logging.h>

#include <grape/communication/comm.h>
#include <grape/fragment/ev_fragment_mutator.h>
#include <grape/fragment/immutable_edgecut_fragment.h>
#include <grape/fragment/loader.h>
#include <grape/fragment/mutable_edgecut_fragment.h>
#include <grape/grape.h>
#include <grape/util.h>

#include "pagerank/pagerank_local_parallel.h"
#include "sssp/sssp.h"
#include "timer.h"

#ifndef __AFFINITY__
#define __AFFINITY__ false
#endif

DEFINE_string(vfile, "", "vertex file");
DEFINE_string(efile, "", "edge file");
DEFINE_string(out_prefix, "", "output directory of results");
DEFINE_string(delta_efile_prefix, "", "delta edge file directory");
DEFINE_int32(delta_efile_part_num, 0, "delta edge file part num");
DEFINE_int64(sssp_source, 0, "source vertex of sssp");
DEFINE_bool(directed, false, "input graph is directed or not.");
DEFINE_double(pr_d, 0.85, "damping_factor of pagerank");
DEFINE_int32(pr_mr, 10, "max rounds of pagerank");
DEFINE_string(application, "", "application name");
#ifndef USE_MPI
DEFINE_string(hostfile, "", "path to hostfile");
DEFINE_int32(worker_id, 0, "worker id");
#endif

void Init() {
  if (FLAGS_out_prefix.empty()) {
    LOG(FATAL) << "Please assign an output prefix.";
  }
  if (FLAGS_efile.empty()) {
    LOG(FATAL) << "Please assign input edge files.";
  }
  if (access(FLAGS_out_prefix.c_str(), 0) != 0) {
    mkdir(FLAGS_out_prefix.c_str(), 0777);
  }

#ifdef USE_MPI
  grape::CommAllocatorType::get().init();
#else
  grape::CommAllocatorType::get().init(FLAGS_hostfile, FLAGS_worker_id);
#endif
  if (grape::CommAllocatorType::get().rank() == grape::kCoordinatorRank) {
    VLOG(1) << "Workers of libgrape-lite initialized.";
  }
}

void Finalize() { VLOG(1) << "Workers finalized."; }

template <typename FRAG_T>
std::shared_ptr<FRAG_T> BuildGraph(const std::string& efile,
                                   const std::string& vfile) {
  timer_next("load graph");
  grape::LoadGraphSpec graph_spec = grape::DefaultLoadGraphSpec();
  graph_spec.set_directed(FLAGS_directed);
  graph_spec.set_rebalance(false, 0);
  graph_spec.set_deserialize(false, "");
  graph_spec.set_serialize(false, "");
  std::shared_ptr<FRAG_T> fragment;
  grape::CommType comm = grape::CommAllocatorType::get().allocate();
  fragment = grape::LoadGraph<FRAG_T>(comm, efile, vfile, graph_spec);
  return fragment;
}

template <typename FRAG_T>
std::shared_ptr<FRAG_T> MutateGraph(const std::string& efile_prefix,
                                    int efile_num,
                                    std::shared_ptr<FRAG_T> fragment) {
  timer_next("mutate graph");
  grape::CommType comm = grape::CommAllocatorType::get().allocate();
  grape::EVFragmentMutator<FRAG_T, grape::LocalIOAdaptor> mutator;
  for (int i = 0; i < efile_num; ++i) {
    std::string path = efile_prefix + ".part_" + std::to_string(i);
    fragment = mutator.MutateFragment(comm, path, "", fragment, FLAGS_directed);
  }
  return fragment;
}

template <typename FRAG_T, typename APP_T, typename... Args>
void RunQuery(std::shared_ptr<FRAG_T> fragment, const std::string& out_prefix,
              const grape::ParallelEngineSpec& spec, Args... args) {
  timer_next("load application");
  auto app = std::make_shared<APP_T>();
  auto worker = APP_T::CreateWorker(app, fragment);
  worker->Init(grape::CommAllocatorType::get(), spec);
  timer_next("run algorithm");
  worker->Query(std::forward<Args>(args)...);
  timer_next("print output");
  std::ofstream ostream;
  std::string output_path =
      grape::GetResultFilename(out_prefix, fragment->fid());
  ostream.open(output_path);
  worker->Output(ostream);
  ostream.close();
  worker->Finalize();
  timer_end();
  VLOG(1) << "Worker-" << grape::CommAllocatorType::get().rank()
          << " finished: " << output_path;
}

template <typename FRAG_T, typename APP_T, typename... Args>
void BuildGraphAndQuery(const std::string& efile, const std::string& vfile,
                        const std::string& efile_prefix, int efile_num,
                        const std::string& out_prefix,
                        const grape::ParallelEngineSpec& spec, Args... args) {
  std::shared_ptr<FRAG_T> fragment = BuildGraph<FRAG_T>(efile, vfile);
  fragment = MutateGraph(efile_prefix, efile_num, fragment);
  RunQuery<FRAG_T, APP_T, Args...>(fragment, out_prefix, spec,
                                   std::forward<Args>(args)...);
}

template <typename FRAG_T, typename APP_T, typename... Args>
void BuildImmutableGraphAndQuery(const std::string& efile,
                                 const std::string& vfile,
                                 const std::string& out_prefix,
                                 const grape::ParallelEngineSpec& spec,
                                 Args... args) {
  std::shared_ptr<FRAG_T> fragment = BuildGraph<FRAG_T>(efile, vfile);
  RunQuery<FRAG_T, APP_T, Args...>(fragment, out_prefix, spec,
                                   std::forward<Args>(args)...);
}

template <typename OID_T, typename VID_T, typename VDATA_T, typename EDATA_T>
void RunBenchmark() {
  int worker_id = grape::CommAllocatorType::get().rank();
  bool is_coordinator = worker_id == grape::kCoordinatorRank;
  timer_start(is_coordinator);

  std::string efile = FLAGS_efile;
  std::string vfile = FLAGS_vfile;
  std::string delta_efile_prefix = FLAGS_delta_efile_prefix;
  int delta_efile_part_num = FLAGS_delta_efile_part_num;
  std::string out_prefix = FLAGS_out_prefix;

  int local_id = grape::CommAllocatorType::get().local_rank();
  int local_size = grape::CommAllocatorType::get().local_size();
  auto spec = grape::MultiProcessSpec(local_id, local_size, __AFFINITY__);

  if (delta_efile_part_num != 0) {
    using GraphType =
        grape::MutableEdgecutFragment<OID_T, VID_T, VDATA_T, double>;
    using AppType = grape::SSSP<GraphType>;
    BuildGraphAndQuery<GraphType, AppType, OID_T>(
        efile, vfile, delta_efile_prefix, delta_efile_part_num, out_prefix,
        spec, FLAGS_sssp_source);
  } else {
    using GraphType =
        grape::ImmutableEdgecutFragment<OID_T, VID_T, VDATA_T, double>;
    using AppType = grape::SSSP<GraphType>;
    BuildImmutableGraphAndQuery<GraphType, AppType, OID_T>(
        efile, vfile, out_prefix, spec, FLAGS_sssp_source);
  }
}

template <typename OID_T, typename VID_T, typename VDATA_T, typename EDATA_T>
void RunPageRankBenchmark() {
  int worker_id = grape::CommAllocatorType::get().rank();
  bool is_coordinator = worker_id == grape::kCoordinatorRank;
  timer_start(is_coordinator);

  std::string efile = FLAGS_efile;
  std::string vfile = FLAGS_vfile;
  std::string delta_efile_prefix = FLAGS_delta_efile_prefix;
  int delta_efile_part_num = FLAGS_delta_efile_part_num;
  std::string out_prefix = FLAGS_out_prefix;

  int local_id = grape::CommAllocatorType::get().local_rank();
  int local_size = grape::CommAllocatorType::get().local_size();
  auto spec = grape::MultiProcessSpec(local_id, local_size, __AFFINITY__);

  if (delta_efile_part_num != 0) {
    using GraphType =
        grape::MutableEdgecutFragment<OID_T, VID_T, VDATA_T, double,
                                      grape::LoadStrategy::kBothOutIn>;
    using AppType = grape::PageRankLocalParallel<GraphType>;
    BuildGraphAndQuery<GraphType, AppType, double, int>(
        efile, vfile, delta_efile_prefix, delta_efile_part_num, out_prefix,
        spec, FLAGS_pr_d, FLAGS_pr_mr);
  } else {
    using GraphType =
        grape::ImmutableEdgecutFragment<OID_T, VID_T, VDATA_T, double,
                                        grape::LoadStrategy::kBothOutIn>;
    using AppType = grape::PageRankLocalParallel<GraphType>;
    BuildImmutableGraphAndQuery<GraphType, AppType, double, int>(
        efile, vfile, out_prefix, spec, FLAGS_pr_d, FLAGS_pr_mr);
  }
}

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

  Init();

  if (FLAGS_application == "pagerank") {
    RunPageRankBenchmark<int64_t, uint32_t, grape::EmptyType,
                         grape::EmptyType>();
  } else if (FLAGS_application == "sssp") {
    RunBenchmark<int64_t, uint32_t, grape::EmptyType, double>();
  }

  Finalize();

  google::ShutdownGoogleLogging();
}
