#ifndef ID_INDEXER_ID_INDEXER_H
#define ID_INDEXER_ID_INDEXER_H

#include <cmath>
#include <vector>

#include <assert.h>
#include <stdint.h>
#include <mpi.h>

#include "id_indexer_impl.h"
#include "shared_buffer.h"

namespace id_encoder_impl {

static constexpr int8_t min_lookups = 4;
static constexpr double max_load_factor = 0.5f;

inline int8_t log2(size_t value) {
  static constexpr int8_t table[64] = {
      63, 0,  58, 1,  59, 47, 53, 2,  60, 39, 48, 27, 54, 33, 42, 3,
      61, 51, 37, 40, 49, 18, 28, 20, 55, 30, 34, 11, 43, 14, 22, 4,
      62, 57, 46, 52, 38, 26, 32, 41, 50, 36, 17, 19, 29, 10, 13, 21,
      56, 45, 25, 31, 35, 16, 9,  12, 44, 24, 15, 8,  23, 7,  6,  5};
  value |= value >> 1;
  value |= value >> 2;
  value |= value >> 4;
  value |= value >> 8;
  value |= value >> 16;
  value |= value >> 32;
  return table[((value - (value >> 1)) * 0x07EDD5E59A4E28C2) >> 58];
}

template <typename T>
struct InternalBuffer {
  using type = std::vector<T>;

  static void send_to(const type& vec, int dst_worker_id, MPI_Comm comm, int tag) {
    uint64_t size = vec.size();
    MPI_Send(&size, 1, MPI_UINT64_T, dst_worker_id, tag, comm);
    MPI_Send(vec.data(), size * sizeof(T), MPI_CHAR, dst_worker_id, tag, comm);
  }

  static void recv_from(type& vec, int src_worker_id, MPI_Comm comm, int tag) {
    uint64_t size;
    MPI_Recv(&size, 1, MPI_UINT64_T, src_worker_id, tag, comm, MPI_STATUS_IGNORE);
    vec.resize(size);
    MPI_Recv(vec.data(), size * sizeof(T), MPI_CHAR, src_worker_id, tag, comm);
  }
};

}  // namespace id_encoder_impl

template <typename KeyT, typename IndexT, typename Hasher>
class ImmutableIdIndexer;

template <typename KeyT, typename IndexT, typename Hasher=std::hash<KeyT>>
class IdIndexer {
 public:
  using KeyBuffer = typename id_encoder_impl::InternalBuffer<KeyT>::type;
  using IndexBuffer =
      typename id_encoder_impl::InternalBuffer<IndexT>::type;
  using DistBuffer = typename id_encoder_impl::InternalBuffer<int8_t>::type;

  IdIndexer() : hasher_() { reset_to_empty_state(); }
  ~IdIndexer() {}

  size_t entry_num() const { return distances_.size(); }

  bool add(const KeyT& oid, IndexT& lid) {
    size_t index = hash_policy_.index_for_hash(hasher_(oid));

    int8_t distance_from_desired = 0;
    for (; distances_[index] >= distance_from_desired;
         ++index, ++distance_from_desired) {
      IndexT cur_lid = indices_[index];
      if (keys_[cur_lid] == oid) {
        lid = cur_lid;
        return false;
      }
    }

    lid = static_cast<IndexT>(keys_.size());
    keys_.push_back(oid);
    assert(keys_.size() == num_elements_ + 1);
    emplace_new_value(distance_from_desired, index, lid);
    assert(keys_.size() == num_elements_);
    return true;
  }

  void finish() {}

  bool _add(const KeyT& oid, size_t hash_value,  IndexT& lid) {
    size_t index = hash_policy_.index_for_hash(hash_value);

    int8_t distance_from_desired = 0;
    for (; distances_[index] >= distance_from_desired;
           ++index, ++distance_from_desired) {
      IndexT cur_lid = indices_[index];
      if (keys_[cur_lid] == oid) {
        lid = cur_lid;
        return false;
      }
    }

    lid = static_cast<IndexT>(keys_.size());
    keys_.push_back(oid);
    assert(keys_.size() == num_elements_ + 1);
    emplace_new_value(distance_from_desired, index, lid);
    assert(keys_.size() == num_elements_);
    return true;
  }

