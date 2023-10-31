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

#ifndef GRAPE_VERTEX_MAP_PH_PARTITIONED_VERTEX_MAP_H_
#define GRAPE_VERTEX_MAP_PH_PARTITIONED_VERTEX_MAP_H_

#include "pthash/pthash.hpp"

#include "grape/fragment/partitioner.h"
#include "grape/vertex_map/ph_vertex_map.h"
#include "grape/vertex_map/vertex_map_base.h"

namespace grape {

template <typename OID_T>
class SimplePartitioner : std::hash<OID_T> {
  using internal_oid_t = typename InternalOID<OID_T>::type;

 public:
  SimplePartitioner() : fnum_(0) {}

  void init_hash_partitioner(fid_t fnum) {
    fnum_ = fnum;
    oid_offsets_.clear();
  }

  void init_segmented_partitioner(const std::vector<OID_T>& oid_offsets) {
    fnum_ = 0;
    oid_offsets_ = oid_offsets;
  }

  fid_t get_partition_id(const internal_oid_t& oid) const {
    if (fnum_) {
#if 0
      return MurmurHash2_64(reinterpret_cast<char const*>(&oid), sizeof(oid),
                            0) %
             fnum_;
#else
      return (*this)(oid) % fnum_;
#endif
    } else if (!oid_offsets_.empty()) {
      auto iter =
          std::upper_bound(oid_offsets_.begin(), oid_offsets_.end(), oid);
      if (iter == oid_offsets_.end()) {
        return oid_offsets_.size();
      } else {
        return iter - oid_offsets_.begin();
      }
    }
    return -1;
  }

  bool valid() const { return (fnum_ > 0) || (!oid_offsets_.empty()); }

  void reset() {
    fnum_ = 0;
    oid_offsets_.clear();
  }

  size_t memory_usage() const {
    return sizeof(fnum_) + oid_offsets_.capacity() * sizeof(OID_T);
  }

  template <typename IOADAPTOR_T>
  void serialize(std::unique_ptr<IOADAPTOR_T>& writer) {
    CHECK(writer->Write(&fnum_, sizeof(fid_t)));
    size_t size = oid_offsets_.size();
    CHECK(writer->Write(&size, sizeof(size_t)));
    if (size != 0) {
      CHECK(writer->Write(oid_offsets_.data(), size * sizeof(OID_T)));
    }
  }

  template <typename IOADAPTOR_T>
  void deserialize(std::unique_ptr<IOADAPTOR_T>& reader) {
    CHECK(reader->Read(&fnum_, sizeof(fid_t)));
    size_t size;
    CHECK(reader->Read(&size, sizeof(size_t)));
    oid_offsets_.clear();
    if (size != 0) {
      oid_offsets_.resize(size);
      CHECK(reader->Read(oid_offsets_.data(), size * sizeof(OID_T)));
    }
  }

 private:
  fid_t fnum_;
  std::vector<OID_T> oid_offsets_;
};

template <>
class SimplePartitioner<std::string> : std::hash<std::string_view> {
 public:
  SimplePartitioner() : fnum_(0) {}

  void init_hash_partitioner(fid_t fnum) {
    fnum_ = fnum;
    oid_offsets_.clear();
  }

  void init_segmented_partitioner(const std::vector<std::string>& oid_offsets) {
    fnum_ = 0;
    oid_offsets_ = oid_offsets;
  }

  fid_t get_partition_id(const string_view& oid) const {
    if (fnum_) {
      return (*this)(oid) % fnum_;
    } else if (!oid_offsets_.empty()) {
      auto iter =
          std::upper_bound(oid_offsets_.begin(), oid_offsets_.end(), oid);
      if (iter == oid_offsets_.end()) {
        return oid_offsets_.size();
      } else {
        return iter - oid_offsets_.begin();
      }
    }
    return -1;
  }

  bool valid() const { return (fnum_ > 0) || (!oid_offsets_.empty()); }

  void reset() {
    fnum_ = 0;
    oid_offsets_.clear();
  }

  size_t memory_usage() const {
    size_t ret = sizeof(fnum_) + oid_offsets_.capacity() * sizeof(std::string);
    for (auto& s : oid_offsets_) {
      ret += s.capacity();
    }
    return ret;
  }

  template <typename IOADAPTOR_T>
  void serialize(std::unique_ptr<IOADAPTOR_T>& writer) {
    CHECK(writer->Write(&fnum_, sizeof(fid_t)));
    size_t size = oid_offsets_.size();
    CHECK(writer->Write(&size, sizeof(size_t)));
    for (auto& s : oid_offsets_) {
      size = s.size();
      CHECK(writer->Write(&size, sizeof(size_t)));
      if (size != 0) {
        CHECK(writer->Write(s.data(), size));
      }
    }
  }

