#ifndef GRAPE_UTILS_PHF_VIEW_H_
#define GRAPE_UTILS_PHF_VIEW_H_

#include <pthash/single_phf.hpp>

#include "grape/utils/mmap_array.h"

namespace grape {

template <typename Hasher, typename Encoder, bool Minimal>
struct phf_view {
  typedef Encoder encoder_type;
  static constexpr bool minimal = Minimal;

 private:
  mmap_array<char> buffer;
};

}  // namespace grape

#endif  // GRAPE_UTILS_PHF_VIEW_H_