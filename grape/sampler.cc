

template <typename OID_T, typename VID_T, typename VDATA_T, typename EDATA_T>
class AppendOnlyEdgecutFragment
    : public ImmutableEdgecutFragment<OID_T, VID_T, VDATA_T, EDATA_T> {
 public:
  void ExtendFragment(const std::vector<Edge<OID_T, EDATA_T>>& edge_messages);

  // 基于邻接表的图结构的数据结构和访问接口
};

template <typename FRAG_T>
class Sampler : public AppBase<FRAG_T, SamplerContext<FRAG_T>> {
 public:
  void PEval(const fragment_t& fragment, context_t& ctx,
             message_manager_t& messages) {
    // sampler的PEval实现
  }
  void IncEval(const fragment_t& fragment, context_t& ctx,
               message_manager_t& messages) {
    // sampler的IncEval实现
  }
};

using graph_t = AppendOnlyEdgecutFragment<oid_t, vid_t, vdata_t, edata_t>;
using app_t = Sampler<graph_t>;
auto fragment = LoadGraph<graph_t>(edge_file, vertex_file);
auto sampler = std::make_shared<app_t>();

std::unique_ptr<KafkaConsumer> consumer(new KafkaConsumer(...));
std::shared_ptr<KafkaProducer> producer(new KafkaProducer(...));
KafkaOutputStream ostream(producer);

while (true) {
  consumer->ConsumeMessages(query_vertices, edges);
  fragment->ExtendFragment(edges);
  if (!query_vertices.empty()) {
    auto worker = app_t::CreateWorker(sampler, fragment);
    worker->Init();
    worker->Query(sampling_options, query_vertices);
    worker->Output(ostream);
  }
}

void PEval(const fragment_t& frag, context_t& ctx) {
  vertex_t source;
  bool native_source = frag.GetInnerVertex(ctx.source_id, source);

  std::priority_queue<std::pair<double, vertex_t>> heap;

  if (native_source) {
    ctx.partial_result.SetValue(source, 0.0);
    heap.emplace(0, source);
  }

  Dijkstra(frag, ctx, heap);
}

void IncEval(const fragment_t& frag, context_t& ctx) {
  auto inner_vertices = frag.InnerVertices();

  std::priority_queue<std::pair<double, vertex_t>> heap;

  for (auto& v : inner_vertices) {
    if (ctx.partial_result.IsUpdated(v)) {
      heap.emplace(-ctx.partial_result.GetValue(v), v);
    }
  }

  Dijkstra(frag, ctx, heap);
}

// sequential Dijkstra algorithm for SSSP.
void Dijkstra(const fragment_t& frag, context_t& ctx,
              std::priority_queue<std::pair<double, vertex_t>>& heap) {
  while (!heap.empty()) {
    vertex_t u = heap.top().second;
    double distu = -heap.top().first;
    heap.pop();

    // ...
  }
}
