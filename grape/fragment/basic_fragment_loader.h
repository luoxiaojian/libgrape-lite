#ifndef GRAPE_FRAGMENT_BASIC_FRAGMENT_LOADER_H_
#define GRAPE_FRAGMENT_BASIC_FRAGMENT_LOADER_H_

#include <stddef.h>

#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "grape/communication/shuffle.h"
#include "grape/config.h"
#include "grape/fragment/rebalancer.h"
#include "grape/graph/edge.h"
#include "grape/graph/vertex.h"
#include "grape/utils/concurrent_queue.h"
#include "grape/utils/vertex_array.h"
#include "grape/worker/comm_spec.h"

namespace grape {

/**
 * @brief LoadGraphSpec determines the specification to load a graph.
 *
 */
struct LoadGraphSpec {
  bool directed;
  bool rebalance;
  int rebalance_vertex_factor;

  bool serialize;
  std::string serialization_prefix;

  bool deserialize;
  std::string deserialization_prefix;

  void set_directed(bool val = true) { directed = val; }
  void set_rebalance(bool flag, int weight) {
    rebalance = flag;
    rebalance_vertex_factor = weight;
  }

  void set_serialize(bool flag, const std::string& prefix) {
    serialize = flag;
    serialization_prefix = prefix;
  }

  void set_deserialize(bool flag, const std::string& prefix) {
    deserialize = flag;
    deserialization_prefix = prefix;
  }
};

inline LoadGraphSpec DefaultLoadGraphSpec() {
  LoadGraphSpec spec;
  spec.directed = true;
  spec.rebalance = true;
  spec.rebalance_vertex_factor = 0;
  spec.serialize = false;
  spec.deserialize = false;
  return spec;
}

template <typename FRAG_T, typename IOADAPTOR_T>
class BasicFragmentLoader {
  using fragment_t = FRAG_T;
  using oid_t = typename fragment_t::oid_t;
  using vid_t = typename fragment_t::vid_t;
  using vdata_t = typename fragment_t::vdata_t;
  using edata_t = typename fragment_t::edata_t;

  using vertex_map_t = typename fragment_t::vertex_map_t;
  using partitioner_t = typename vertex_map_t::partitioner_t;

  static constexpr LoadStrategy load_strategy = fragment_t::load_strategy;

 public:
  explicit BasicFragmentLoader(const CommSpec& comm_spec)
      : comm_spec_(comm_spec) {
    comm_spec_.Dup();
    vm_ptr_ = std::make_shared<vertex_map_t>(comm_spec_);
    vertices_to_frag_.resize(comm_spec_.fnum());
    edges_to_frag_.resize(comm_spec_.fnum());
    for (fid_t fid = 0; fid < comm_spec_.fnum(); ++fid) {
      int worker_id = comm_spec_.FragToWorker(fid);
      vertices_to_frag_[fid].Init(comm_spec_.comm(), vertex_tag);
      vertices_to_frag_[fid].SetDestination(worker_id, fid);
      edges_to_frag_[fid].Init(comm_spec_.comm(), edge_tag);
      edges_to_frag_[fid].SetDestination(worker_id, fid);
      if (worker_id == comm_spec_.worker_id()) {
        vertices_to_frag_[fid].DisableComm();
        edges_to_frag_[fid].DisableComm();
      }
    }

    recv_thread_running_ = false;
  }

  ~BasicFragmentLoader() { Stop(); }

  void SetPartitioner(const partitioner_t& partitioner) {
    vm_ptr_->SetPartitioner(partitioner);
  }

  void SetPartitioner(partitioner_t&& partitioner) {
    vm_ptr_->SetPartitioner(std::move(partitioner));
  }

  void SetRebalance(bool rebalance, int rebalance_vertex_factor) {
    rebalance_ = rebalance;
    rebalance_vertex_factor_ = rebalance_vertex_factor;
  }

  void Start() {
    vertex_recv_thread_ =
        std::thread(&BasicFragmentLoader::vertexRecvRoutine, this);
    edge_recv_thread_ =
        std::thread(&BasicFragmentLoader::edgeRecvRoutine, this);
    recv_thread_running_ = true;
  }

  void Stop() {
    if (recv_thread_running_) {
      for (auto& va : vertices_to_frag_) {
        va.Flush();
      }
      for (auto& ea : edges_to_frag_) {
        ea.Flush();
      }
      vertex_recv_thread_.join();
      edge_recv_thread_.join();
      recv_thread_running_ = false;
    }
  }

  void AddVertex(const oid_t& id, const vdata_t& data) {
    auto& partitioner = vm_ptr_->GetPartitioner();
    fid_t fid = partitioner.GetPartitionId(id);
    vdata_t ref_data(data);
    vertices_to_frag_[fid].Emplace(id, ref_data);
  }

