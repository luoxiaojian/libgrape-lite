#ifndef EXAMPLES_ANALYTICAL_APPS_PAGERANK_PAGERANK_PUSH_CONTEXT_H_
#define EXAMPLES_ANALYTICAL_APPS_PAGERANK_PAGERANK_PUSH_CONTEXT_H_

#include <grape/grape.h>

#include <iomanip>

namespace grape {
/**
 * @brief Context for the batch version of PageRank.
 *
 * @tparam FRAG_T
 */
template <typename FRAG_T>
class PageRankPushContext : public VertexDataContext<FRAG_T, double> {
  using oid_t = typename FRAG_T::oid_t;
  using vid_t = typename FRAG_T::vid_t;

 public:
  explicit PageRankPushContext(const FRAG_T& fragment)
      : VertexDataContext<FRAG_T, double>(fragment),
        result(this->data()) {}

  void Init(ParallelMessageManager& messages, double delta, int max_round) {
    auto& frag = this->fragment();
    auto inner_vertices = frag.InnerVertices();

    this->delta = delta;
    this->max_round = max_round;
    next_result.Init(inner_vertices);
    step = 0;

  }

  void Output(std::ostream& os) override {
    auto& frag = this->fragment();
    auto inner_vertices = frag.InnerVertices();
    for (auto v : inner_vertices) {
      os << frag.GetId(v) << " " << std::scientific << std::setprecision(15)
         << result[v] << std::endl;
    }
  }

  typename FRAG_T::template inner_vertex_array_t<double>& result;
  typename FRAG_T::template inner_vertex_array_t<double> next_result;

#ifdef PROFILING
  double preprocess_time = 0;
  double exec_time = 0;
  double postprocess_time = 0;
#endif

  vid_t total_dangling_vnum = 0;
  vid_t graph_vnum;
  int step = 0;
  int max_round = 0;
  double delta = 0;

  double dangling_sum = 0.0;
};
}  // namespace grape

#endif  // EXAMPLES_ANALYTICAL_APPS_PAGERANK_PAGERANK_PUSH_CONTEXT_H_
