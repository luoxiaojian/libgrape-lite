#ifndef EXAMPLES_ANALYTICAL_APPS_TRAVERSE_TRAVERSE_MUTATE_H_
#define EXAMPLES_ANALYTICAL_APPS_TRAVERSE_TRAVERSE_MUTATE_H_

#include <grape/grape.h>

#include "traverse/traverse_mutate_context.h"

namespace grape {

template <typename FRAG_T>
class TraverseMutate : public ParallelAppBase<FRAG_T, TraverseMutateContext<FRAG_T>>,
                       public ParallelEngine {
  using edata_t = typename FRAG_T::edata_t;
public:
 INSTALL_PARALLEL_WORKER(TraverseMutate<FRAG_T>, TraverseMutateContext<FRAG_T>, FRAG_T)

 void PEval(const fragment_t& frag, context_t& ctx,
            message_manager_t& messages) {
   auto& inner_vertices = frag.InnerVertices();
   for (auto& v : inner_vertices) {
     if (frag.GetId(v) % 2 == 1) {
       auto es = frag.GetOutgoingAdjList(v);
       for (auto& e : es) {
         if (frag.GetId(e.neighbor) % 2 == 1) {
           ctx.remove_edge(v, e.neighbor);
         }
       }
     }
   }
   messages.ForceContinue();
 }

 void IncEval(const fragment_t& frag, context_t& ctx,
              message_manager_t& messages) {
   auto& inner_vertices = frag.InnerVertices();
   for (auto& v : inner_vertices) {
     ctx.add_edge(v, v, edata_t());
   }
 }
};

}  // namespace grape

#endif  // EXAMPLES_ANALYTICAL_APPS_TRAVERSE_TRAVERSE_MUTATE_H_