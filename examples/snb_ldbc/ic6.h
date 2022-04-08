#ifndef EXAMPLES_SNB_LDBC_IC6_H_
#define EXAMPLES_SNB_LDBC_IC6_H_

#include <set>
#include <queue>

#include "grape/fragment/property_fragment.h"
#include "examples/snb_ldbc/utils.h"
#include "grape/utils/bitset.h"

namespace grape {

#define USE_BITSET
#define PROF

#ifdef USE_BITSET
// inline void get_2d_friends(SingleLabelSubGraph& graph, uint32_t root, Bitset& friends) {
inline void get_2d_friends(SingleLabelSubGraph& graph, uint32_t root, std::vector<bool>& friends) {
  std::set<uint32_t> neighbors;
  AdjList<uint32_t, uint64_t> adjlist;
  adjlist = graph.GetIncomingAdjList(root);
  for (auto& e : adjlist) {
    neighbors.insert(e.get_neighbor_lid());
  }
  adjlist = graph.GetOutgoingAdjList(root);
  for (auto& e : adjlist) {
    neighbors.insert(e.get_neighbor_lid());
  }
  // friends.clear();
  for (auto v : neighbors) {
    friends[v] = true;
    // friends.set_bit(v);
    adjlist = graph.GetIncomingAdjList(v);
    for (auto& e : adjlist) {
      friends[e.get_neighbor_lid()] = true;
      // friends.set_bit(e.get_neighbor_lid());
    }
    adjlist = graph.GetOutgoingAdjList(v);
    for (auto& e : adjlist) {
      friends[e.get_neighbor_lid()] = true;
      // friends.set_bit(e.get_neighbor_lid());
    }
  }
  friends[root] = false;
  // friends.reset_bit(root);
}
#else
inline void get_2d_friends(SingleLabelSubGraph& graph, uint32_t root, std::set<uint32_t>& friends) {
  std::set<uint32_t> neighbors;
  AdjList<uint32_t, uint64_t> adjlist;
  adjlist = graph.GetIncomingAdjList(root);
  for (auto& e : adjlist) {
    neighbors.insert(e.get_neighbor_lid());
  }
  adjlist = graph.GetOutgoingAdjList(root);
  for (auto& e : adjlist) {
    neighbors.insert(e.get_neighbor_lid());
  }
  friends.clear();
  for (auto v : neighbors) {
    friends.insert(v);
    adjlist = graph.GetIncomingAdjList(v);
    for (auto& e : adjlist) {
      friends.insert(e.get_neighbor_lid());
    }
    adjlist = graph.GetOutgoingAdjList(v);
    for (auto& e : adjlist) {
      friends.insert(e.get_neighbor_lid());
    }
  }
  friends.erase(root);
}
#endif

struct TagComparer {
  TagComparer(std::vector<int>& counts, StringViewVector& names)
      : counts(counts), names(names) {}

  bool operator()(uint32_t lhs, uint32_t rhs) {
    if (counts[lhs] > counts[rhs]) {
      return true;
    } else if (counts[lhs] < counts[rhs]) {
      return false;
    } else {
      return names[lhs] < names[rhs];
    }
  }

  std::vector<int>& counts;
  StringViewVector& names;
};

class IC6 {
 public:
  IC6(PropertyFragment& fragment)
      : tag_label_id_(fragment.schema().get_vertex_label_id("TAG")),
        person_label_id_(fragment.schema().get_vertex_label_id("PERSON")),
        post_label_id_(fragment.schema().get_vertex_label_id("POST")),
        knows_label_id_(fragment.schema().get_edge_label_id("KNOWS")),
        has_creator_label_id_(fragment.schema().get_edge_label_id("HASCREATOR")),
        has_tag_label_id_(fragment.schema().get_edge_label_id("HASTAG")),
        person_sub_graph_(fragment.GetSubGraph(person_label_id_, knows_label_id_)),
        person_post_sub_graph_(fragment.GetSubGraph(person_label_id_, post_label_id_, has_creator_label_id_)),
        post_tag_sub_graph_(fragment.GetSubGraph(post_label_id_, tag_label_id_, has_tag_label_id_)) {
    auto tag_name_col = std::dynamic_pointer_cast<StringColumn>(fragment.GetVertexDataColumn(tag_label_id_, 0));
    auto& tag_name_buffer = tag_name_col->buffer();
    tag_num_ = tag_name_buffer.size();
    for (uint32_t tag_i = 0; tag_i != tag_num_; ++tag_i) {
      tag_indexer_._add(tag_name_buffer[tag_i]);
    }
#ifdef PROF
    stage0_ = 0.0;
    stage1_ = 0.0;
    stage2_ = 0.0;
#endif

#ifdef USE_BITSET
    // friends_.init(person_sub_graph_.vertex_num());
    friends_.resize(person_sub_graph_.vertex_num());
#endif
  }
  ~IC6() {
#ifdef PROF
    LOG(INFO) << "prof: " << stage0_ << ", " << stage1_ << ", " << stage2_;
#endif
  }

