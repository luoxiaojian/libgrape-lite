

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