  void AddEdge(const oid_t& src, const oid_t& dst, const edata_t& data) {
    auto& partitioner = vm_ptr_->GetPartitioner();
    fid_t src_fid = partitioner.GetPartitionId(src);
    fid_t dst_fid = partitioner.GetPartitionId(dst);
    edata_t ref_data(data);
    edges_to_frag_[src_fid].Emplace(src, dst, ref_data);
    if (src_fid != dst_fid) {
      edges_to_frag_[dst_fid].Emplace(src, dst, ref_data);
    }
  }

  bool SerializeFragment(std::shared_ptr<fragment_t>& fragment,
                         const std::string& serialization_prefix) {
    if (comm_spec_.worker_id() == 0) {
      vm_ptr_->template Serialize<IOADAPTOR_T>(serialization_prefix);
    }

    MPI_Barrier(comm_spec_.comm());

    // If not using a nfs, each worker should serialize a copy of vertex map.
    auto exists_file = [](const std::string& name) {
      std::ifstream f(name.c_str());
      return f.good();
    };
    char serial_file[1024];
    snprintf(serial_file, sizeof(serial_file), "%s/%s",
             serialization_prefix.c_str(), kSerializationVertexMapFilename);
    if (comm_spec_.local_id() == 0 && !exists_file(serial_file)) {
      vm_ptr_->template Serialize<IOADAPTOR_T>(serialization_prefix);
    }

    fragment->template Serialize<IOADAPTOR_T>(serialization_prefix);

    return true;
  }

  bool DeserializeFragment(std::shared_ptr<fragment_t>& fragment,
                           const std::string& deserialization_prefix) {
    auto io_adaptor =
        std::unique_ptr<IOADAPTOR_T>(new IOADAPTOR_T(deserialization_prefix));
    if (io_adaptor->IsExist()) {
      vm_ptr_->template Deserialize<IOADAPTOR_T>(deserialization_prefix);
      fragment = std::shared_ptr<fragment_t>(new fragment_t(vm_ptr_));
      fragment->template Deserialize<IOADAPTOR_T>(deserialization_prefix,
                                                  comm_spec_.fid());
    }
    return true;
  }

  void ConstructFragment(std::shared_ptr<fragment_t>& fragment) {
    for (auto& va : vertices_to_frag_) {
      va.Flush();
    }
    for (auto& ea : edges_to_frag_) {
      ea.Flush();
    }
    vertex_recv_thread_.join();
    edge_recv_thread_.join();
    recv_thread_running_ = false;

    MPI_Barrier(comm_spec_.comm());

    got_vertices_id_.emplace_back(
        std::move(get_buffer<0>(vertices_to_frag_[comm_spec_.fid()])));
    got_vertices_data_.emplace_back(
        std::move(get_buffer<1>(vertices_to_frag_[comm_spec_.fid()])));
    vertices_to_frag_[comm_spec_.fid()].Clear();
    got_edges_src_.emplace_back(
        std::move(get_buffer<0>(edges_to_frag_[comm_spec_.fid()])));
    got_edges_dst_.emplace_back(
        std::move(get_buffer<1>(edges_to_frag_[comm_spec_.fid()])));
    got_edges_data_.emplace_back(
        std::move(get_buffer<2>(edges_to_frag_[comm_spec_.fid()])));
    edges_to_frag_[comm_spec_.fid()].Clear();

    vm_ptr_->Init();
    auto builder = vm_ptr_->GetLocalBuilder();
    size_t v_buf_num = got_vertices_id_.size();
    size_t e_buf_num = got_edges_src_.size();
    for (size_t i = 0; i < v_buf_num; ++i) {
      for (auto& id : got_vertices_id_[i]) {
        builder.add_vertex(id);
      }
    }
    for (size_t i = 0; i < e_buf_num; ++i) {
      for (auto& id : got_edges_src_[i]) {
        builder.add_vertex(id);
      }
      for (auto& id : got_edges_dst_[i]) {
        builder.add_vertex(id);
      }
    }
    builder.finish(*vm_ptr_);

    processed_vertices_.clear();
    if (!std::is_same<vdata_t, EmptyType>::value) {
      for (size_t i = 0; i < v_buf_num; ++i) {
        vid_t gid;
        size_t buf_size = got_vertices_id_[i].size();
        for (size_t k = 0; k < buf_size; ++k) {
          CHECK(vm_ptr_->GetGid(got_vertices_id_[i][k], gid));
          processed_vertices_.emplace_back(gid,
                                           std::move(got_vertices_data_[i][k]));
        }
      }
    }
    got_vertices_id_.clear();
    got_vertices_data_.clear();

    for (size_t i = 0; i < e_buf_num; ++i) {
      size_t buf_size = got_edges_src_[i].size();
      vid_t src_gid, dst_gid;
      for (size_t k = 0; k < buf_size; ++k) {
        CHECK(vm_ptr_->GetGid(got_edges_src_[i][k], src_gid));
        CHECK(vm_ptr_->GetGid(got_edges_dst_[i][k], dst_gid));
        processed_edges_.emplace_back(src_gid, dst_gid, got_edges_data_[i][k]);
      }
    }

    fragment = std::shared_ptr<fragment_t>(new fragment_t(vm_ptr_));
    fragment->Init(comm_spec_.fid(), processed_vertices_, processed_edges_);

    if (!std::is_same<vdata_t, EmptyType>::value) {
      initOuterVertexData(fragment);
    }
  }

