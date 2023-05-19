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

#ifndef EXAMPLES_ANALYTICAL_APPS_LCC_LCC_DIRECTED_SORT_H_
#define EXAMPLES_ANALYTICAL_APPS_LCC_LCC_DIRECTED_SORT_H_

#include <grape/grape.h>

#include <vector>

#include "lcc/lcc_directed_context.h"

namespace grape {

/**
 * @brief An implementation of LCC (Local CLustering Coefficient), the version
 * in LDBC, which only works on undirected graphs.
 *
 * This version of LCC inherits ParallelAppBase. Messages can be sent in
 * parallel to the evaluation. This strategy improve performance by overlapping
 * the communication time and the evaluation time.
 *
 * @tparam FRAG_T
 */
template <typename FRAG_T, typename COUNT_T=uint32_t>
class LCCDirectedSort : public ParallelAppBase<FRAG_T, LCCDirectedContext<FRAG_T, COUNT_T>>,
            public ParallelEngine {
 public:
  // using app_t = LCCDirected<FRAG_T, COUNT_T>;
  // using ctx_t = LCCDirectedContext<FRAG_T, COUNT_T>;
  // INSTALL_PARALLEL_WORKER(app_t, ctx_t, FRAG_T);

  using fragment_t = FRAG_T;
  using context_t = LCCDirectedContext<FRAG_T, COUNT_T>;
  using message_manager_t = ParallelMessageManager;
  using worker_t = ParallelWorker<LCCDirectedSort<FRAG_T, COUNT_T>>;

  virtual ~LCCDirectedSort() {}

  static std::shared_ptr<worker_t> CreateWorker(std::shared_ptr<LCCDirectedSort<FRAG_T, COUNT_T>> app, std::shared_ptr<FRAG_T> frag) {
    return std::shared_ptr<worker_t>(new worker_t(app, frag));
  }
  using vertex_t = typename fragment_t::vertex_t;
  using count_t = COUNT_T;

  static constexpr MessageStrategy message_strategy =
      MessageStrategy::kAlongEdgeToOuterVertex;
  static constexpr LoadStrategy load_strategy = LoadStrategy::kBothOutIn;

  void PEval(const fragment_t& frag, context_t& ctx,
             message_manager_t& messages) {
    using vid_t = typename context_t::vid_t;
    auto inner_vertices = frag.InnerVertices();

    messages.InitChannels(thread_num());

#ifdef PROFILING
    ctx.postprocess_time -= GetCurrentTime();
#endif

    ForEach(inner_vertices, [&frag, &messages, &ctx](int tid, vertex_t v) {
      auto& nbr_vec = ctx.complete_neighbor[v];
      std::vector<vid_t> msg_vec;
      auto oes = frag.GetOutgoingAdjList(v);
      for (auto& e : oes) {
        auto u = e.get_neighbor();
        nbr_vec.push_back(u);
        msg_vec.push_back(frag.Vertex2Gid(u));
      }
      auto ies = frag.GetIncomingAdjList(v);
      for (auto& e : ies) {
        auto u = e.get_neighbor();
        nbr_vec.push_back(u);
        msg_vec.push_back(frag.Vertex2Gid(u));
      }
      std::sort(nbr_vec.begin(), nbr_vec.end());
      messages.SendMsgThroughEdges<fragment_t, std::vector<vid_t>>(frag, v, msg_vec, tid);
    });

#ifdef PROFILING
    ctx.postprocess_time += GetCurrentTime();
#endif
    // Just in case we are running on single process and no messages will
    // be send. ForceContinue() ensure the computation
    messages.ForceContinue();
  }

  void IncEval(const fragment_t& frag, context_t& ctx,
               message_manager_t& messages) {
    using vid_t = typename context_t::vid_t;

    auto inner_vertices = frag.InnerVertices();
#ifdef PROFILING
      ctx.preprocess_time -= GetCurrentTime();
#endif
      messages.ParallelProcess<fragment_t, std::vector<vid_t>>(
          thread_num(), frag,
          [&frag, &ctx](int tid, vertex_t u, const std::vector<vid_t>& msg) {
            auto& nbr_vec = ctx.complete_neighbor[u];
            for (auto gid : msg) {
              vertex_t v;
              if (frag.Gid2Vertex(gid, v)) {
                nbr_vec.push_back(v);
              }
            }
	    std::sort(nbr_vec.begin(), nbr_vec.end());
          });

      ForEach(
          inner_vertices,
          [&ctx](int tid, vertex_t v) {
            auto& v0_nbr_vec = ctx.complete_neighbor[v];
	    if (v0_nbr_vec.empty()) {
	      return ;
	    }
            std::vector<vertex_t> deduped_v0_nbr_vec;
	    deduped_v0_nbr_vec.push_back(v0_nbr_vec[0]);
	    int v0_nbr_num = v0_nbr_vec.size();
	    for (int i = 1; i < v0_nbr_num; ++i) {
	      if (v0_nbr_vec[i] != v0_nbr_vec[i - 1]) {
	        deduped_v0_nbr_vec.push_back(v0_nbr_vec[i]);
	      }
	    }
            ctx.global_degree[v] = deduped_v0_nbr_vec.size();
            int count = 0;
	    const vertex_t* end1 = deduped_v0_nbr_vec.data() + deduped_v0_nbr_vec.size();
            for (auto u : deduped_v0_nbr_vec) {
	      const vertex_t* ptr1 = deduped_v0_nbr_vec.data();

              const auto& v1_nbr_vec = ctx.complete_neighbor[u];
	      const vertex_t* end2 = v1_nbr_vec.data() + v1_nbr_vec.size();
	      const vertex_t* ptr2 = std::lower_bound(v1_nbr_vec.data(), end2, u);

	      while (ptr1 != end1 && ptr2 != end2) {
                if (*ptr1 == *ptr2) {
                  ++ptr2;
		  ++count;
		} else if (*ptr1 < *ptr2) {
                  ++ptr1;
		} else {
                  ++ptr2;
		}
	      }
            }
            ctx.tricnt[v] = count;
          });

#ifdef PROFILING
      ctx.exec_time += GetCurrentTime();
      ctx.postprocess_time -= GetCurrentTime();
#endif
  }
};
}  // namespace grape

#endif  // EXAMPLES_ANALYTICAL_APPS_LCC_LCC_DIRECTED_SORT_H_
