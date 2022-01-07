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

#ifndef EXAMPLES_ANALYTICAL_APPS_RUN_MUTABLE_APP_H_
#define EXAMPLES_ANALYTICAL_APPS_RUN_MUTABLE_APP_H_

#include <sys/stat.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <grape/fragment/ev_fragment_mutator.h>
#include <grape/fragment/loader.h>
#include <grape/fragment/mutable_edgecut_fragment.h>
#include <grape/grape.h>
#include <grape/util.h>

#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>
#include <glog/logging.h>

#include "bfs/bfs.h"
#include "bfs/bfs_auto.h"
#include "cdlp/cdlp.h"
#include "cdlp/cdlp_auto.h"
#include "flags.h"
#include "lcc/lcc.h"
#include "lcc/lcc_auto.h"
#include "pagerank/pagerank.h"
#include "pagerank/pagerank_auto.h"
#include "pagerank/pagerank_local.h"
#include "pagerank/pagerank_local_parallel.h"
#include "pagerank/pagerank_parallel.h"
#include "sssp/sssp.h"
#include "sssp/sssp_auto.h"
#include "traverse/traverse.h"
#include "wcc/wcc.h"
#include "wcc/wcc_auto.h"

#ifndef __AFFINITY__
#define __AFFINITY__ false
#endif

