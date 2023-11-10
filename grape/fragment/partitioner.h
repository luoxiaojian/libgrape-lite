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

#ifndef GRAPE_FRAGMENT_PARTITIONER_H_
#define GRAPE_FRAGMENT_PARTITIONER_H_

#include <vector>

#include "grape/config.h"
#include "grape/types.h"
#include "murmurhash/murmurhash.h"

namespace grape {

#define USE_MURMUR_HASH

static constexpr fid_t INVALID_PARTITION_ID = std::numeric_limits<fid_t>::max();

enum class PartitionStrategy { kHash, kSegmented };

template <typename OID_T>
class Partitioner {
 public:
  using oid_t = OID_T;

  Partitioner() : fnum_(0) {}
  ~Partitioner() = default;

  void InitHashPartitioner(size_t frag_num, uint64_t seed = 0x11151115) {
    fnum_ = frag_num;
    spliters_.clear();
    seed_ = seed;
  }

  void InitSegmentedPartitioner(const std::vector<OID_T>& spliters) {
    spliters_ = spliters;
    if (spliters_.empty()) {
      fnum_ = 1;
    } else {
      fnum_ = 0;
    }
  }

  fid_t GetPartitionId(const OID_T& oid) const {
    if (fnum_) {
#ifdef USE_MURMUR_HASH
      return static_cast<fid_t>(
          MurmurHash2_64(reinterpret_cast<const char*>(&oid), sizeof(OID_T),
                         seed_) %
          fnum_);
#else
      return hash_func_(oid) % fnum_;
#endif
    } else if (!spliters_.empty()) {
      auto iter = std::upper_bound(spliters_.begin(), spliters_.end(), oid);
      if (iter == spliters_.end()) {
        return spliters_.size();
      } else {
        return iter - spliters_.begin();
      }
    }
    return INVALID_PARTITION_ID;
  }

  bool valid() const { return fnum_ > 0 || !spliters_.empty(); }

  void reset() {
    fnum_ = 0;
    spliters_.clear();
  }

  template <typename IOADAPTOR_T>
  void serialize(std::unique_ptr<IOADAPTOR_T>& writer) {
    InArchive arc;
    arc << fnum_ << spliters_ << seed_;
    CHECK(writer->WriteArchive(arc));
  }

  template <typename IOADAPTOR_T>
  void deserialize(std::unique_ptr<IOADAPTOR_T>& reader) {
    OutArchive arc;
    CHECK(reader->ReadArchive(arc));
    arc >> fnum_ >> spliters_ >> seed_;
  }

 private:
  fid_t fnum_;
  std::vector<OID_T> spliters_;
  uint64_t seed_;
#ifndef USE_MURMUR_HASH
  std::hash<OID_T> hash_func_;
#endif
};

template <>
class Partitioner<std::string> {
 public:
  using oid_t = std::string;
  Partitioner() : fnum_(0) {}
  ~Partitioner() = default;

  void InitHashPartitioner(size_t frag_num, uint64_t seed = 0x11151115) {
    fnum_ = frag_num;
    spliters_.clear();
    seed_ = seed;
  }

  void InitSegmentedPartitioner(const std::vector<std::string>& spliters) {
    spliters_ = spliters;
    if (spliters_.empty()) {
      fnum_ = 1;
    } else {
      fnum_ = 0;
    }
  }

  void InitSegmentedPartitioner(const std::vector<string_view>& spliters) {
    for (auto s : spliters) {
      spliters_.emplace_back(s.data(), s.size());
    }
    if (spliters_.empty()) {
      fnum_ = 1;
    } else {
      fnum_ = 0;
    }
  }

  fid_t GetPartitionId(const std::string& oid) const {
    if (fnum_) {
#ifdef USE_MURMUR_HASH
      return static_cast<fid_t>(MurmurHash2_64(oid.data(), oid.size(), seed_) %
                                fnum_);
#else
      return hash_func_(string_view(oid)) % fnum_;
#endif
    } else if (!spliters_.empty()) {
      auto iter = std::upper_bound(spliters_.begin(), spliters_.end(), oid);
      if (iter == spliters_.end()) {
        return spliters_.size();
      } else {
        return iter - spliters_.begin();
      }
    }
    return INVALID_PARTITION_ID;
  }