  void vertexRecvRoutine() {
    ShuffleIn<oid_t, vdata_t> data_in;
    data_in.Init(comm_spec_.fnum(), comm_spec_.comm(), vertex_tag);
    fid_t dst_fid;
    int src_worker_id;
    while (!data_in.Finished()) {
      src_worker_id = data_in.Recv(dst_fid);
      if (src_worker_id == -1) {
        break;
      }
      auto& dst_buf0 = got_vertices_id_;
      auto& dst_buf1 = got_vertices_data_;
      dst_buf0.emplace_back(std::move(get_buffer<0>(data_in)));
      dst_buf1.emplace_back(std::move(get_buffer<1>(data_in)));
      data_in.Clear();
    }
  }

  void edgeRecvRoutine() {
    ShuffleIn<oid_t, oid_t, edata_t> data_in;
    data_in.Init(comm_spec_.fnum(), comm_spec_.comm(), edge_tag);
    fid_t dst_fid;
    int src_worker_id;
    while (!data_in.Finished()) {
      src_worker_id = data_in.Recv(dst_fid);
      if (src_worker_id == -1) {
        break;
      }
      CHECK_EQ(dst_fid, comm_spec_.fid());
      got_edges_src_.emplace_back(std::move(get_buffer<0>(data_in)));
      got_edges_dst_.emplace_back(std::move(get_buffer<1>(data_in)));
      got_edges_data_.emplace_back(std::move(get_buffer<2>(data_in)));
      data_in.Clear();
    }
  }

  void initOuterVertexData(std::shared_ptr<fragment_t> fragment) {
    int worker_num = comm_spec_.worker_num();

    std::vector<std::vector<vid_t>> request_gid_lists(worker_num);
    auto& outer_vertices = fragment->OuterVertices();
    for (auto& v : outer_vertices) {
      fid_t fid = fragment->GetFragId(v);
      request_gid_lists[comm_spec_.FragToWorker(fid)].emplace_back(
          fragment->GetOuterVertexGid(v));
    }
    std::vector<std::vector<vid_t>> requested_gid_lists(worker_num);
    sync_comm::AllToAll(request_gid_lists, requested_gid_lists,
                        comm_spec_.comm());
    std::vector<std::vector<vdata_t>> response_vdata_lists(worker_num);
    for (int i = 0; i < worker_num; ++i) {
      auto& id_vec = requested_gid_lists[i];
      auto& data_vec = response_vdata_lists[i];
      data_vec.reserve(id_vec.size());
      for (auto id : id_vec) {
        typename fragment_t::vertex_t v;
        CHECK(fragment->InnerVertexGid2Vertex(id, v));
        data_vec.emplace_back(fragment->GetData(v));
      }
    }
    std::vector<std::vector<vdata_t>> responsed_vdata_lists(worker_num);
    sync_comm::AllToAll(response_vdata_lists, responsed_vdata_lists,
                        comm_spec_.comm());
    for (int i = 0; i < worker_num; ++i) {
      auto& id_vec = request_gid_lists[i];
      auto& data_vec = responsed_vdata_lists[i];
      CHECK_EQ(id_vec.size(), data_vec.size());
      size_t num = id_vec.size();
      for (size_t k = 0; k < num; ++k) {
        typename fragment_t::vertex_t v;
        CHECK(fragment->OuterVertexGid2Vertex(id_vec[k], v));
        fragment->SetData(v, data_vec[k]);
      }
    }
  }

 private:
  CommSpec comm_spec_;
  std::shared_ptr<vertex_map_t> vm_ptr_;

  std::vector<ShuffleOut<oid_t, vdata_t>> vertices_to_frag_;
  std::vector<ShuffleOut<oid_t, oid_t, edata_t>> edges_to_frag_;

  std::thread vertex_recv_thread_;
  std::thread edge_recv_thread_;
  bool recv_thread_running_;

  std::vector<std::vector<oid_t>> got_vertices_id_;
  std::vector<std::vector<vdata_t>> got_vertices_data_;

  std::vector<std::vector<oid_t>> got_edges_src_;
  std::vector<std::vector<oid_t>> got_edges_dst_;
  std::vector<std::vector<edata_t>> got_edges_data_;

  std::vector<internal::Vertex<vid_t, vdata_t>> processed_vertices_;
  std::vector<Edge<vid_t, edata_t>> processed_edges_;

  static constexpr int vertex_tag = 5;
  static constexpr int edge_tag = 6;

  bool rebalance_;
  int rebalance_vertex_factor_;
};

}  // namespace grape

#endif  // GRAPE_FRAGMENT_BASIC_FRAGMENT_LOADER_H_