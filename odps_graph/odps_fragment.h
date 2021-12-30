//
// Created by luoxiaojian on 2021/12/3.
//

#ifndef LIBGRAPE_LITE_ODPS_FRAGMENT_H
#define LIBGRAPE_LITE_ODPS_FRAGMENT_H

#include "grape/fragment/immutable_edgecut_fragment.h"
#include "vertex_map.h"

template <typename OID_T, typename VID_T>
class ODPSFragment : public grape::ImmutableEdgecutFragment<OID_T, VID_T, grape::EmptyType, grape::EmptyType, grape::LoadStrategy::kOnlyOut, VertexMap<OID_T, VID_T>> {
  using Base = grape::ImmutableEdgecutFragment<OID_T, VID_T, grape::EmptyType, grape::EmptyType, grape::LoadStrategy::kOnlyOut, VertexMap<OID_T, VID_T>>;
  using Base::vertex_map_t;
 public:
  ODPSFragment(std::shared_ptr<vertex_map_t> vm_ptr) : Base(vm_ptr) {}

  using Base::fid;

  using Base::fnum;

  // Base::vertex_map

  using Base::id_mask;

  using Base::fid_offset;

  int GetIVNum() const { return static_cast<int>(Base::GetInnerVerticesNum()); }

  int GetOVNum() const { return static_cast<int>(Base::GetOuterVerticesNum()); }

  int64_t GetTVNum() const { return static_cast<int>(Base::GetTotalVerticesNum()); }

  int GetFragmentIVNum(grape::fid_t fid) const { return Base::vm_ptr_->GetInnerVertexSize(fid); }

  int GetOutgoingEdgeNum() const { return static_cast<int>(Base::oenum_); }

  VID_T GetGid(grape::fid_t fid, const OID_T& oid) const {
    VID_T lid;
    if (Base::vm_ptr_->GetLid(fid, oid, lid)) {
      return ((static_cast<VID_T>(fid) << Base::fid_offset_) | lid);
    } else {
      return std::numeric_limits<VID_T>::max();
    }
  }

  VID_T LocalVertexOid2Gid(const OID_T& oid) const {
    return GetGid(Base::fid_, oid);
  }

  int64_t GetOutgoingAdjListBeginAddr(int lid) const {
    return reinterpret_cast<int64_t>(Base::oeoffset_[lid]);
  }

  int64_t GetOutgoingAdjListEndAddr(int lid) const {
    return reinterpret_cast<int64_t>(Base::oeoffset_[lid + 1]);
  }


  ConstBlob GetVertexIdAsConstBlob() const {}
  ConstBlob GetVertexIdOffsetAsConstBlob() const {}

  ConstBlob GetOuterVertexIdAsConstBlob() const {}
  ConstBlob GetOuterVertexIdOffsetAsConstBlob() const {}

  VID_T Lid2Gid(int lid) const { return Base::ovgid_[lid]; }

  void Init(grape::fid_t fid,
            std::vector<std::vector<VID_T>>& src_lid_lists,
            std::vector<std::vector<VID_T>>& dst_gid_lists,
            std::vector<std::vector<int>>& edge_indices) {
    Base::basicInit(fid);

    Base::tvnum_ = Base::ivnum_;

    auto& gid_map = Base::ovg2l_;
    auto& gid_list = Base::ovgid_;
    auto& index_list = ovind_;
    if (Base::vm_ptr_->is_global() || !Base::vm_ptr_->is_synced()) {
      for (auto& vec : dst_gid_lists) {
        for (auto& lid : vec) {
          fid_t cur_fid = (lid >> Base::fid_offset_);
          if (cur_fid != fid) {
            VID_T gid = lid;
            auto& iter = gid_map.find(gid);
            if (iter == gid_map.end()) {
              gid_list.push_back(gid);
              lid = Base::tvnum_++;
              gid_map.emplace(gid, lid);
            }
          } else {
            lid = (lid & Base::id_mask_);
          }
        }
      }
      index_list = gid_list;
    } else {
      for (auto& vec : dst_gid_lists) {
        for (auto& lid : vec) {
          fid_t cur_fid = (lid >> Base::fid_offset_);
          if (cur_fid != fid) {
            VID_T index = lid;
            VID_T gid;
            CHECK(Base::vm_ptr->Index2Gid(cur_fid, index & Base::id_mask_, gid));
            auto iter = gid_map.find(gid);
            if (iter == gid_map.end()) {
              gid_list.push_back(gid);
              lid = Base::tvnum_++;
              gid_map.emplace(gid, lid);
              index_list.push_back(index);
            } else {
              lid = iter->second;
            }
          } else {
            lid = (lid & Base::id_mask_);
          }
        }
      }
    }

    Base::ovnum_ = Base::tvnum_ - Base::ivnum_;

    std::vector<int> odegree(Base::ivnum_, 0);
    for (auto& vec : src_lid_lists) {
      Base::oenum_ += vec.size();
      for (auto lid : vec) {
        ++odegree[lid];
      }
    }

    Base::oe_.resize(Base::oenum_);
    Base::oeoffset_.resize(tvnum_ + 1);
    Base::oeoffset_[0] = &Base::oe_[0];

    for (VID_T i = 0; i < Base::ivnum_; ++i) {
      Base::oeoffset_[i + 1] = Base::oeoffset_[i] + odegree[i];
    }
    for (VID_T i = Base::ivnum_; i < Base::tvnum_; ++i) {
      Base::oeoffset_[i + 1] = Base::oeoffset_[i];
    }

    {
      grape::Array<typename Base::nbr_t*, grape::Allocator<typename Base::nbr_t*>> oeiter(Base::oeoffset_);
      size_t evec_num = src_lid_lists.size();
      CHECK_EQ(evec_num, dst_gid_lists.size());
      edge_indices.clear();
      edge_indices.resize(evec_num);

      for (size_t evec_i = 0; evec_i < evec_num; ++evec_i) {
        auto& src_list = src_lid_lists[evec_i];
        auto& dst_list = dst_gid_lists[evec_i];
        auto& ind_list = edge_indices[evec_i];

        size_t evec_size = src_list.size();
        CHECK_EQ(evec_size, dst_list.size());
        ind_list.resize(evec_size);

        for (size_t e_i = 0; e_i < evec_size; ++e_i) {
          VID_T src_lid = src_list[e_i];
          VID_T dst_lid = dst_list[e_i];
          CHECK_LT(src_lid, Base::ivnum_);
          CHECK_LT(dst_lid, Base::tvnum_);
          int csr_index = oeiter[src_lid] - &oe_[0];
          oeiter[src_lid]->neighbor.SetValue(dst_lid);
          ind_list[e_i] = csr_index;
          ++oeiter[src_lid];
        }
      }
    }
  }

 private:
  std::vector<VID_T> ovind_;

};

#endif  // LIBGRAPE_LITE_ODPS_FRAGMENT_H