namespace grape {

void Init() {
  if (FLAGS_out_prefix.empty()) {
    LOG(FATAL) << "Please assign an output prefix.";
  }
  if (FLAGS_deserialize && FLAGS_serialization_prefix.empty()) {
    LOG(FATAL) << "Please assign a serialization prefix.";
  } else if (FLAGS_efile.empty()) {
    LOG(FATAL) << "Please assign input edge files.";
  } else if (FLAGS_vfile.empty() && FLAGS_segmented_partition) {
    LOG(FATAL) << "EFragmentLoader dosen't support Segmented Partitioner. "
                  "Please assign vertex files or use Hash Partitioner";
  }

  if (access(FLAGS_out_prefix.c_str(), 0) != 0) {
    mkdir(FLAGS_out_prefix.c_str(), 0777);
  }

  InitMPIComm();
  CommSpec comm_spec;
  comm_spec.Init(MPI_COMM_WORLD);
  if (comm_spec.worker_id() == kCoordinatorRank) {
    VLOG(1) << "Workers of libgrape-lite initialized.";
  }
}

void Finalize() {
  FinalizeMPIComm();
  VLOG(1) << "Workers finalized.";
}

template <typename FRAG_T>
std::shared_ptr<FRAG_T> BuildGraph(const CommSpec& comm_spec,
                                   const std::string& efile,
                                   const std::string& vfile) {
  LoadGraphSpec graph_spec = DefaultLoadGraphSpec();
  graph_spec.set_directed(FLAGS_directed);
  graph_spec.set_rebalance(false, 0);
  graph_spec.set_deserialize(false, "");
  graph_spec.set_serialize(false, "");
  std::shared_ptr<FRAG_T> fragment;
  fragment = LoadGraph<FRAG_T, HashPartitioner<typename FRAG_T::oid_t>>(
      efile, vfile, comm_spec, graph_spec);
  return fragment;
}

template <typename FRAG_T>
std::shared_ptr<FRAG_T> MutateGraph(const CommSpec& comm_spec,
                                    const std::string& efile,
                                    const std::string& vfile,
                                    std::shared_ptr<FRAG_T> fragment) {
  EVFragmentMutator<FRAG_T, LocalIOAdaptor> mutator(comm_spec);
  return mutator.MutateFragment(efile, vfile, fragment, FLAGS_directed);
}

template <typename FRAG_T, typename APP_T, typename... Args>
void RunQuery(std::shared_ptr<FRAG_T> fragment, const CommSpec& comm_spec,
              const std::string& out_prefix, const ParallelEngineSpec& spec,
              Args... args) {
  auto app = std::make_shared<APP_T>();
  auto worker = APP_T::CreateWorker(app, fragment);
  worker->Init(comm_spec, spec);
  worker->Query(std::forward<Args>(args)...);
  std::ofstream ostream;
  std::string output_path = GetResultFilename(out_prefix, fragment->fid());
  ostream.open(output_path);
  worker->Output(ostream);
  ostream.close();
  worker->Finalize();
}

template <typename FRAG_T, typename APP_T, typename... Args>
void BuildGraphAndQuery(const CommSpec& comm_spec, const std::string& efile,
                        const std::string& vfile,
                        const std::string& delta_efile,
                        const std::string& delta_vfile,
                        const std::string& out_prefix,
                        const ParallelEngineSpec& spec, Args... args) {
  std::shared_ptr<FRAG_T> fragment =
      BuildGraph<FRAG_T>(comm_spec, efile, vfile);
  fragment = MutateGraph(comm_spec, delta_efile, delta_vfile, fragment);
  RunQuery<FRAG_T, APP_T, Args...>(fragment, comm_spec, out_prefix, spec,
                                   std::forward<Args>(args)...);
}

template <typename OID_T, typename VID_T, typename VDATA_T, typename EDATA_T>
void RunMutable() {
  CommSpec comm_spec;
  comm_spec.Init(MPI_COMM_WORLD);

  std::string efile = FLAGS_efile;
  std::string vfile = FLAGS_vfile;
  std::string delta_efile = FLAGS_delta_efile;
  std::string delta_vfile = FLAGS_delta_vfile;
  std::string out_prefix = FLAGS_out_prefix;
  auto spec = MultiProcessSpec(comm_spec, __AFFINITY__);
  if (FLAGS_app_concurrency != -1) {
    spec.thread_num = FLAGS_app_concurrency;
    if (__AFFINITY__) {
      if (spec.cpu_list.size() >= spec.thread_num) {
        spec.cpu_list.resize(spec.thread_num);
      } else {
        uint32_t num_to_append = spec.thread_num - spec.cpu_list.size();
        for (uint32_t i = 0; i < num_to_append; ++i) {
          spec.cpu_list.push_back(spec.cpu_list[i]);
        }
      }
    }
  }
  std::string name = FLAGS_application;

  if (name.find("sssp") != std::string::npos) {
    using GraphType = MutableEdgecutFragment<OID_T, VID_T, VDATA_T, double>;
    if (name == "sssp_auto") {
      using AppType = SSSPAuto<GraphType>;
      BuildGraphAndQuery<GraphType, AppType, OID_T>(
          comm_spec, efile, vfile, delta_efile, delta_vfile, out_prefix, spec,
          FLAGS_sssp_source);
    } else if (name == "sssp") {
      using AppType = SSSP<GraphType>;
      BuildGraphAndQuery<GraphType, AppType, OID_T>(
          comm_spec, efile, vfile, delta_efile, delta_vfile, out_prefix, spec,
          FLAGS_sssp_source);
    } else {
      LOG(FATAL) << "No avaiable application named [" << name << "].";
    }
  } else {
    if (name == "traverse") {
      using GraphType = MutableEdgecutFragment<OID_T, VID_T, VDATA_T, double,
                                               LoadStrategy::kOnlyOut>;
      using AppType = Traverse<GraphType>;
      BuildGraphAndQuery<GraphType, AppType>(
          comm_spec, efile, vfile, delta_efile, delta_vfile, out_prefix, spec);
    } else if (name == "bfs_auto") {
      using GraphType = MutableEdgecutFragment<OID_T, VID_T, VDATA_T, EDATA_T,
                                               LoadStrategy::kOnlyOut>;
      using AppType = BFSAuto<GraphType>;
      BuildGraphAndQuery<GraphType, AppType, OID_T>(
          comm_spec, efile, vfile, delta_efile, delta_vfile, out_prefix, spec,
          FLAGS_bfs_source);
    } else if (name == "bfs") {
      using GraphType = MutableEdgecutFragment<OID_T, VID_T, VDATA_T, EDATA_T,
                                               LoadStrategy::kOnlyOut>;
      using AppType = BFS<GraphType>;
      BuildGraphAndQuery<GraphType, AppType, OID_T>(
          comm_spec, efile, vfile, delta_efile, delta_vfile, out_prefix, spec,
          FLAGS_bfs_source);
    } else if (name == "pagerank_local_parallel") {
      using GraphType = MutableEdgecutFragment<OID_T, VID_T, VDATA_T, EDATA_T,
                                               LoadStrategy::kBothOutIn>;
      using AppType = PageRankLocalParallel<GraphType>;
      BuildGraphAndQuery<GraphType, AppType, double, int>(
          comm_spec, efile, vfile, delta_efile, delta_vfile, out_prefix, spec,
          FLAGS_pr_d, FLAGS_pr_mr);
    } else if (name == "pagerank_auto") {
      using GraphType = MutableEdgecutFragment<OID_T, VID_T, VDATA_T, EDATA_T,
                                               LoadStrategy::kBothOutIn>;
      using AppType = PageRankAuto<GraphType>;
      BuildGraphAndQuery<GraphType, AppType, double, int>(
          comm_spec, efile, vfile, delta_efile, delta_vfile, out_prefix, spec,
          FLAGS_pr_d, FLAGS_pr_mr);
    } else if (name == "pagerank_parallel") {
      using GraphType = MutableEdgecutFragment<OID_T, VID_T, VDATA_T, EDATA_T,
                                               LoadStrategy::kBothOutIn>;
      using AppType = PageRankParallel<GraphType>;
      BuildGraphAndQuery<GraphType, AppType, double, int>(
          comm_spec, efile, vfile, delta_efile, delta_vfile, out_prefix, spec,
          FLAGS_pr_d, FLAGS_pr_mr);
    } else if (name == "cdlp_auto") {
      using GraphType = MutableEdgecutFragment<OID_T, VID_T, VDATA_T, EDATA_T,
                                               LoadStrategy::kBothOutIn>;
      using AppType = CDLPAuto<GraphType>;
      BuildGraphAndQuery<GraphType, AppType, int>(
          comm_spec, efile, vfile, delta_efile, delta_vfile, out_prefix, spec,
          FLAGS_cdlp_mr);
    } else if (name == "cdlp") {
      using GraphType = MutableEdgecutFragment<OID_T, VID_T, VDATA_T, EDATA_T,
                                               LoadStrategy::kOnlyOut>;
      using AppType = CDLP<GraphType>;
      BuildGraphAndQuery<GraphType, AppType, int>(
          comm_spec, efile, vfile, delta_efile, delta_vfile, out_prefix, spec,
          FLAGS_cdlp_mr);
    } else if (name == "wcc_auto") {
      using GraphType = MutableEdgecutFragment<OID_T, VID_T, VDATA_T, EDATA_T,
                                               LoadStrategy::kOnlyOut>;
      using AppType = WCCAuto<GraphType>;
      BuildGraphAndQuery<GraphType, AppType>(
          comm_spec, efile, vfile, delta_efile, delta_vfile, out_prefix, spec);
    } else if (name == "wcc") {
      using GraphType = MutableEdgecutFragment<OID_T, VID_T, VDATA_T, EDATA_T,
                                               LoadStrategy::kOnlyOut>;
      using AppType = WCC<GraphType>;
      BuildGraphAndQuery<GraphType, AppType>(
          comm_spec, efile, vfile, delta_efile, delta_vfile, out_prefix, spec);
    } else if (name == "lcc_auto") {
      using GraphType = MutableEdgecutFragment<OID_T, VID_T, VDATA_T, EDATA_T,
                                               LoadStrategy::kOnlyOut>;
      using AppType = LCCAuto<GraphType>;
      BuildGraphAndQuery<GraphType, AppType>(
          comm_spec, efile, vfile, delta_efile, delta_vfile, out_prefix, spec);
    } else if (name == "lcc") {
      using GraphType = MutableEdgecutFragment<OID_T, VID_T, VDATA_T, EDATA_T,
                                               LoadStrategy::kOnlyOut>;
      using AppType = LCC<GraphType>;
      BuildGraphAndQuery<GraphType, AppType>(
          comm_spec, efile, vfile, delta_efile, delta_vfile, out_prefix, spec);
    } else {
      LOG(FATAL) << "No avaiable application named [" << name << "].";
    }
  }
}

}  // namespace grape

#endif  // EXAMPLES_ANALYTICAL_APPS_RUN_MUTABLE_APP_H_
