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

#ifndef EXAMPLES_ANALYTICAL_APPS_RUN_APP_H_
#define EXAMPLES_ANALYTICAL_APPS_RUN_APP_H_

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>
#include <glog/logging.h>

#include <grape/fragment/immutable_edgecut_fragment.h>
#include <grape/fragment/loader.h>
#include <grape/grape.h>
#include <grape/util.h>
#include <grape/vertex_map/global_vertex_map.h>

#ifdef GRANULA
#include "thirdparty/atlarge-research-granula/granula.hpp"
#endif

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
#include "pagerank/pagerank_push.h"
#include "sssp/sssp.h"
#include "sssp/sssp_auto.h"
#include "timer.h"
#include "wcc/wcc.h"
#include "wcc/wcc_auto.h"

#ifndef __AFFINITY__
#define __AFFINITY__ false
#endif

namespace grape {

void Init() {
  if (FLAGS_deserialize && FLAGS_serialization_prefix.empty()) {
    LOG(FATAL) << "Please assign a serialization prefix.";
  } else if (FLAGS_efile.empty()) {
    LOG(FATAL) << "Please assign input edge files.";
  } else if (FLAGS_vfile.empty() && FLAGS_segmented_partition) {
    LOG(FATAL) << "EFragmentLoader dosen't support Segmented Partitioner. "
                  "Please assign vertex files or use Hash Partitioner";
  }

  if (!FLAGS_out_prefix.empty() && access(FLAGS_out_prefix.c_str(), 0) != 0) {
    mkdir(FLAGS_out_prefix.c_str(), 0777);
  }

  CommAllocatorType::get().init();
  if (CommAllocatorType::get().rank() == kCoordinatorRank) {
    VLOG(1) << "Workers of libgrape-lite initialized.";
  }
}

void Finalize() { VLOG(1) << "Workers finalized."; }

template <typename FRAG_T, typename APP_T, typename... Args>
void DoQuery(std::shared_ptr<FRAG_T> fragment, std::shared_ptr<APP_T> app,
             const ParallelEngineSpec& spec, const std::string& out_prefix,
             Args... args) {
  timer_next("load application");
  auto worker = APP_T::CreateWorker(app, fragment);
  worker->Init(CommAllocatorType::get(), spec);
  timer_next("run algorithm");
  worker->Query(std::forward<Args>(args)...);
  timer_next("print output");
  if (!out_prefix.empty()) {
    std::ofstream ostream;
    std::string output_path =
        grape::GetResultFilename(out_prefix, fragment->fid());
    ostream.open(output_path);
    worker->Output(ostream);
    ostream.close();
    worker->Finalize();
    VLOG(1) << "Worker-" << CommAllocatorType::get().rank()
            << " finished: " << output_path;
  } else {
    worker->Finalize();
    VLOG(1) << "Worker-" << CommAllocatorType::get().rank()
            << " finished without output";
  }
  timer_end();
}

template <typename OID_T, typename VID_T, typename VDATA_T, typename EDATA_T,
          LoadStrategy load_strategy, template <class> class APP_T,
          typename... Args>
void CreateAndQuery(const std::string& out_prefix,
                    const ParallelEngineSpec& spec, Args... args) {
  timer_next("load graph");
  LoadGraphSpec graph_spec = DefaultLoadGraphSpec();
  graph_spec.set_directed(FLAGS_directed);
  graph_spec.set_rebalance(FLAGS_rebalance, FLAGS_rebalance_vertex_factor);
  if (FLAGS_deserialize) {
    graph_spec.set_deserialize(true, FLAGS_serialization_prefix);
  } else if (FLAGS_serialize) {
    graph_spec.set_serialize(true, FLAGS_serialization_prefix);
  }
  CommType loader_comm = CommAllocatorType::get().allocate();
  if (FLAGS_segmented_partition) {
    using VertexMapType =
        GlobalVertexMap<OID_T, VID_T, SegmentedPartitioner<OID_T>>;
    using FRAG_T = ImmutableEdgecutFragment<OID_T, VID_T, VDATA_T, EDATA_T,
                                            load_strategy, VertexMapType>;
    std::shared_ptr<FRAG_T> fragment =
        LoadGraph<FRAG_T>(FLAGS_efile, FLAGS_vfile, loader_comm, graph_spec);
    using AppType = APP_T<FRAG_T>;
    auto app = std::make_shared<AppType>();
    DoQuery<FRAG_T, AppType, Args...>(fragment, app, loader_comm, spec,
                                      out_prefix, args...);
  } else {
    graph_spec.set_rebalance(false, 0);
    using FRAG_T =
        ImmutableEdgecutFragment<OID_T, VID_T, VDATA_T, EDATA_T, load_strategy>;
    std::shared_ptr<FRAG_T> fragment =
        LoadGraph<FRAG_T>(FLAGS_efile, FLAGS_vfile, loader_comm, graph_spec);
    using AppType = APP_T<FRAG_T>;
    auto app = std::make_shared<AppType>();
    DoQuery<FRAG_T, AppType, Args...>(fragment, app, loader_comm, spec,
                                      out_prefix, args...);
  }
}

template <typename OID_T, typename VID_T, typename VDATA_T, typename EDATA_T>
void Run() {
  int worker_id = CommAllocatorType::get().rank();
  int worker_num = CommAllocatorType::get().size();
  bool is_coordinator = worker_id == kCoordinatorRank;
  timer_start(is_coordinator);
#ifdef GRANULA
  std::string job_id = FLAGS_jobid;
  granula::startMonitorProcess(getpid());
  granula::operation grapeJob("grape", "Id.Unique", "Job", "Id.Unique");
  granula::operation loadGraph("grape", "Id.Unique", "LoadGraph", "Id.Unique");
  if (worker_id == kCoordinatorRank) {
    std::cout << grapeJob.getOperationInfo("StartTime", grapeJob.getEpoch())
              << std::endl;
    std::cout << loadGraph.getOperationInfo("StartTime", loadGraph.getEpoch())
              << std::endl;
  }

  granula::linkNode(job_id);
  granula::linkProcess(getpid(), job_id);
#endif

#ifdef GRANULA
  if (worker_id == kCoordinatorRank) {
    std::cout << loadGraph.getOperationInfo("EndTime", loadGraph.getEpoch())
              << std::endl;
  }
#endif
  // FIXME: no barrier apps. more manager? or use a dynamic-cast.
  std::string out_prefix = FLAGS_out_prefix;
  int local_id = CommAllocatorType::get().local_rank();
  int local_size = CommAllocatorType::get().local_size();
  auto spec = MultiProcessSpec(local_id, local_size, __AFFINITY__);
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
    if (name == "sssp_auto") {
      CreateAndQuery<OID_T, VID_T, VDATA_T, double, LoadStrategy::kOnlyOut,
                     SSSPAuto, OID_T>(out_prefix, spec, FLAGS_sssp_source);
    } else if (name == "sssp") {
      CreateAndQuery<OID_T, VID_T, VDATA_T, double, LoadStrategy::kOnlyOut,
                     SSSP, OID_T>(out_prefix, spec, FLAGS_sssp_source);
    } else {
      LOG(FATAL) << "No avaiable application named [" << name << "].";
    }
  } else {
    if (name == "bfs_auto") {
      CreateAndQuery<OID_T, VID_T, VDATA_T, EmptyType, LoadStrategy::kOnlyOut,
                     BFSAuto, OID_T>(out_prefix, spec, FLAGS_bfs_source);
    } else if (name == "bfs") {
      CreateAndQuery<OID_T, VID_T, VDATA_T, EmptyType, LoadStrategy::kOnlyOut,
                     BFS, OID_T>(out_prefix, spec, FLAGS_bfs_source);
    } else if (name == "pagerank_local") {
      CreateAndQuery<OID_T, VID_T, VDATA_T, EmptyType, LoadStrategy::kOnlyOut,
                     PageRankLocal, double, int>(out_prefix, spec, FLAGS_pr_d,
                                                 FLAGS_pr_mr);
    } else if (name == "pagerank_local_parallel") {
      CreateAndQuery<OID_T, VID_T, VDATA_T, EmptyType, LoadStrategy::kBothOutIn,
                     PageRankLocalParallel, double, int>(
          out_prefix, spec, FLAGS_pr_d, FLAGS_pr_mr);
    } else if (name == "pagerank_auto") {
      CreateAndQuery<OID_T, VID_T, VDATA_T, EmptyType, LoadStrategy::kBothOutIn,
                     PageRankAuto, double, int>(out_prefix, spec, FLAGS_pr_d,
                                                FLAGS_pr_mr);
    } else if (name == "pagerank") {
      CreateAndQuery<OID_T, VID_T, VDATA_T, EmptyType, LoadStrategy::kOnlyOut,
                     PageRank, double, int>(out_prefix, spec, FLAGS_pr_d,
                                            FLAGS_pr_mr);
    } else if (name == "pagerank_push") {
      CreateAndQuery<OID_T, VID_T, VDATA_T, EmptyType, LoadStrategy::kOnlyOut,
                     PageRankPush, double, int>(out_prefix, spec, FLAGS_pr_d,
                                                FLAGS_pr_mr);
    } else if (name == "pagerank_parallel") {
      CreateAndQuery<OID_T, VID_T, VDATA_T, EmptyType, LoadStrategy::kBothOutIn,
                     PageRankParallel, double, int>(out_prefix, spec,
                                                    FLAGS_pr_d, FLAGS_pr_mr);
    } else if (name == "cdlp_auto") {
      CreateAndQuery<OID_T, VID_T, VDATA_T, EmptyType, LoadStrategy::kBothOutIn,
                     CDLPAuto, int>(out_prefix, spec, FLAGS_cdlp_mr);
    } else if (name == "cdlp") {
      CreateAndQuery<OID_T, VID_T, VDATA_T, EmptyType, LoadStrategy::kOnlyOut,
                     CDLP, int>(out_prefix, spec, FLAGS_cdlp_mr);
    } else if (name == "wcc_auto") {
      CreateAndQuery<OID_T, VID_T, VDATA_T, EmptyType, LoadStrategy::kOnlyOut,
                     WCCAuto>(out_prefix, spec);
    } else if (name == "wcc") {
      CreateAndQuery<OID_T, VID_T, VDATA_T, EmptyType, LoadStrategy::kOnlyOut,
                     WCC>(out_prefix, spec);
    } else if (name == "lcc_auto") {
      CreateAndQuery<OID_T, VID_T, VDATA_T, EmptyType, LoadStrategy::kOnlyOut,
                     LCCAuto>(out_prefix, spec);
    } else if (name == "lcc") {
      CreateAndQuery<OID_T, VID_T, VDATA_T, EmptyType, LoadStrategy::kOnlyOut,
                     LCC>(out_prefix, spec, FLAGS_degree_threshold);
    } else {
      LOG(FATAL) << "No avaiable application named [" << name << "].";
    }
  }
#ifdef GRANULA
  granula::operation offloadGraph("grape", "Id.Unique", "OffloadGraph",
                                  "Id.Unique");
#endif

#ifdef GRANULA
  if (worker_id == kCoordinatorRank) {
    std::cout << offloadGraph.getOperationInfo("StartTime",
                                               offloadGraph.getEpoch())
              << std::endl;

    std::cout << grapeJob.getOperationInfo("EndTime", grapeJob.getEpoch())
              << std::endl;
  }

  granula::stopMonitorProcess(getpid());
#endif
}

}  // namespace grape

#endif  // EXAMPLES_ANALYTICAL_APPS_RUN_APP_H_
