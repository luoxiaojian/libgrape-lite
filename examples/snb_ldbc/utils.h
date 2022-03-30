#ifndef EXAMPLES_SNB_LDBC_UTILS_H_
#define EXAMPLES_SNB_LDBC_UTILS_H_

#include <vector>
#include <string>

#include "string_view/string_view.hpp"

namespace grape {

void split(const char* data,
           std::vector<nonstd::string_view>& splits,
           char delimiter) {
  const char* end = data + strlen(data);
  while (true) {
    const char* ptr = data;
    while (*ptr != delimiter && ptr != end) {
      ++ptr;
    }
    splits.push_back(nonstd::string_view(data, ptr - data));
    if (ptr == end) {
      break;
    }
    data = ptr + 1;
  }
}

}  // namespace grape

#endif  // EXAMPLES_SNB_LDBC_UTILS_H_