  void _add(const KeyT& oid) {
    size_t index = hash_policy_.index_for_hash(hasher_(oid));

    int8_t distance_from_desired = 0;
    for (; distances_[index] >= distance_from_desired;
         ++index, ++distance_from_desired) {
      if (keys_[indices_[index]] == oid) {
        return ;
      }
    }

    IndexT lid = static_cast<IndexT>(keys_.size());
    keys_.push_back(oid);
    assert(keys_.size() == num_elements_ + 1);
    emplace_new_value(distance_from_desired, index, lid);
    assert(keys_.size() == num_elements_);
  }

  size_t bucket_count() const {
    return num_slots_minus_one_ ? num_slots_minus_one_ + 1 : 0;
  }

  bool empty() const { return (num_elements_ == 0); }

  size_t size() const { return num_elements_; }

  bool get_key(IndexT lid, KeyT& oid) const {
    if (lid >= num_elements_) {
      return false;
    }
    oid = keys_[lid];
    return true;
  }

  bool get_index(const KeyT& oid, IndexT& lid) const {
    size_t index = hash_policy_.index_for_hash(hasher_(oid));
    for (int8_t distance = 0; distances_[index] >= distance;
         ++distance, ++index) {
      IndexT ret = indices_[index];
      if (keys_[ret] == oid) {
        lid = ret;
        return true;
      }
    }
    return false;
  }

  bool _get_index(const KeyT& oid, size_t hash, IndexT& lid) const {
    size_t index = hash_policy_.index_for_hash(hash);
    for (int8_t distance = 0; distances_[index] >= distance;
         ++distance, ++index) {
      IndexT ret = indices_[index];
      if (keys_[ret] == oid) {
        lid = ret;
        return true;
      }
    }
    return false;
  }

  void swap(IdIndexer<KeyT, IndexT, Hasher>& rhs) {
    keys_.swap(rhs.keys_);
    indices_.swap(rhs.indices_);
    distances_.swap(rhs.distances_);

    hash_policy_.swap(rhs.hash_policy_);
    std::swap(max_lookups_, rhs.max_lookups_);
    std::swap(num_elements_, rhs.num_elements_);
    std::swap(num_slots_minus_one_, rhs.num_slots_minus_one_);

    std::swap(hasher_, rhs.hasher_);
  }

  const KeyBuffer& keys() const { return keys_; }

  KeyBuffer& keys() { return keys_; }

  void send_to(int dst_worker_id, MPI_Comm comm, int tag = 0) {
    uint64_t members[4];
    members[0] = hash_policy_.get_mod_function_index();
    members[1] = static_cast<uint64_t>(max_lookups_);
    members[2] = num_elements_;
    members[3] = num_slots_minus_one_;

    MPI_Send(members, 4, MPI_UINT64_T, dst_worker_id, tag, comm);
    id_encoder_impl::InternalBuffer<KeyT>::send_to(keys_, dst_worker_id, comm, tag);
    id_encoder_impl::InternalBuffer<IndexT>::send_to(indices_, dst_worker_id, comm, tag);
    id_encoder_impl::InternalBuffer<int8_t>::send_to(distances_, dst_worker_id, comm, tag);
  }

  void recv_from(int src_worker_id, MPI_Comm comm, int tag = 0) {
    uint64_t members[4];
    MPI_Recv(members, 4, MPI_UINT64_T, src_worker_id, tag, comm);
    hash_policy_.set_mod_function_by_index(members[0]);
    max_lookups_ = static_cast<int8_t>(members[1]);
    num_elements_ = members[2];
    num_slots_minus_one_ = members[3];

    id_encoder_impl::InternalBuffer<KeyT>::recv_from(keys_, src_worker_id, comm, tag);
    id_encoder_impl::InternalBuffer<IndexT>::recv_from(indices_, src_worker_id, comm, tag);
    id_encoder_impl::InternalBuffer<int8_t>::recv_from(distances_, src_worker_id, comm, tag);
  }

