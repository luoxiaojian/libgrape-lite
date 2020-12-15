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

#ifndef EXAMPLES_ANALYTICAL_APPS_SSSP_DEFAULT_SSSP_H_
#define EXAMPLES_ANALYTICAL_APPS_SSSP_DEFAULT_SSSP_H_

#include <grape/grape.h>

#include "sssp/default_sssp_context.h"

namespace grape {

template <typename FRAG_T>
class DefaultSSSP : public DefaultAppBase<FRAG_T, DefaultSSSPContext<FRAG_T>> {
 public:
  // specialize the templated worker.
  INSTALL_DEFAULT_WORKER(DefaultSSSP<FRAG_T>, DefaultSSSPContext<FRAG_T>,
                         FRAG_T)
  using vertex_t = typename fragment_t::vertex_t;

  /**
   * @brief Partial evaluation for DefaultSSSP.
   *
   * @param frag
   * @param ctx
   * @param messages
   */
  void PEval(const fragment_t& frag, context_t& ctx,
             message_manager_t& messages) {
    auto inner_vertices = frag.InnerVertices();

    vertex_t source;
    bool native_source = frag.GetInnerVertex(ctx.source_id, source);

#ifdef PROFILING
    ctx.exec_time -= GetCurrentTime();
#endif

    ctx.next_modified.Clear();

    if (native_source) {
      ctx.partial_result[source] = 0;
      auto es = frag.GetOutgoingAdjList(source);
      for (auto& e : es) {
        vertex_t v = e.get_neighbor();
        ctx.partial_result[v] =
            std::min(ctx.partial_result[v], static_cast<double>(e.get_data()));
        if (frag.IsOuterVertex(v)) {
          // put the message to the channel.
          messages.SyncStateOnOuterVertex<fragment_t, double>(
              frag, v, ctx.partial_result[v]);
        } else {
          ctx.next_modified.Insert(v);
        }
      }
    }

#ifdef PROFILING
    ctx.exec_time += GetCurrentTime();
    ctx.postprocess_time -= GetCurrentTime();
#endif

    messages.ForceContinue();

    ctx.next_modified.Swap(ctx.curr_modified);
#ifdef PROFILING
    ctx.postprocess_time += GetCurrentTime();
#endif
  }

  /**
   * @brief Incremental evaluation for DefaultSSSP.
   *
   * @param frag
   * @param ctx
   * @param messages
   */
  void IncEval(const fragment_t& frag, context_t& ctx,
               message_manager_t& messages) {
    auto inner_vertices = frag.InnerVertices();

#ifdef PROFILING
    ctx.preprocess_time -= GetCurrentTime();
#endif

    ctx.next_modified.Clear();

    {
      vertex_t u;
      double msg;
      while (messages.GetMessage<fragment_t, double>(frag, u, msg)) {
        if (ctx.partial_result[u] > msg) {
          ctx.partial_result[u] = msg;
          ctx.curr_modified.Insert(u);
        }
      }
    }

#ifdef PROFILING
    ctx.preprocess_time += GetCurrentTime();
    ctx.exec_time -= GetCurrentTime();
#endif

    for (auto v : inner_vertices) {
      if (ctx.curr_modified.Exist(v)) {
        double distv = ctx.partial_result[v];
        auto es = frag.GetOutgoingAdjList(v);
        for (auto& e : es) {
          vertex_t u = e.get_neighbor();
          double ndistu = distv + e.get_data();
          if (ndistu < ctx.partial_result[u]) {
            ctx.partial_result[u] = ndistu;
            ctx.next_modified.Insert(u);
          }
        }
      }
    }

#ifdef PROFILING
    ctx.exec_time += GetCurrentTime();
    ctx.postprocess_time -= GetCurrentTime();
#endif
    auto outer_vertices = frag.OuterVertices();
    for (auto v : outer_vertices) {
      if (ctx.next_modified.Exist(v)) {
        messages.SyncStateOnOuterVertex<fragment_t, double>(
            frag, v, ctx.partial_result[v]);
      }
    }

    if (!ctx.next_modified.PartialEmpty(0, frag.GetInnerVerticesNum())) {
      messages.ForceContinue();
    }

    ctx.next_modified.Swap(ctx.curr_modified);
#ifdef PROFILING
    ctx.postprocess_time += GetCurrentTime();
#endif
  }
};

}  // namespace grape

#endif  // EXAMPLES_ANALYTICAL_APPS_SSSP_DEFAULT_SSSP_H_
