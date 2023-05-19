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

#ifndef EXAMPLES_ANALYTICAL_APPS_WCC_WCC_BETA_H_
#define EXAMPLES_ANALYTICAL_APPS_WCC_WCC_BETA_H_

#include <grape/grape.h>

#include "wcc/wcc_beta_context.h"

namespace grape {

#define MIN_COMP_ID(a, b) ((a) > (b) ? (b) : (a))

/**
 * @brief WCC application, determines the weakly connected component each vertex
 * belongs to, which only works on both undirected graph.
 *
 * This version of WCC inherits ParallelAppBase. Messages can be sent in
 * parallel to the evaluation. This strategy improve performance by overlapping
 * the communication time and the evaluation time.
 *
 * @tparam FRAG_T
 */
template <typename FRAG_T>
class WCCBeta : public ParallelAppBase<FRAG_T, WCCBetaContext<FRAG_T>>,
                public ParallelEngine {
  INSTALL_PARALLEL_WORKER(WCCBeta<FRAG_T>, WCCBetaContext<FRAG_T>, FRAG_T)
  using vertex_t = typename fragment_t::vertex_t;
  using oid_t = typename fragment_t::oid_t;
  using vid_t = typename fragment_t::vid_t;

  static constexpr bool need_split_edges = true;

 public:
  void PEval(const fragment_t& frag, context_t& ctx,
             message_manager_t& messages) {
    auto inner_vertices = frag.InnerVertices();
    auto outer_vertices = frag.OuterVertices();

    messages.InitChannels(thread_num());
    auto& channels = messages.Channels();

    ForEach(outer_vertices, [&frag, &ctx](int tid, vertex_t v) {
      auto es = frag.GetIncomingAdjList(v);
      vertex_t parent = v;
      for (auto& e : es) {
        auto u = e.get_neighbor();
        if (u < parent) {
          parent = u;
        }
      }
      ctx.tree[v] = parent;
    });
    ForEach(inner_vertices, [&frag, &ctx](int tid, vertex_t v) {
      auto es = frag.GetOutgoingInnerVertexAdjList(v);
      vertex_t parent = v;
      for (auto& e : es) {
        parent = MIN_COMP_ID(parent, e.get_neighbor());
      }
      auto oes = frag.GetOutgoingOuterVertexAdjList(v);
      for (auto& e : oes) {
        parent = MIN_COMP_ID(parent, ctx.tree[e.get_neighbor()]);
      }
      ctx.comp_id[v] = std::numeric_limits<oid_t>::max();
    });
    ForEach(inner_vertices, [&ctx, &frag](int tid, vertex_t v) {
      auto cur = v;
      while (cur != ctx.tree[cur]) {
        cur = ctx.tree[cur];
      }
      ctx.tree[v] = cur;
      oid_t cid = frag.GetInnerVertexId(v);
      atomic_min(ctx.comp_id[cur], cid);
    });
    ForEach(outer_vertices, [&ctx, &frag](int tid, vertex_t v) {
      auto cur = v;
      while (cur != ctx.tree[cur]) {
        cur = ctx.tree[cur];
      }
      ctx.tree[v] = cur;
      oid_t cid = frag.GetOuterVertexId(v);
      atomic_min(ctx.comp_id[cur], cid);
    });

    ForEach(outer_vertices, [&ctx, &frag, &channels](int tid, vertex_t v) {
      oid_t origin_cid = frag.GetOuterVertexId(v);
      if (ctx.comp_id[v] < origin_cid) {
        channels[tid].SyncStateOnOuterVertex<fragment_t, oid_t>(frag, v,
                                                                ctx.comp_id[v]);
      }
    });
  }

  void IncEval(const fragment_t& frag, context_t& ctx,
               message_manager_t& messages) {
    ctx.modified.ParallelClear(GetThreadPool());

    // aggregate messages
    messages.ParallelProcess<fragment_t, oid_t>(
        thread_num(), frag, [&ctx](int tid, vertex_t u, oid_t msg) {
          vertex_t root = ctx.tree[u];
          if (ctx.comp_id[root] > msg) {
            atomic_min(ctx.comp_id[root], msg);
            ctx.modified.Insert(root);
          }
        });

    auto& channels = messages.Channels();

    ForEach(frag.OuterVertices(),
            [&frag, &ctx, &channels](int tid, vertex_t v) {
              vertex_t root = ctx.tree[v];
              if (ctx.modified.Exist(root)) {
                channels[tid].SyncStateOnOuterVertex<fragment_t, oid_t>(
                    frag, v, ctx.comp_id[root]);
              }
            });
  }
};

}  // namespace grape
#endif  // EXAMPLES_ANALYTICAL_APPS_WCC_WCC_H_
