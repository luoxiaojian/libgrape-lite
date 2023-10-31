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

#ifndef GRAPE_VERTEX_MAP_PH_VERTEX_MAP_H_
#define GRAPE_VERTEX_MAP_PH_VERTEX_MAP_H_

#include "pthash/pthash.hpp"

#include "grape/fragment/partitioner.h"
#include "grape/utils/mmap_array.h"
#include "grape/vertex_map/vertex_map_base.h"

namespace grape {

/*
    This code is an adaptation from
    https://github.com/aappleby/smhasher/blob/master/src/MurmurHash2.cpp
        by Austin Appleby
*/
static uint64_t MurmurHash2_64(void const* key, size_t len, uint64_t seed) {
  const uint64_t m = 0xc6a4a7935bd1e995ULL;
  const int r = 47;

  uint64_t h = seed ^ (len * m);

#if defined(__arm) || defined(__arm__)
  const size_t ksize = sizeof(uint64_t);
  const unsigned char* data = (const unsigned char*) key;
  const unsigned char* end = data + (std::size_t)(len / 8) * ksize;
#else
  const uint64_t* data = (const uint64_t*) key;
  const uint64_t* end = data + (len / 8);
#endif

  while (data != end) {
#if defined(__arm) || defined(__arm__)
    uint64_t k;
    memcpy(&k, data, ksize);
    data += ksize;
#else
    uint64_t k = *data++;
#endif

    k *= m;
    k ^= k >> r;
    k *= m;

    h ^= k;
    h *= m;
  }

  const unsigned char* data2 = (const unsigned char*) data;

  switch (len & 7) {
  // fall through
  case 7:
    h ^= uint64_t(data2[6]) << 48;
  // fall through
  case 6:
    h ^= uint64_t(data2[5]) << 40;
  // fall through
  case 5:
    h ^= uint64_t(data2[4]) << 32;
  // fall through
  case 4:
    h ^= uint64_t(data2[3]) << 24;
  // fall through
  case 3:
    h ^= uint64_t(data2[2]) << 16;
  // fall through
  case 2:
    h ^= uint64_t(data2[1]) << 8;
  // fall through
  case 1:
    h ^= uint64_t(data2[0]);
    h *= m;
  };

  h ^= h >> r;
  h *= m;
  h ^= h >> r;

  return h;
}

struct murmurhash2_64 {
  typedef pthash::hash64 hash_type;

  // specialization for std::string
  static inline hash_type hash(std::string const& val, uint64_t seed) {
    return MurmurHash2_64(val.data(), val.size(), seed);
  }

  // specialization for uint64_t
  static inline hash_type hash(uint64_t val, uint64_t seed) {
    return MurmurHash2_64(reinterpret_cast<char const*>(&val), sizeof(val),
                          seed);
  }

  // specialization for std::string
  static inline hash_type hash(string_view const& val, uint64_t seed) {
    return MurmurHash2_64(val.data(), val.size(), seed);
  }
};

template <typename OID_T>
struct PHArrayHelper {
 public:
  template <typename FUNC_T>
  static void assign_array(const OID_T* oid_list, size_t num,
                           mmap_array<OID_T>& oid_array, const FUNC_T& func,
                           int thread_num) {
    oid_array.reset();
    oid_array.resize(num);
    if (thread_num == 1) {
      for (size_t i = 0; i < num; ++i) {
        oid_array.set(func(oid_list[i]), oid_list[i]);
      }
    } else {
      std::vector<std::thread> threads;
      const size_t chunk_size = 4096;
      std::atomic<size_t> offset(0);
      for (int i = 0; i < thread_num; ++i) {
        threads.emplace_back([&]() {
          while (true) {
            size_t begin = offset.fetch_add(chunk_size);
            if (begin >= num) {
              break;
            }
            size_t end = std::min(begin + chunk_size, num);
            while (begin < end) {
              oid_array.set(func(oid_list[begin]), oid_list[begin]);
              ++begin;
            }
          }
        });
      }
      for (auto& thrd : threads) {
        thrd.join();
      }
    }
  }
};

template <>
struct PHArrayHelper<string_view> {
 public:
  template <typename FUNC_T>
  static void assign_array(const std::string* oid_list, size_t num,
                           mmap_array<string_view>& oid_array,
                           const FUNC_T& func, int thread_num) {
    if (thread_num == 1) {
      size_t total_length(0);
      for (size_t i = 0; i < num; ++i) {
        total_length += oid_list[i].length();
      }
      size_t max_length = (total_length + num - 1) / num;
      oid_array.reset();
      oid_array.set_max_length(max_length);
      oid_array._resize(num, total_length);
      for (size_t i = 0; i < num; ++i) {
        oid_array.set(func(oid_list[i]), oid_list[i]);
      }
    } else {
      std::vector<size_t> thread_length(thread_num, 0);
      size_t chunk = (num + thread_num - 1) / thread_num;
      {
        std::vector<std::thread> threads;
        for (int i = 0; i < thread_num; ++i) {
          threads.emplace_back(
              [&](int tid) {
                size_t local_length = 0;
                size_t begin = std::min(tid * chunk, num);
                size_t end = std::min(begin + chunk, num);

                for (size_t k = begin; k < end; ++k) {
                  local_length += oid_list[k].length();
                }

                thread_length[tid] = local_length;
              },
              i);
        }
        for (auto& thrd : threads) {
          thrd.join();
        }
      }
      size_t total_length = 0;
      std::vector<size_t> thread_offsets;
      for (int i = 0; i < thread_num; ++i) {
        thread_offsets.push_back(total_length);
        total_length += thread_length[i];
      }

      size_t max_length = (total_length + num - 1) / num;
      oid_array.reset();
      oid_array.set_max_length(max_length);
      oid_array._resize(num, total_length);

      {
        std::vector<std::thread> threads;
        for (int i = 0; i < thread_num; ++i) {
          threads.emplace_back(
              [&](int tid) {
                size_t local_offset = thread_offsets[tid];
                size_t begin = std::min(tid * chunk, num);
                size_t end = std::min(begin + chunk, num);

                for (size_t k = begin; k < end; ++k) {
                  string_view str(oid_list[k]);
                  oid_array._set(func(str), local_offset, str);
                  local_offset += str.length();
                }
              },
              i);
        }
        for (auto& thrd : threads) {
          thrd.join();
        }
      }
    }
  }
};

template <typename OID_T, typename VID_T>
class PHVertexMap : public VertexMapBase<OID_T, VID_T, HashPartitioner<OID_T>> {
  using base_t = VertexMapBase<OID_T, VID_T, HashPartitioner<OID_T>>;
  using internal_oid_t = typename InternalOID<OID_T>::type;