 private:
  void emplace(IndexT lid) {
    KeyT key = keys_[lid];
    size_t index = hash_policy_.index_for_hash(hasher_(key));
    int8_t distance_from_desired = 0;
    for (; distances_[index] >= distance_from_desired;
           ++index, ++distance_from_desired) {
      if (indices_[index] == lid) {
        return;
      }
    }

    emplace_new_value(distance_from_desired, index, lid);
  }

  void emplace_new_value(int8_t distance_from_desired, size_t index,
                         IndexT lid) {
    if (num_slots_minus_one_ == 0 || distance_from_desired == max_lookups_ ||
        num_elements_ + 1 >
        (num_slots_minus_one_ + 1) * id_encoder_impl::max_load_factor) {
      grow();
      return;
    } else if (distances_[index] < 0) {
      indices_[index] = lid;
      distances_[index] = distance_from_desired;
      ++num_elements_;
      return;
    }
    IndexT to_insert = lid;
    std::swap(distance_from_desired, distances_[index]);
    std::swap(to_insert, indices_[index]);
    for (++distance_from_desired, ++index;; ++index) {
      if (distances_[index] < 0) {
        indices_[index] = to_insert;
        distances_[index] = distance_from_desired;
        ++num_elements_;
        return;
      } else if (distances_[index] < distance_from_desired) {
        std::swap(distance_from_desired, distances_[index]);
        std::swap(to_insert, indices_[index]);
        ++distance_from_desired;
      } else {
        ++distance_from_desired;
        if (distance_from_desired == max_lookups_) {
          grow();
          return;
        }
      }
    }
  }

  void grow() { rehash(std::max(size_t(4), 2 * bucket_count())); }

  void rehash(size_t num_buckets) {
    num_buckets = std::max(
        num_buckets, static_cast<size_t>(std::ceil(
            num_elements_ / id_encoder_impl::max_load_factor)));

    if (num_buckets == 0) {
      reset_to_empty_state();
      return;
    }

    auto new_prime_index = hash_policy_.next_size_over(num_buckets);
    if (num_buckets == bucket_count()) {
      return;
    }

    int8_t new_max_lookups = compute_max_lookups(num_buckets);

    DistBuffer new_distances(num_buckets + new_max_lookups);
    size_t special_end_index = num_buckets + new_max_lookups - 1;
    for (size_t i = 0; i != special_end_index; ++i) {
      new_distances[i] = -1;
    }
    new_distances[special_end_index] = 0;

    IndexBuffer new_indices(num_buckets + new_max_lookups);

    new_indices.swap(indices_);
    new_distances.swap(distances_);

    std::swap(num_slots_minus_one_, num_buckets);
    --num_slots_minus_one_;
    hash_policy_.commit(new_prime_index);

    max_lookups_ = new_max_lookups;

    num_elements_ = 0;
    IndexT elem_num = static_cast<IndexT>(keys_.size());
    for (IndexT lid = 0; lid < elem_num; ++lid) {
      emplace(lid);
    }
  }

  void reset_to_empty_state() {
    keys_.clear();

    indices_.clear();
    distances_.clear();
    indices_.resize(id_encoder_impl::min_lookups);
    distances_.resize(id_encoder_impl::min_lookups, -1);
    distances_[id_encoder_impl::min_lookups - 1] = 0;

    num_slots_minus_one_ = 0;
    hash_policy_.reset();
    max_lookups_ = id_encoder_impl::min_lookups - 1;
    num_elements_ = 0;
  }

  static int8_t compute_max_lookups(size_t num_buckets) {
    int8_t desired = id_encoder_impl::log2(num_buckets);
    return std::max(id_encoder_impl::min_lookups, desired);
  }


  KeyBuffer keys_;
  IndexBuffer indices_;
  DistBuffer distances_;

  id_indexer_impl::prime_number_hash_policy hash_policy_;
  int8_t max_lookups_ = id_encoder_impl::min_lookups - 1;
  size_t num_elements_ = 0;
  size_t num_slots_minus_one_ = 0;

  Hasher hasher_;

  template <typename _KeyT, typename _IndexT, typename _Hasher>
  friend class ImmutableIdIndexer;
};


#endif  // ID_INDEXER_ID_INDEXER_H