  fid_t GetPartitionId(const string_view& oid) const {
    if (fnum_) {
#ifdef USE_MURMUR_HASH
      return static_cast<fid_t>(MurmurHash2_64(oid.data(), oid.size(), seed_) %
                                fnum_);
#else
      return hash_func_(string_view(oid)) % fnum_;
#endif
    } else if (!spliters_.empty()) {
      auto iter = std::upper_bound(spliters_.begin(), spliters_.end(), oid);
      if (iter == spliters_.end()) {
        return spliters_.size();
      } else {
        return iter - spliters_.begin();
      }
    }
    return INVALID_PARTITION_ID;
  }

  bool valid() const { return fnum_ > 0 || !spliters_.empty(); }

  void reset() {
    fnum_ = 0;
    spliters_.clear();
  }

  template <typename IOADAPTOR_T>
  void serialize(std::unique_ptr<IOADAPTOR_T>& writer) {
    InArchive arc;
    arc << fnum_ << spliters_ << seed_;
    CHECK(writer->WriteArchive(arc));
  }

  template <typename IOADAPTOR_T>
  void deserialize(std::unique_ptr<IOADAPTOR_T>& reader) {
    OutArchive arc;
    CHECK(reader->ReadArchive(arc));
    arc >> fnum_ >> spliters_ >> seed_;
  }

 private:
  fid_t fnum_;
  std::vector<std::string> spliters_;
  uint64_t seed_;
#ifndef USE_MURMUR_HASH
  std::hash<string_view> hash_func_;
#endif
};

template <>
class Partitioner<string_view> {
 public:
  using oid_t = string_view;
  Partitioner() : fnum_(0) {}
  ~Partitioner() = default;

  void InitHashPartitioner(size_t frag_num, uint64_t seed = 0x11151115) {
    fnum_ = frag_num;
    spliters_.clear();
    seed_ = seed;
  }

  void InitSegmentedPartitioner(const std::vector<std::string>& spliters) {
    spliters_ = spliters;
    if (spliters_.empty()) {
      fnum_ = 1;
    } else {
      fnum_ = 0;
    }
  }

  void InitSegmentedPartitioner(const std::vector<string_view>& spliters) {
    for (auto s : spliters) {
      spliters_.emplace_back(s.data(), s.size());
    }
    if (spliters_.empty()) {
      fnum_ = 1;
    } else {
      fnum_ = 0;
    }
  }

  fid_t GetPartitionId(const std::string& oid) const {
    if (fnum_) {
#ifdef USE_MURMUR_HASH
      return static_cast<fid_t>(MurmurHash2_64(oid.data(), oid.size(), seed_) %
                                fnum_);
#else
      return hash_func_(string_view(oid)) % fnum_;
#endif
    } else if (!spliters_.empty()) {
      auto iter = std::upper_bound(spliters_.begin(), spliters_.end(), oid);
      if (iter == spliters_.end()) {
        return spliters_.size();
      } else {
        return iter - spliters_.begin();
      }
    }
    return INVALID_PARTITION_ID;
  }

  fid_t GetPartitionId(const string_view& oid) const {
    if (fnum_) {
#ifdef USE_MURMUR_HASH
      return static_cast<fid_t>(MurmurHash2_64(oid.data(), oid.size(), seed_) %
                                fnum_);
#else
      return hash_func_(string_view(oid)) % fnum_;
#endif
    } else if (!spliters_.empty()) {
      auto iter = std::upper_bound(spliters_.begin(), spliters_.end(), oid);
      if (iter == spliters_.end()) {
        return spliters_.size();
      } else {
        return iter - spliters_.begin();
      }
    }
    return INVALID_PARTITION_ID;
  }

  bool valid() const { return fnum_ > 0 || !spliters_.empty(); }

  void reset() {
    fnum_ = 0;
    spliters_.clear();
  }

  template <typename IOADAPTOR_T>
  void serialize(std::unique_ptr<IOADAPTOR_T>& writer) {
    InArchive arc;
    arc << fnum_ << spliters_ << seed_;
    CHECK(writer->WriteArchive(arc));
  }

  template <typename IOADAPTOR_T>
  void deserialize(std::unique_ptr<IOADAPTOR_T>& reader) {
    OutArchive arc;
    CHECK(reader->ReadArchive(arc));
    arc >> fnum_ >> spliters_ >> seed_;
  }

 private:
  fid_t fnum_;
  std::vector<std::string> spliters_;
  uint64_t seed_;
#ifndef USE_MURMUR_HASH
  std::hash<string_view> hash_func_;
#endif
};

}  // namespace grape

#endif  // GRAPE_FRAGMENT_PARTITIONER_H_