  void Query(const char* line, std::ostream& stream) {
    std::vector<nonstd::string_view> params;
    split(line, params, ',');
    Query(std::stol(params[1].to_string()), params[2], stream);
  }

  void Query(int64_t person_id, const nonstd::string_view& tag_name, std::ostream& stream) {
    uint32_t root = person_sub_graph_.GetVertex(person_id);
    uint32_t tag_id;
    CHECK(tag_indexer_.get_index(tag_name, tag_id));
#ifdef PROF
    stage0_ -= GetCurrentTime();
#endif
    get_2d_friends(person_sub_graph_, root, friends_);
#ifdef PROF
    stage0_ += GetCurrentTime();

    stage1_ -= GetCurrentTime();
#endif
    std::vector<int> post_count(tag_num_, 0);

#ifdef USE_BITSET
    uint32_t person_num = person_sub_graph_.vertex_num();
    for (uint32_t v = 0; v != person_num; ++v) {
      // if (!friends_.get_bit(v)) {
      if (!friends_[v]) {
        continue;
      }
      friends_[v] = false;
#else
    for (auto v : friends_) {
#endif
      auto ie = person_post_sub_graph_.GetIncomingAdjList(v);
      for (auto& e : ie) {
        uint32_t post_id = e.get_neighbor_lid();
        auto oe2 = post_tag_sub_graph_.GetOutgoingAdjList(post_id);
        bool contain = false;
        for (auto& e2 : oe2) {
          if (e2.get_neighbor_lid() == tag_id) {
            contain = true;
            break;
          }
        }
        if (contain) {
          for (auto& e2 : oe2) {
            ++post_count[e2.get_neighbor_lid()];
          }
        }
      }
    }
    post_count[tag_id] = 0;
#ifdef PROF
    stage1_ += GetCurrentTime();

    stage2_ -= GetCurrentTime();
#endif
    auto& tag_names = tag_indexer_.keys();
    TagComparer comparer(post_count, tag_names);
    std::priority_queue<uint32_t, std::vector<uint32_t>, TagComparer> que(comparer);
    uint32_t other_tag_id = 0;
    while (que.size() < 10 && other_tag_id < tag_num_) {
      if (post_count[other_tag_id] > 0) {
        que.push(other_tag_id);
      }
      ++other_tag_id;
    }
    uint32_t top = que.top();
    while (other_tag_id < tag_num_) {
      if (post_count[other_tag_id] > 0) {
        if (comparer(other_tag_id, top)) {
          que.pop();
          que.push(other_tag_id);
          top = que.top();
        }
      }
      ++other_tag_id;
    }
    std::vector<uint32_t> result;
    while (!que.empty()) {
      result.push_back(que.top());
      que.pop();
    }
#if 0
    for (auto iter = result.rbegin(); iter != result.rend(); ++iter) {
      uint32_t v = *iter;
      stream << tag_names[v] << " " << post_count[v] << "\n";
    }
#endif
#ifdef PROF
    stage2_ += GetCurrentTime();
#endif
  }

 private:
  uint8_t tag_label_id_;
  uint8_t person_label_id_;
  uint8_t post_label_id_;

  uint8_t knows_label_id_;
  uint8_t has_creator_label_id_;
  uint8_t has_tag_label_id_;

  SingleLabelSubGraph person_sub_graph_;
  DoubleLabelSubGraph person_post_sub_graph_;
  DoubleLabelSubGraph post_tag_sub_graph_;

#ifdef PROF
  double stage0_;
  double stage1_;
  double stage2_;
#endif

#ifdef USE_BITSET
  std::vector<bool> friends_;
#else
  std::set<uint32_t> friends_;
#endif

  uint32_t tag_num_;
  IdIndexer<nonstd::string_view, uint32_t> tag_indexer_;
};

}  // namespace grape


#endif  // EXAMPLES_SNB_LDBC_IC6_H_