 public:
  explicit PHVertexMap(const CommSpec& comm_spec) : base_t(comm_spec) {}
  ~PHVertexMap() = default;

  using base_t::comm_spec_;
  void Init(const std::vector<OID_T>& oid_list,
            int thread_num = std::thread::hardware_concurrency()) {
    double t0 = -GetCurrentTime();
    pthash::build_configuration config;
    config.c = 7.0;
    config.alpha = 0.94;
    if (oid_list.size() > 121242388) {
      config.num_threads = std::min(thread_num, 32);
    } else {
      config.num_threads = std::min(thread_num, 16);
    }
    LOG(INFO) << "build thread_num: " << config.num_threads;
    config.minimal_output = true;
    config.verbose_output = false;
    f_.build_in_internal_memory(oid_list.begin(), oid_list.size(), config);
    t0 += GetCurrentTime();

    double t1 = -GetCurrentTime();
    PHArrayHelper<internal_oid_t>::assign_array(
        oid_list.data(), oid_list.size(), oid_list_, f_, thread_num);
    t1 += GetCurrentTime();
    LOG(INFO) << "build functions: " << t0 << " s, build array: " << t1 << " s";

    chunk_size_ =
        (oid_list_.size() + comm_spec_.fnum() - 1) / comm_spec_.fnum();
    last_chunk_size_ = oid_list_.size() - (comm_spec_.fnum() - 1) * chunk_size_;
    for (fid_t fid = 0; fid < comm_spec_.fnum(); ++fid) {
      chunk_offsets_.push_back(fid * chunk_size_);
    }
    chunk_offsets_.push_back(oid_list_.size());
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

  size_t GetTotalVertexSize() const { return oid_list_.size(); }
  size_t GetInnerVertexSize(fid_t fid) const {
    return chunk_offsets_[fid + 1] - chunk_offsets_[fid];
  }

  using base_t::GetFidFromGid;
  using base_t::GetLidFromGid;
  bool GetOid(const VID_T& gid, OID_T& oid) const {
    fid_t fid = GetFidFromGid(gid);
    VID_T lid = GetLidFromGid(gid);
    return GetOid(fid, lid, oid);
  }

  bool GetOid(fid_t fid, const VID_T& lid, OID_T& oid) const {
    size_t index = chunk_offsets_[fid] + lid;
    if (index < chunk_offsets_[fid + 1]) {
      oid = oid_list_.get(index);
      return true;
    }
    return false;
  }

  using base_t::Lid2Gid;
  bool _GetGid(fid_t fid, const internal_oid_t& oid, VID_T& gid) const {
    size_t index = f_(oid);
    if (index < chunk_offsets_[fid + 1] && index >= chunk_offsets_[fid] &&
        oid_list_.get(index) == oid) {
      gid = Lid2Gid(fid, index - chunk_offsets_[fid]);
      return true;
    }
    return false;
  }

  bool GetGid(fid_t fid, const OID_T& oid, VID_T& gid) const {
    internal_oid_t internal_oid(oid);
    return _GetGid(fid, internal_oid, gid);
  }

  bool _GetGid(const internal_oid_t& oid, VID_T& gid) const {
    size_t index = f_(oid);
    fid_t fid;
    VID_T lid;
    if (index < oid_list_.size() && oid_list_.get(index) == oid) {
      index_to_fid_lid(index, fid, lid);
      gid = Lid2Gid(fid, lid);
      return true;
    }
    return false;
  }

  bool GetGid(const OID_T& oid, VID_T& gid) const {
    return _GetGid(internal_oid_t(oid), gid);
  }

  size_t memory_usage() const {
    size_t ret = f_.num_bits() / 8;
    ret += oid_list_.memory_usage();
    ret += chunk_offsets_.capacity() * sizeof(VID_T);
    ret += sizeof(VID_T) * 2;
    return ret;
  }

  void Serialize(const std::string& prefix) {
    if (comm_spec_.fid() == 0) {
      {
        auto io_adaptor = std::unique_ptr<LocalIOAdaptor>(
            new LocalIOAdaptor(prefix + "/vm.base"));
        io_adaptor->Open("wb");
        base_t::serialize(io_adaptor);
      }
      essentials::save(f_, (prefix + "/vm.f").c_str());
      oid_list_.save(prefix + "/vm.oid_list");
      FILE* fout = fopen((prefix + "/vm.chunk").c_str(), "wb");
      fwrite(&chunk_size_, sizeof(VID_T), 1, fout);
      fwrite(&last_chunk_size_, sizeof(VID_T), 1, fout);
      size_t chunk_offsets_size = chunk_offsets_.size();
      fwrite(&chunk_offsets_size, sizeof(size_t), 1, fout);
      fwrite(chunk_offsets_.data(), sizeof(VID_T), chunk_offsets_.size(), fout);
      fflush(fout);
      fclose(fout);
    }
    MPI_Barrier(comm_spec_.comm());
    if (exists_file(prefix + "/vm.base")) {
      return;
    } else {
      {
        auto io_adaptor = std::unique_ptr<LocalIOAdaptor>(
            new LocalIOAdaptor(prefix + "/vm.base"));
        io_adaptor->Open("wb");
        base_t::serialize(io_adaptor);
      }
      essentials::save(f_, (prefix + "/vm.f").c_str());
      oid_list_.save(prefix + "/vm.oid_list");
      FILE* fout = fopen((prefix + "/vm.chunk").c_str(), "wb");
      CHECK_EQ(fwrite(&chunk_size_, sizeof(VID_T), 1, fout), 1);
      CHECK_EQ(fwrite(&last_chunk_size_, sizeof(VID_T), 1, fout), 1);
      size_t chunk_offsets_size = chunk_offsets_.size();
      CHECK_EQ(fwrite(&chunk_offsets_size, sizeof(size_t), 1, fout), 1);
      CHECK_EQ(fwrite(chunk_offsets_.data(), sizeof(VID_T),
                      chunk_offsets_.size(), fout),
               chunk_offsets_size);
      fflush(fout);
      fclose(fout);
    }
  }

  void Deserialize(const std::string& prefix, fid_t fid) {
    {
      auto io_adaptor = std::unique_ptr<LocalIOAdaptor>(
          new LocalIOAdaptor(prefix + "/vm.base"));
      io_adaptor->Open();
      base_t::deserialize(io_adaptor);
    }
    essentials::load(f_, (prefix + "/vm.f").c_str());
    oid_list_.open(prefix + "/vm.oid_list", true);
    FILE* fin = fopen((prefix + "/vm.chunk").c_str(), "rb");
    CHECK_EQ(fread(&chunk_size_, sizeof(VID_T), 1, fin), 1);
    CHECK_EQ(fread(&last_chunk_size_, sizeof(VID_T), 1, fin), 1);
    size_t chunk_offsets_size;
    CHECK_EQ(fread(&chunk_offsets_size, sizeof(size_t), 1, fin), 1);
    chunk_offsets_.resize(chunk_offsets_size);
    CHECK_EQ(
        fread(chunk_offsets_.data(), sizeof(VID_T), chunk_offsets_size, fin),
        chunk_offsets_size);
    fclose(fin);
  }

 private:
  void index_to_fid_lid(VID_T index, fid_t& fid, VID_T& lid) const {
    if (chunk_size_ == 0) {
      auto iter =
          std::lower_bound(chunk_offsets_.begin(), chunk_offsets_.end(), index);
      fid = iter - chunk_offsets_.begin() - 1;
      lid = index - chunk_offsets_[fid];
    } else {
      fid = index / chunk_size_;
      lid = index % chunk_size_;
    }
  }

  typedef pthash::single_phf<murmurhash2_64, pthash::dictionary_dictionary,
                             true>
      pthash_type;
  pthash_type f_;
  mmap_array<internal_oid_t> oid_list_;

  VID_T chunk_size_;
  VID_T last_chunk_size_;

  std::vector<VID_T> chunk_offsets_;
};

}  // namespace grape

#endif  // GRAPE_VERTEX_MAP_GLOBAL_PH_VERTEX_MAP_H_