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

#ifndef EXAMPLES_ANALYTICAL_APPS_CDLP_CDLP_BETA_H_
#define EXAMPLES_ANALYTICAL_APPS_CDLP_CDLP_BETA_H_

#include <grape/grape.h>

#include "cdlp/cdlp_beta_context.h"
#include "cdlp/cdlp_utils.h"

namespace grape {

/**
 * @brief An implementation of CDLP(Community detection using label
 * propagation), the version in LDBC, which only works on the undirected graph.
 *
 * This version of CDLP inherits ParallelAppBase. Messages can be sent in
 * parallel to the evaluation. This strategy improve performance by overlapping
 * the communication time and the evaluation time.
 *
 * @tparam FRAG_T
 */
template <typename FRAG_T>
class CDLPBeta : public ParallelAppBase<FRAG_T, CDLPBetaContext<FRAG_T>>,
             public ParallelEngine {
  INSTALL_PARALLEL_WORKER(CDLPBeta<FRAG_T>, CDLPBetaContext<FRAG_T>, FRAG_T)

 private:
  using label_t = typename context_t::label_t;
  using vid_t = typename context_t::vid_t;

  void PropagateLabel(const fragment_t& frag, context_t& ctx,
                      message_manager_t& messages) {
#ifdef PROFILING
    ctx.preprocess_time -= GetCurrentTime();
#endif

    auto inner_vertices = frag.InnerVertices();
    auto& new_ilabels = ctx.new_ilabels;

#ifdef PROFILING
    ctx.preprocess_time += GetCurrentTime();
    ctx.exec_time -= GetCurrentTime();
#endif

    // touch neighbor and send messages in parallel
    ForEach(inner_vertices,
            [&frag, &ctx, &new_ilabels, &messages](int tid, vertex_t v) {
              auto es = frag.GetOutgoingAdjList(v);
              if (!es.Empty()) {
                label_t new_label = update_label_fast<label_t>(es, ctx.labels);
                if (ctx.labels[v] != new_label) {
                  new_ilabels[v] = new_label;
                  ctx.changed[v] = true;
                  messages.SendMsgThroughOEdges<fragment_t, label_t>(
                      frag, v, new_label, tid);
                }
              }
            });

#ifdef PROFILING
    ctx.exec_time += GetCurrentTime();
    ctx.postprocess_time -= GetCurrentTime();
#endif

    ForEach(inner_vertices, [&ctx, &new_ilabels](int tid, vertex_t v) {
      if (ctx.changed[v]) {
        ctx.labels[v] = new_ilabels[v];
        ctx.changed[v] = false;
      }
    });

#ifdef PROFILING
    ctx.postprocess_time += GetCurrentTime();
#endif
  }

  void PropagateLabelSparse(const fragment_t& frag, context_t& ctx,
                            message_manager_t& messages) {
#ifdef PROFILING
    ctx.preprocess_time -= GetCurrentTime();
#endif

    auto inner_vertices = frag.InnerVertices();
    auto& new_ilabels = ctx.new_ilabels;

#ifdef PROFILING
    ctx.preprocess_time += GetCurrentTime();
    ctx.exec_time -= GetCurrentTime();
#endif

    // touch neighbor and send messages in parallel
    ForEach(ctx.potential_change,
            [&frag, &ctx, &new_ilabels, &messages](int tid, vertex_t v) {
              auto es = frag.GetOutgoingAdjList(v);
              if (!es.Empty()) {
                label_t new_label = update_label_fast<label_t>(es, ctx.labels);
                if (ctx.labels[v] != new_label) {
                  new_ilabels[v] = new_label;
                  ctx.changed[v] = true;
                  messages.SendMsgThroughOEdges<fragment_t, label_t>(
                      frag, v, new_label, tid);
                }
              }
            });
    ctx.potential_change.ParallelClear(GetThreadPool());

#ifdef PROFILING
    ctx.exec_time += GetCurrentTime();
    ctx.postprocess_time -= GetCurrentTime();
#endif

    ForEach(inner_vertices, [&ctx, &new_ilabels](int tid, vertex_t v) {
      if (ctx.changed[v]) {
        ctx.labels[v] = new_ilabels[v];
        ctx.changed[v] = false;
      }
    });

#ifdef PROFILING
    ctx.postprocess_time += GetCurrentTime();
#endif
  }

 public:
  static constexpr MessageStrategy message_strategy =
      MessageStrategy::kAlongOutgoingEdgeToOuterVertex;
  static constexpr LoadStrategy load_strategy = LoadStrategy::kOnlyOut;
  using vertex_t = typename fragment_t::vertex_t;

  void PEval(const fragment_t& frag, context_t& ctx,
             message_manager_t& messages) {
    auto inner_vertices = frag.InnerVertices();
    auto outer_vertices = frag.OuterVertices();

    messages.InitChannels(thread_num());

    ++ctx.step;
    if (ctx.step > ctx.max_round) {
      return;
    } else {
      messages.ForceContinue();
    }

#ifdef GID_AS_LABEL
    ForEach(inner_vertices, [&frag, &ctx](int tid, vertex_t v) {
      ctx.labels[v] = frag.GetInnerVertexGid(v);
    });
    ForEach(outer_vertices, [&frag, &ctx](int tid, vertex_t v) {
      ctx.labels[v] = frag.GetOuterVertexGid(v);
    });
#else
    ForEach(inner_vertices, [&frag, &ctx](int tid, vertex_t v) {
      ctx.labels[v] = frag.GetInnerVertexId(v);
    });
    ForEach(outer_vertices, [&frag, &ctx](int tid, vertex_t v) {
      ctx.labels[v] = frag.GetOuterVertexId(v);
    });
#endif

    PropagateLabel(frag, ctx, messages);
  }

  void IncEval(const fragment_t& frag, context_t& ctx,
               message_manager_t& messages) {
    ++ctx.step;

#ifdef PROFILING
    ctx.preprocess_time -= GetCurrentTime();
#endif

    // receive messages and set labels

    if (ctx.dense) {
      size_t recv_cnt = messages.ParallelProcessCount<fragment_t, label_t>(
          thread_num(), frag, [&ctx](int tid, vertex_t u, const label_t& msg) {
            ctx.labels[u] = msg;
          });
      double rate = static_cast<double>(recv_cnt) * ctx.k;

      if (rate <= ctx.threshold) {
        ctx.dense = false;
      }

      if (ctx.step > ctx.max_round) {
        return;
      } else {
        messages.ForceContinue();
      }

#ifdef PROFILING
      ctx.preprocess_time += GetCurrentTime();
#endif

      PropagateLabel(frag, ctx, messages);
    } else {
      if (ctx.step > ctx.max_round) {
        messages.ParallelProcess<fragment_t, label_t>(
            thread_num(), frag,
            [&ctx, &frag](int tid, vertex_t u, const label_t& msg) {
              ctx.labels[u] = msg;
            });
        return;
      } else {
        messages.ParallelProcess<fragment_t, label_t>(
            thread_num(), frag,
            [&ctx, &frag](int tid, vertex_t u, const label_t& msg) {
              ctx.labels[u] = msg;
              auto ie = frag.GetIncomingAdjList(u);
              for (auto& e : ie) {
                ctx.potential_change.Insert(e.neighbor);
              }
            });
        messages.ForceContinue();
      }

#ifdef PROFILING
      ctx.preprocess_time += GetCurrentTime();
#endif

      PropagateLabelSparse(frag, ctx, messages);
    }
  }
};
}  // namespace grape

#endif  // EXAMPLES_ANALYTICAL_APPS_CDLP_CDLP_BETA_H_