  template <typename IOADAPTOR_T>
  void deserialize(std::unique_ptr<IOADAPTOR_T>& reader) {
    CHECK(reader->Read(&fnum_, sizeof(fid_t)));
    size_t size;
    CHECK(reader->Read(&size, sizeof(size_t)));
    oid_offsets_.resize(size);
    for (auto& s : oid_offsets_) {
      CHECK(reader->Read(&size, sizeof(size_t)));
      s.resize(size);
      if (size != 0) {
        CHECK(reader->Read(s.data(), size));
      }
    }
  }

 private:
  fid_t fnum_;
  std::vector<std::string> oid_offsets_;
};

class mem_buffer_saver {
 public:
  mem_buffer_saver() = default;
  ~mem_buffer_saver() = default;

  template <typename T>
  void visit(T& val) {
    if constexpr (std::is_pod<T>::value) {
      char* ptr = reinterpret_cast<char*>(&val);
      buf_.insert(buf_.end(), ptr, ptr + sizeof(T));
    } else {
      val.visit(*this);
    }
  }

  template <typename T, typename Allocator>
  void visit(std::vector<T, Allocator>& vec) {
    if constexpr (std::is_pod<T>::value) {
      size_t n = vec.size();
      visit(n);
      char* ptr = reinterpret_cast<char*>(vec.data());
      buf_.insert(buf_.end(), ptr, ptr + sizeof(T) * n);
    } else {
      size_t n = vec.size();
      visit(n);
      for (auto& v : vec)
        visit(v);
    }
  }

  std::vector<char>& buffer() { return buf_; }

 private:
  std::vector<char> buf_;
};

class mem_buffer_loader {
 public:
  mem_buffer_loader(const char* ptr) : ptr_(ptr) {}
  ~mem_buffer_loader() = default;

  template <typename T>
  void visit(T& val) {
    if constexpr (std::is_pod<T>::value) {
      char* p = reinterpret_cast<char*>(&val);
      memcpy(p, ptr_, sizeof(T));
      ptr_ += sizeof(T);
    } else {
      val.visit(*this);
    }
  }

  template <typename T, typename Allocator>
  void visit(std::vector<T, Allocator>& vec) {
    size_t n;
    visit(n);
    vec.resize(n);
    if constexpr (std::is_pod<T>::value) {
      char* p = reinterpret_cast<char*>(vec.data());
      memcpy(p, ptr_, sizeof(T) * n);
      ptr_ += sizeof(T) * n;
    } else {
      for (auto& v : vec)
        visit(v);
    }
  }

