//
// Created by luoxiaojian on 2021/12/3.
//

#ifndef LIBGRAPE_LITE_VERTEX_MAP_H
#define LIBGRAPE_LITE_VERTEX_MAP_H

#include "grape/utils/id_indexer.h"
#include "grape/worker/comm_spec.h"
#include "grape/vertex_map/vertex_map_base.h"

template <typename OID_T, typename VID_T>
class VertexMap : public grape::VertexMapBase<OID_T, VID_T>{
  using Base = grape::VertexMapBase<OID_T, VID_T>;
 public:
  explicit VertexMap(const grape::CommSpec& comm_spec)
      : Base(comm_spec), is_global_(false), is_synced_(false) {}
  ~VertexMap() {}

  void Init() {
    Base::Init();
    indexers_.resize(Base::GetCommSpec().fnum());
    gid_lists_.resize(Base::GetCommSpec().fnum());
    vertex_sizes_.clear();
    vertex_sizes_.resize(Base::GetCommSpec().fnum(), 0);
  }

  size_t GetTotalVertexSize() {
    return total_vertex_size_;
  }

  size_t GetInnerVertexSize(fid_t fid) {
    return vertex_sizes_[fid];
  }

  void Clear() {}

  void AddVertex(fid_t fid, const OID_T& oid) {
    indexers_[fid]._add(oid);
  }

  bool AddVertex(fid_t fid, const OID_T& oid, VID_T& lid) {
    return indexers_[fid].add(oid, lid);
  }

  bool Index2Oid(fid_t fid, const VID_T& index, OID_T& oid) {
    return indexers_[fid].get_key(index);
  }

  bool Index2Gid(fid_t fid, const VID_T& index, VID_T& gid) {
    if (fid == Base::GetCommSpec().fid() || is_global_) {
      gid = Base::Lid2Gid(fid, index);
      return true;
    } else {
      if (index >= gid_lists_[fid].size()) {
        return false;
      }
      gid = gid_lists_[fid][index];
      return true;
    }
  }

  bool Oid2Gid(fid_t fid, const OID_T& oid, VID_T& gid) {
    VID_T index;
    if (!indexers_[fid].get_index(oid, index)) {
      return false;
    }
    gid = gid_lists_[fid][index];
    return true;
  }

  bool Oid2Index(fid_t fid, const OID_T& oid, VID_T& index) {
    if (indexers_[fid].get_index(oid, index)) {
      index = index | (static_cast<VID_T>(fid) << (Base::fid_offset_));
      return true;
    }
    return false;
  }

  void Construct() {
    const grape::CommSpec& comm_spec = Base::GetCommSpec();
    int worker_id = comm_spec.worker_id();
    int worker_num = comm_spec.worker_num();

    size_t local_size = indexers_[worker_id].size();
    MPI_Allgather(&local_size, 1, MPI_UINT64_T, &vertex_sizes_[0], 1, MPI_UINT64_T, comm_spec.comm());
    CHECK_EQ(local_size, vertex_sizes[worker_id]);

    total_vertex_size_ = 0;
    for (auto s : vertex_sizes_) {
      total_vertex_size_ += s;
    }

    is_synced = is_global_ = (total_vertex_size_ < 100000000);
    if (!is_global_) {
      return ;
    }

    std::thread send_thread([&]() {
      for (int i = 1; i < worker_num; ++i) {
        int dst_worker_id = (i + worker_id) % worker_num;
        indexers_[i].send_to(dst_worker_id, comm_spec.comm(), 0);
      }
    });
    std::thread recv_thread([&]() {
      for (int i = 1; i < worker_num; ++i) {
        int src_worker_id = (worker_id + worker_num - i) % worker_num;
        indexers_[i].recv_from(src_worker_id, comm_spec.comm(), 0);
      }
    });
    send_thread.join();
    recv_thread.join();
  }

  void Sync() {
    if (is_synced_) {
      return ;
    }
    const grape::CommSpec& comm_spec = Base::GetCommSpec();
    int worker_id = comm_spec.worker_id();
    int worker_num = comm_spec.worker_num();
    std::thread send_thread([&]() {
      for (int i = 1; i < worker_num; ++i) {
        int dst_worker_id = (i + worker_id) % worker_num;
        id_encoder_impl::InternalBuffer<OID_T>::send_to(indexers_[dst_worker_id].keys(), dst_worker_id, comm_spec.comm(), 0);
        id_encoder_impl::InternalBuffer<VID_T>::recv_from(gid_lists_[dst_worker_id], dst_worker_id, comm_spec.comm(), 1);
      }
    });
    std::thread recv_thread([&]() {
      id_encoder_impl::InternalBuffer<VID_T>::type parsed_gid_list;
      VID_T gid_frame = static_cast<VID_T>(Base::GetCommSpec().fid()) << Base::GetFidOffset();
      VID_T gid;
      auto& local_indexer = indexers_[Base::GetCommSpec().fid()];
      id_encoder_impl::InternalBuffer<OID_T> oid_list;
      for (int i = 1; i < worker_num; ++i) {
        int src_worker_id = (worker_id + worker_num - i) % worker_num;
        id_encoder_impl::InternalBuffer<OID_T>::recv_from(oid_list, src_worker_id, comm_spec.comm(), 0);
        size_t oid_list_size = oid_list.size();
        parsed_gid_list.clear();
        for (size_t k = 0; k < oid_list_size; ++k) {
          CHECK(local_indexer.get_index(oid_list[k], gid));
          parsed_gid_list.push_back(gid | gid_frame);
        }
        id_encoder_impl::InternalBuffer<VID_T>::send_to(parsed_gid_list, src_worker_id, comm_spec.comm(), 1);
      }
    });
    send_thread.join();
    recv_thread.join();
    is_synced_ = true;
  }

 private:
  std::vector<IdIndexer<OID_T, VID_T>> indexers_;
  std::vector<std::vector<VID_T>> gid_lists_;

  bool is_global_;
  bool is_synced_;

  size_t total_vertex_size_;
  std::vector<size_t> vertex_sizes_;
};

#endif  // LIBGRAPE_LITE_VERTEX_MAP_H
