#ifndef EXAMPLES_ANALYTICAL_APPS_PAGERANK_PAGERANK_PUSH_H_
#define EXAMPLES_ANALYTICAL_APPS_PAGERANK_PAGERANK_PUSH_H_

#include <grape/grape.h>

#include "pagerank/pagerank_push_context.h"

namespace grape {

/**
 * @brief An implementation of PageRank, which can work
 * on undirected graphs.
 *
 * This version of PageRank inherits BatchShuffleAppBase.
 * Messages are generated in batches and received in-place.
 *
 * @tparam FRAG_T
 */
template <typename FRAG_T>
class PageRankPush : public ParallelAppBase<FRAG_T, PageRankPushContext<FRAG_T>>,
                 public ParallelEngine,
                 public Communicator {
 public:
  INSTALL_PARALLEL_WORKER(PageRankPush<FRAG_T>, PageRankPushContext<FRAG_T>,
                               FRAG_T)

  using vertex_t = typename FRAG_T::vertex_t;
  using vid_t = typename FRAG_T::vid_t;

  static constexpr bool need_split_edges = true;
  static constexpr LoadStrategy load_strategy = LoadStrategy::kOnlyOut;

  PageRankPush() = default;

  void PEval(const fragment_t& frag, context_t& ctx,
             message_manager_t& messages) {
    if (ctx.max_round <= 0) {
      return;
    }
    messages.InitChannels(thread_num());

    auto inner_vertices = frag.InnerVertices();
    auto outer_vertices = frag.OuterVertices();

#ifdef PROFILING
    ctx.exec_time -= GetCurrentTime();
#endif

    ctx.step = 0;
    ctx.graph_vnum = frag.GetTotalVerticesNum();
    vid_t dangling_vnum = 0;
    double p = 1.0 / ctx.graph_vnum;

    std::vector<vid_t> dangling_vnum_tid(thread_num(), 0);
    ForEach(inner_vertices,
            [&ctx, &frag, p, &dangling_vnum_tid](int tid, vertex_t u) {
              int EdgeNum = frag.GetLocalOutDegree(u);
              if (EdgeNum > 0) {
                ctx.result[u] = p / EdgeNum;
              } else {
                ++dangling_vnum_tid[tid];
                ctx.result[u] = p;
              }
#if 0
	      ctx.next_result[u] = 0;
#endif
            });

    for (auto vn : dangling_vnum_tid) {
      dangling_vnum += vn;
    }

    Sum(dangling_vnum, ctx.total_dangling_vnum);
    ctx.dangling_sum = p * ctx.total_dangling_vnum;

    auto& channels = messages.Channels();
    ForEach(outer_vertices, [&ctx, &frag, &channels](int tid, vertex_t u) {
      auto es = frag.GetIncomingAdjList(u);
      double msg = 0.0;
      for (auto& e : es) {
        msg += ctx.result[e.get_neighbor()];
      }
      channels[tid].SyncStateOnOuterVertex<fragment_t, double>(frag, u, msg);
    });
  }

  void IncEval(const fragment_t& frag, context_t& ctx,
               message_manager_t& messages) {
    auto inner_vertices = frag.InnerVertices();
    auto outer_vertices = frag.OuterVertices();
    ++ctx.step;

    double base = (1.0 - ctx.delta) / ctx.graph_vnum +
                  ctx.delta * ctx.dangling_sum / ctx.graph_vnum;
    ctx.dangling_sum = base * ctx.total_dangling_vnum;

#if 1
    ForEach(inner_vertices, [&ctx, &frag](int tid, vertex_t u) {
      double cur = 0;
      auto es = frag.GetOutgoingInnerVertexAdjList(u);
      for (auto& e : es) {
        cur += ctx.result[e.get_neighbor()];
      }
      ctx.next_result[u] = cur;
    });

    messages.ParallelProcess<fragment_t, double>(thread_num(), frag, [&ctx](int tid, vertex_t u, double msg) {
      atomic_add(ctx.next_result[u], msg);
      // ctx.next_result[u] += msg;
    });

    if (ctx.step != ctx.max_round) {
      ForEach(inner_vertices, [&ctx, &frag, base] (int tid, vertex_t u) {
        int en = frag.GetLocalOutDegree(u);
        ctx.result[u] = en > 0 ? (ctx.delta * ctx.next_result[u] + base) / en : base;
        ctx.next_result[u] = 0;
      });

      auto& channels = messages.Channels();
      ForEach(outer_vertices, [&ctx, &frag, &channels](int tid, vertex_t u) {
        auto es = frag.GetIncomingAdjList(u);
        double msg = 0.0;
        for (auto& e : es) {
          msg += ctx.result[e.get_neighbor()];
        }
        channels[tid].SyncStateOnOuterVertex<fragment_t, double>(frag, u, msg);
      });
    } else {
      ForEach(inner_vertices, [&ctx, &frag, base] (int tid, vertex_t u) {
        int en = frag.GetLocalOutDegree(u);
        ctx.result[u] = en > 0 ? (ctx.delta * ctx.next_result[u] + base) : base;
      });
    }
#else
    messages.ParallelProcess<fragment_t, double>(thread_num(), frag, [&ctx](int tid, vertex_t u, double msg) {
      atomic_add(ctx.next_result[u], msg);
      // ctx.next_result[u] += msg;
    });

    if (ctx.step != ctx.max_round) {
      ForEach(inner_vertices, [&ctx, &frag, base] (int tid, vertex_t u) {
        double cur = ctx.next_result[u];
        auto es = frag.GetOutgoingInnerVertexAdjList(u);
        for (auto& e : es) {
          cur += ctx.result[e.get_neighbor()];
        }

        int en = frag.GetLocalOutDegree(u);
        ctx.result[u] = en > 0 ? (ctx.delta * cur + base) / en : base;
        ctx.next_result[u] = 0;
      });

      auto& channels = messages.Channels();
      ForEach(outer_vertices, [&ctx, &frag, &channels](int tid, vertex_t u) {
        auto es = frag.GetIncomingAdjList(u);
        double msg = 0.0;
        for (auto& e : es) {
          msg += ctx.result[e.get_neighbor()];
        }
        channels[tid].SyncStateOnOuterVertex<fragment_t, double>(frag, u, msg);
      });
    } else {
      ForEach(inner_vertices, [&ctx, &frag, base] (int tid, vertex_t u) {
        double cur = ctx.next_result[u];
        auto es = frag.GetOutgoingInnerVertexAdjList(u);
        for (auto& e : es) {
          cur += ctx.result[e.get_neighbor()];
        }

        int en = frag.GetLocalOutDegree(u);
        ctx.result[u] = en > 0 ? (ctx.delta * ctx.next_result[u] + base) : base;
      });
    }
#endif
  }
};

}  // namespace grape

#endif  // EXAMPLES_ANALYTICAL_APPS_PAGERANK_PAGERANK_PUSH_H_