 private:
  const char* ptr_;
};

template <typename PH_T>
static void AllGatherPH(const CommSpec& comm_spec, std::vector<PH_T>& ph_list) {
  int worker_id = comm_spec.worker_id();
  int worker_num = comm_spec.worker_num();
  fid_t fnum = comm_spec.fnum();
  std::thread send_thread([&]() {
    mem_buffer_saver saver;
    saver.visit(ph_list[comm_spec.fid()]);

    int dst_worker_id = (worker_id + worker_num - 1) % worker_num;
    while (dst_worker_id != worker_id) {
      for (fid_t fid = 0; fid < fnum; ++fid) {
        if (comm_spec.FragToWorker(fid) != worker_id) {
          continue;
        }
        sync_comm::Send(saver.buffer(), dst_worker_id, 0, comm_spec.comm());
      }
      dst_worker_id = (dst_worker_id + worker_num - 1) % worker_num;
    }
  });
  std::thread recv_thread([&]() {
    int src_worker_id = (worker_id + 1) % worker_num;
    while (src_worker_id != worker_id) {
      for (fid_t fid = 0; fid < fnum; ++fid) {
        if (comm_spec.FragToWorker(fid) != src_worker_id) {
          continue;
        }
        std::vector<char> buffer;
        sync_comm::Recv(buffer, src_worker_id, 0, comm_spec.comm());
        mem_buffer_loader loader(buffer.data());
        loader.visit(ph_list[fid]);
      }
      src_worker_id = (src_worker_id + 1) % worker_num;
    }
  });
  send_thread.join();
  recv_thread.join();
}

template <typename OID_T>
struct PPHArrayHelper {
 public:
  static void all_gather(const CommSpec& comm_spec,
                         std::vector<mmap_array<OID_T>>& arrays) {
    int worker_id = comm_spec.worker_id();
    int worker_num = comm_spec.worker_num();
    fid_t fnum = comm_spec.fnum();
    std::thread send_thread([&]() {
      int dst_worker_id = (worker_id + worker_num - 1) % worker_num;
      while (dst_worker_id != worker_id) {
        for (fid_t fid = 0; fid < fnum; ++fid) {
          if (comm_spec.FragToWorker(fid) != worker_id) {
            continue;
          }
          size_t buffer_size = arrays[fid].size();

          MPI_Send(&buffer_size, sizeof(size_t), MPI_CHAR, dst_worker_id, 0,
                   comm_spec.comm());
          sync_comm::send_buffer(arrays[fid].data(), buffer_size, dst_worker_id,
                                 0, comm_spec.comm());
        }
        dst_worker_id = (dst_worker_id + worker_num - 1) % worker_num;
      }
    });
    std::thread recv_thread([&]() {
      int src_worker_id = (worker_id + 1) % worker_num;
      while (src_worker_id != worker_id) {
        for (fid_t fid = 0; fid < fnum; ++fid) {
          if (comm_spec.FragToWorker(fid) != src_worker_id) {
            continue;
          }
          size_t buffer_size;

          MPI_Recv(&buffer_size, sizeof(size_t), MPI_CHAR, src_worker_id, 0,
                   comm_spec.comm(), MPI_STATUS_IGNORE);
          arrays[fid].resize(buffer_size);
          sync_comm::recv_buffer(arrays[fid].data(), buffer_size, src_worker_id,
                                 0, comm_spec.comm());
        }
        src_worker_id = (src_worker_id + 1) % worker_num;
      }
    });
    send_thread.join();
    recv_thread.join();
  }
};

template <>
struct PPHArrayHelper<string_view> {
 public:
  static void all_gather(const CommSpec& comm_spec,
                         std::vector<mmap_array<string_view>>& arrays) {
    int worker_id = comm_spec.worker_id();
    fid_t fnum = comm_spec.fnum();
    int worker_num = comm_spec.worker_num();
    std::thread send_thread([&]() {
      int dst_worker_id = (worker_id + worker_num - 1) % worker_num;
      while (dst_worker_id != worker_id) {
        for (fid_t fid = 0; fid < fnum; ++fid) {
          if (comm_spec.FragToWorker(fid) != worker_id) {
            continue;
          }
          size_t buffers_size[4];
          buffers_size[0] = arrays[fid].items_array().size();
          buffers_size[1] = arrays[fid].data_array().size();
          buffers_size[2] = arrays[fid].max_length();
          buffers_size[3] = arrays[fid].data_size();

          MPI_Send(buffers_size, 4 * sizeof(size_t), MPI_CHAR, dst_worker_id, 0,
                   comm_spec.comm());
          sync_comm::send_buffer(arrays[fid].items_array().data(),
                                 arrays[fid].items_array().size(),
                                 dst_worker_id, 0, comm_spec.comm());
          sync_comm::send_buffer(arrays[fid].data_array().data(),
                                 arrays[fid].data_array().size(), dst_worker_id,
                                 0, comm_spec.comm());
        }
        dst_worker_id = (dst_worker_id + worker_num - 1) % worker_num;
      }
    });
    std::thread recv_thread([&]() {
      int src_worker_id = (worker_id + 1) % worker_num;
      while (src_worker_id != worker_id) {
        for (fid_t fid = 0; fid < fnum; ++fid) {
          if (comm_spec.FragToWorker(fid) != src_worker_id) {
            continue;
          }
          size_t buffers_size[4];

          MPI_Recv(buffers_size, 4 * sizeof(size_t), MPI_CHAR, src_worker_id, 0,
                   comm_spec.comm(), MPI_STATUS_IGNORE);
          arrays[fid].set_max_length(buffers_size[2]);
          arrays[fid].items_array().resize(buffers_size[0]);
          arrays[fid].data_array().resize(buffers_size[1]);
          arrays[fid].set_data_size(buffers_size[3]);
          sync_comm::recv_buffer(arrays[fid].items_array().data(),
                                 arrays[fid].items_array().size(),
                                 src_worker_id, 0, comm_spec.comm());
          sync_comm::recv_buffer(arrays[fid].data_array().data(),
                                 arrays[fid].data_array().size(), src_worker_id,
                                 0, comm_spec.comm());
        }
        src_worker_id = (src_worker_id + 1) % worker_num;
      }
    });
    send_thread.join();
    recv_thread.join();
  }
};

template <typename OID_T, typename VID_T>
class PHPartitionedVertexMap
    : public VertexMapBase<OID_T, VID_T, HashPartitioner<OID_T>> {
  using base_t = VertexMapBase<OID_T, VID_T, HashPartitioner<OID_T>>;
  using internal_oid_t = typename InternalOID<OID_T>::type;

 public:
  explicit PHPartitionedVertexMap(const CommSpec& comm_spec)
      : base_t(comm_spec) {}
  ~PHPartitionedVertexMap() = default;

  using base_t::comm_spec_;
  void InitHash(const std::vector<OID_T>& oid_list,
                int thread_num = std::thread::hardware_concurrency()) {
    spartitioner_.init_hash_partitioner(comm_spec_.fnum());
    if (comm_spec_.fnum() == 1) {
      initImpl(oid_list.data(), oid_list.size(), thread_num);
    } else {
      double t = -GetCurrentTime();
      std::vector<OID_T> local_list;
      for (auto& oid : oid_list) {
        if (spartitioner_.get_partition_id(oid) == comm_spec_.fid()) {
          local_list.push_back(oid);
        }
      }
      t += GetCurrentTime();
      LOG(INFO) << "hash preprocess: " << t << " s";
      initImpl(local_list.data(), local_list.size(), thread_num);
    }
  }

  void InitSegmented(const std::vector<OID_T>& oid_list,
                     int thread_num = std::thread::hardware_concurrency()) {
    if (comm_spec_.fnum() == 1) {
      InitHash(oid_list, thread_num);
      return;
    }
    double t = -GetCurrentTime();
    size_t vnum = oid_list.size();
    size_t chunk = (vnum + comm_spec_.fnum() - 1) / comm_spec_.fnum();
    std::vector<OID_T> spliters;
    for (fid_t k = 1; k != comm_spec_.fnum(); ++k) {
      spliters.push_back(oid_list[k * chunk]);
    }
    spartitioner_.init_segmented_partitioner(spliters);

    size_t local_begin = std::min(chunk * comm_spec_.fid(), vnum);
    size_t local_end = std::min(local_begin + chunk, vnum);
    size_t local_num = local_end - local_begin;
    t += GetCurrentTime();
    LOG(INFO) << "segmented preprocess: " << t << " s";

    initImpl(oid_list.data() + local_begin, local_num, thread_num);
  }

  void InitPartitioned(const std::vector<std::vector<OID_T>>& oid_lists,
                       int thread_num = std::thread::hardware_concurrency()) {
    spartitioner_.reset();
    initImpl(oid_lists[comm_spec_.fid()].data(),
             oid_lists[comm_spec_.fid()].size(), thread_num);
  }

  void AddVertex(const OID_T& oid) override { LOG(FATAL) << "not support"; }

  bool AddVertex(const OID_T& oid, VID_T& gid) override {
    LOG(FATAL) << "not support";
    return false;
  }

  void UpdateToBalance(std::vector<VID_T>& vnum_list,
                       std::vector<std::vector<VID_T>>& gid_maps) override {
    LOG(FATAL) << "not support";
  }

  size_t GetTotalVertexSize() const {
    size_t ret = 0;
    for (auto& list : oid_lists_) {
      ret += list.size();
    }
    return ret;
  }
  size_t GetInnerVertexSize(fid_t fid) const { return oid_lists_[fid].size(); }

  using base_t::GetFidFromGid;
  using base_t::GetLidFromGid;
  bool GetOid(const VID_T& gid, OID_T& oid) const {
    fid_t fid = GetFidFromGid(gid);
    VID_T lid = GetLidFromGid(gid);
    return GetOid(fid, lid, oid);
  }

  bool GetOid(fid_t fid, const VID_T& lid, OID_T& oid) const {
    if (lid < oid_lists_[fid].size()) {
      oid = oid_lists_[fid].get(lid);
      return true;
    }
    return false;
  }

  using base_t::Lid2Gid;
  bool _GetGid(fid_t fid, const internal_oid_t& oid, VID_T& gid) const {
    size_t index = flist_[fid](oid);
    if (oid_lists_[fid].get(index) == oid) {
      gid = Lid2Gid(fid, index);
      return true;
    }
    return false;
  }

  bool GetGid(fid_t fid, const OID_T& oid, VID_T& gid) const {
    internal_oid_t internal_oid(oid);
    return _GetGid(fid, internal_oid, gid);
  }

  bool _GetGid(const internal_oid_t& oid, VID_T& gid) const {
    fid_t fid = spartitioner_.get_partition_id(oid);
    if (fid >= 0) {
      return _GetGid(fid, oid, gid);
    } else {
      for (fid_t k = 0; k < comm_spec_.fnum(); ++k) {
        if (_GetGid(k, oid, gid)) {
          return true;
        }
      }
      return false;
    }
  }

  bool GetGid(const OID_T& oid, VID_T& gid) const {
    return _GetGid(internal_oid_t(oid), gid);
  }

  size_t memory_usage() const {
    size_t ret = 0;
    for (auto& f : flist_) {
      ret += f.num_bits();
    }
    ret = ret / 8;
    for (auto& l : oid_lists_) {
      ret += l.memory_usage();
    }
    ret += spartitioner_.memory_usage();
    return ret;
  }

  void initImpl(const OID_T* oid_list, size_t num, int thread_num) {
    double t0 = -GetCurrentTime();
    pthash::build_configuration config;
    config.c = 7.0;
    config.alpha = 0.94;
    if (num > 121242388) {
      config.num_threads = std::min(thread_num, 32);
    } else {
      config.num_threads = std::min(thread_num, 16);
    }
    LOG(INFO) << "build thread_num: " << config.num_threads;
    // config.num_threads = thread_num;
    config.minimal_output = true;
    config.verbose_output = false;

    flist_.resize(comm_spec_.fnum());
    flist_[comm_spec_.fid()].build_in_internal_memory(oid_list, num, config);
    t0 += GetCurrentTime();

    double t1 = -GetCurrentTime();
    oid_lists_.clear();
    oid_lists_.resize(comm_spec_.fnum());
    PHArrayHelper<internal_oid_t>::assign_array(
        oid_list, num, oid_lists_[comm_spec_.fid()], flist_[comm_spec_.fid()],
        thread_num);
    t1 += GetCurrentTime();
    double t2 = -GetCurrentTime();
    AllGatherPH(comm_spec_, flist_);
    t2 += GetCurrentTime();
    double t3 = -GetCurrentTime();
    PPHArrayHelper<internal_oid_t>::all_gather(comm_spec_, oid_lists_);
    t3 += GetCurrentTime();
    LOG(INFO) << "build: " << t0 << " s, assign: " << t1
              << " s, allgather functions: " << t2
              << " s, allgather oid: " << t3 << " s";
  }

  void Serialize(const std::string& prefix) {
    if (comm_spec_.fid() == 0) {
      {
        auto io_adaptor = std::unique_ptr<LocalIOAdaptor>(
            new LocalIOAdaptor(prefix + "/vm.base"));
        io_adaptor->Open("wb");
        base_t::serialize(io_adaptor);
        spartitioner_.serialize(io_adaptor);
      }
    }
    MPI_Barrier(comm_spec_.comm());
    if (!exists_file(prefix + "/vm.base")) {
      {
        auto io_adaptor = std::unique_ptr<LocalIOAdaptor>(
            new LocalIOAdaptor(prefix + "/vm.base"));
        io_adaptor->Open("rb");
        base_t::serialize(io_adaptor);
        spartitioner_.serialize(io_adaptor);
      }
    }
    essentials::save(
        flist_[comm_spec_.fid()],
        (prefix + "/vm.flist_" + std::to_string(comm_spec_.fid())).c_str());
    oid_lists_[comm_spec_.fid()].save(prefix + "/vm.oid_list_" +
                                      std::to_string(comm_spec_.fid()));
  }

  void Deserialize(const std::string& prefix, fid_t fid) {
    {
      auto io_adaptor = std::unique_ptr<LocalIOAdaptor>(
          new LocalIOAdaptor(prefix + "/vm.base"));
      io_adaptor->Open("rb");
      base_t::deserialize(io_adaptor);
      spartitioner_.deserialize(io_adaptor);
    }
    flist_.resize(comm_spec_.fnum());
    oid_lists_.resize(comm_spec_.fnum());
    essentials::load(
        flist_[comm_spec_.fid()],
        (prefix + "/vm.flist_" + std::to_string(comm_spec_.fid())).c_str());
    oid_lists_[comm_spec_.fid()].open(
        prefix + "/vm.oid_list_" + std::to_string(comm_spec_.fid()), true);
    AllGatherPH(comm_spec_, flist_);
    PPHArrayHelper<internal_oid_t>::all_gather(comm_spec_, oid_lists_);
  }

 private:
  typedef pthash::single_phf<murmurhash2_64, pthash::dictionary_dictionary,
                             true>
      pthash_type;
  std::vector<pthash_type> flist_;
  std::vector<mmap_array<internal_oid_t>> oid_lists_;

  SimplePartitioner<OID_T> spartitioner_;
};

}  // namespace grape

#endif  // GRAPE_VERTEX_MAP_PH_PARTITIONED_VERTEX_MAP_H_