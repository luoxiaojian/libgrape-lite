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

#ifndef GRAPE_REORDER_GORDER_REORDER_H_
#define GRAPE_REORDER_GORDER_REORDER_H_

#include "grape/fragment/immutable_edgecut_fragment.h"
#include "grape/reorder/reorder_base.h"

#include "glog/logging.h"
#include "unitheap.h"

// This file contains the implementation of the GOrder
// The implementation is based on https://github.com/lecfab/rescience-gorder

namespace grape {

template <typename FRAG_T>
class GOrderReorder : public ReorderBase<FRAG_T> {
 public:
  GOrderReorder(uint32_t w) {}

  void Reorder(FRAG_T& fragment) override {
    LOG(ERROR) << "GOrderReorder is not implemented yet";
  }
};

template <typename OID_T, typename VID_T, typename VDATA_T, typename EDATA_T,
          LoadStrategy load_strategy, typename PART_T>
class GOrderReorder<
    ImmutableEdgecutFragment<OID_T, VID_T, VDATA_T, EDATA_T, load_strategy,
                             GlobalVertexMap<OID_T, VID_T, PART_T>>>
    : public ReorderBase<ImmutableEdgecutFragment<
          OID_T, VID_T, VDATA_T, EDATA_T, load_strategy,
          GlobalVertexMap<OID_T, VID_T, PART_T>>> {
  using fragment_t =
      ImmutableEdgecutFragment<OID_T, VID_T, VDATA_T, EDATA_T, load_strategy,
                               GlobalVertexMap<OID_T, VID_T, PART_T>>;
  using vid_t = typename fragment_t::vid_t;
  using vertex_t = typename fragment_t::vertex_t;

 public:
  GOrderReorder(uint32_t w) : w_(w) {}

  void Reorder(fragment_t& fragment) override {
    if (fragment.directed()) {
      gorder_directed(fragment);
    } else {
      gorder_undirected(fragment);
    }
  }

 private:
  void move_window_d(fragment_t& fragment, gorder::UnitHeap& heap,
                     vid_t new_node, vid_t old_node) {
    auto old_parent = fragment.GetIncomingAdjList(vertex_t(old_node)).begin();
    auto old_parent_end = fragment.GetIncomingAdjList(vertex_t(old_node)).end();
    auto new_parent = fragment.GetIncomingAdjList(vertex_t(new_node)).begin();
    auto new_parent_end = fragment.GetIncomingAdjList(vertex_t(new_node)).end();

    vid_t ivnum = fragment.GetInnerVerticesNum();

    if (old_node == new_node) {
      old_parent = old_parent_end;
    } else {
      if (static_cast<uint64_t>(
              fragment.GetLocalOutDegree(vertex_t(old_node))) <= heap.huge) {
        for (auto& e : fragment.GetOutgoingAdjList(vertex_t(old_node))) {
          auto child = e.get_neighbor().GetValue();
          if (child < ivnum) {
            heap.lazyIncrement(child, -1);
          }
        }
      }
    }

    std::vector<vid_t> tmp_old_parents, tmp_new_parents;
    while (true) {
      int factor = -1;
      if (old_parent == old_parent_end) {
        if (new_parent == new_parent_end)
          break;
        factor = 1;
      } else if (new_parent != new_parent_end) {
        if (new_parent->neighbor == old_parent->neighbor) {
          ++old_parent;
          ++new_parent;
          continue;
        }
        if (new_parent->neighbor < old_parent->neighbor) {
          factor = 1;
        }
      }

      if (factor == -1) {
        if (static_cast<uint64_t>(fragment.GetLocalOutDegree(
                old_parent->neighbor)) <= heap.huge) {
          tmp_old_parents.push_back(old_parent->neighbor.GetValue());
        }
        ++old_parent;
      } else {
        if (static_cast<uint64_t>(fragment.GetLocalOutDegree(
                new_parent->neighbor)) <= heap.huge) {
          tmp_new_parents.push_back(new_parent->neighbor.GetValue());
        }
        ++new_parent;
      }
    }

    for (auto& parent : tmp_old_parents) {
      if (parent < ivnum) {
        heap.lazyIncrement(parent, -1);
      }
      for (auto& e : fragment.GetOutgoingAdjList(vertex_t(parent))) {
        auto sibling = e.get_neighbor().GetValue();
        if (sibling < ivnum && sibling != old_node) {
          heap.lazyIncrement(sibling, -1);
        }
      }
    }

    if (static_cast<uint64_t>(fragment.GetLocalOutDegree(vertex_t(new_node))) <=
        heap.huge) {
      for (auto& e : fragment.GetOutgoingAdjList(vertex_t(new_node))) {
        auto child = e.get_neighbor().GetValue();
        if (child < ivnum) {
          heap.lazyIncrement(child, 1);
        }
      }
    }

    for (auto& parent : tmp_new_parents) {
      if (parent < ivnum) {
        heap.lazyIncrement(parent, 1);
      }
      for (auto& e : fragment.GetOutgoingAdjList(vertex_t(parent))) {
        auto sibling = e.get_neighbor().GetValue();
        if (sibling < ivnum && sibling != new_node) {
          heap.lazyIncrement(sibling, 1);
        }
      }
    }
  }

  void gorder_directed(fragment_t& fragment) {
    if (load_strategy != LoadStrategy::kBothOutIn) {
      LOG(ERROR) << "GOrderReorder only supports LoadStrategy::kBothOutIn "
                    "for directed graph";
      return;
    }

    std::vector<vertex_t> order;
    vid_t ivnum = fragment.GetInnerVerticesNum();
    order.reserve(ivnum);
    gorder::UnitHeap heap(ivnum);
    std::vector<vertex_t> isolates;

    for (vid_t i = 0; i < ivnum; ++i) {
      int ideg = fragment.GetLocalInDegree(vertex_t(i));
      int deg = ideg + fragment.GetLocalOutDegree(vertex_t(i));

      if (deg == 0) {
        isolates.push_back(vertex_t(i));
      } else {
        heap.InsertElement(i, ideg);
      }
    }

    heap.ReConstruct();

    vid_t hub = heap.top;
    order.push_back(vertex_t(hub));
    heap.DeleteElement(hub);

    move_window_d(fragment, heap, hub, hub);

    while (heap.heapsize > 0) {
      vid_t new_node = heap.ExtractMax();
      CHECK_LT(new_node, ivnum);
      order.push_back(vertex_t(new_node));
      vid_t old_node = new_node;
      if (order.size() > w_) {
        old_node = order[order.size() - w_ - 1].GetValue();
      }
      move_window_d(fragment, heap, new_node, old_node);
    }

    order.insert(order.end(), isolates.begin(), isolates.end());

    fragment.Reorder(order);
  }

  void move_window_ud(fragment_t& fragment, gorder::UnitHeap& heap,
                      vid_t new_node, vid_t old_node) {
    auto old_parent = fragment.GetOutgoingAdjList(vertex_t(old_node)).begin();
    auto old_parent_end = fragment.GetOutgoingAdjList(vertex_t(old_node)).end();
    auto new_parent = fragment.GetOutgoingAdjList(vertex_t(new_node)).begin();
    auto new_parent_end = fragment.GetOutgoingAdjList(vertex_t(new_node)).end();

    vid_t ivnum = fragment.GetInnerVerticesNum();

    if (old_node == new_node) {
      old_parent = old_parent_end;
    } else {
      if (static_cast<uint64_t>(fragment.GetLocalOutDegree(vertex_t(old_node))) <=
          heap.huge) {
        for (auto& e : fragment.GetOutgoingAdjList(vertex_t(old_node))) {
          auto child = e.get_neighbor().GetValue();
          if (child < ivnum) {
            heap.lazyIncrement(child, -1);
          }
        }
      }
    }

    std::vector<vid_t> tmp_old_parents, tmp_new_parents;
    while (true) {
      int factor = -1;
      if (old_parent == old_parent_end) {
        if (new_parent == new_parent_end)
          break;
        factor = 1;
      } else if (new_parent != new_parent_end) {
        if (new_parent->neighbor == old_parent->neighbor) {
          ++old_parent;
          ++new_parent;
          continue;
        }
        if (new_parent->neighbor < old_parent->neighbor) {
          factor = 1;
        }
      }

      if (factor == -1) {
        if (static_cast<uint64_t>(fragment.GetLocalOutDegree(
                old_parent->neighbor)) <= heap.huge) {
          tmp_old_parents.push_back(old_parent->neighbor.GetValue());
        }
        ++old_parent;
      } else {
        if (static_cast<uint64_t>(fragment.GetLocalOutDegree(
                new_parent->neighbor)) <= heap.huge) {
          tmp_new_parents.push_back(new_parent->neighbor.GetValue());
        }
        ++new_parent;
      }
    }

    for (auto& parent : tmp_old_parents) {
      if (parent >= ivnum) {
        for (auto& e : fragment.GetIncomingAdjList(vertex_t(parent))) {
          auto sibling = e.get_neighbor().GetValue();
          if (sibling != old_node) {
            heap.lazyIncrement(sibling, -1);
          }
        }
      } else {
        heap.lazyIncrement(parent, -1);
        for (auto& e : fragment.GetOutgoingAdjList(vertex_t(parent))) {
          auto sibling = e.get_neighbor().GetValue();
          if (sibling < ivnum && sibling != old_node) {
            heap.lazyIncrement(sibling, -1);
          }
        }
      }
    }

    if (static_cast<uint64_t>(fragment.GetLocalOutDegree(vertex_t(new_node))) <=
        heap.huge) {
      for (auto& e : fragment.GetOutgoingAdjList(vertex_t(new_node))) {
        auto child = e.get_neighbor().GetValue();
        if (child < ivnum) {
          heap.lazyIncrement(child, 1);
        }
      }
    }

    for (auto& parent : tmp_new_parents) {
      if (parent >= ivnum) {
        for (auto& e : fragment.GetIncomingAdjList(vertex_t(parent))) {
          auto sibling = e.get_neighbor().GetValue();
          if (sibling != new_node) {
            heap.lazyIncrement(sibling, 1);
          }
        }
      } else {
        heap.lazyIncrement(parent, 1);
        for (auto& e : fragment.GetOutgoingAdjList(vertex_t(parent))) {
          auto sibling = e.get_neighbor().GetValue();
          if (sibling < ivnum && sibling != new_node) {
            heap.lazyIncrement(sibling, 1);
          }
        }
      }
    }
  }

  void gorder_undirected(fragment_t& fragment) {
    if (load_strategy != LoadStrategy::kOnlyOut) {
      LOG(ERROR) << "GOrderReorder only supports LoadStrategy::kOnlyOut "
                    "for undirected graph";
      return;
    }

    std::vector<vertex_t> order;
    vid_t ivnum = fragment.GetInnerVerticesNum();
    order.reserve(ivnum);
    gorder::UnitHeap heap(ivnum);
    std::vector<vertex_t> isolates;

    for (vid_t i = 0; i < ivnum; ++i) {
      int deg = fragment.GetLocalOutDegree(vertex_t(i));

      if (deg == 0) {
        isolates.push_back(vertex_t(i));
      } else {
        heap.InsertElement(i, deg);
      }
    }

    heap.ReConstruct();

    vid_t hub = heap.top;
    order.push_back(vertex_t(hub));
    heap.DeleteElement(hub);

    move_window_ud(fragment, heap, hub, hub);

    while (heap.heapsize > 0) {
      vid_t new_node = heap.ExtractMax();
      CHECK_LT(new_node, ivnum);
      order.push_back(vertex_t(new_node));
      vid_t old_node = new_node;
      if (order.size() > w_) {
        old_node = order[order.size() - w_ - 1].GetValue();
      }
      move_window_ud(fragment, heap, new_node, old_node);
    }

    order.insert(order.end(), isolates.begin(), isolates.end());

    fragment.Reorder(order);
  }

  uint32_t w_;
};

}  // namespace grape

#endif  // GRAPE_REORDER_GORDER_REORDER_H_
