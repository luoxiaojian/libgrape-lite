#ifndef EXAMPLES_SNB_LDBC_UTILS_H_
#define EXAMPLES_SNB_LDBC_UTILS_H_

#include <vector>
#include <string>

#include "string_view/string_view.hpp"

namespace grape {

#if 0
void split(const char* data,
           std::vector<nonstd::string_view>& splits,
           char delimiter) {
  splits.clear();
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
#else
void split(const char* data,
           std::vector<nonstd::string_view>& splits,
           char delimiter) {
  if (splits.empty()) {
    return ;
  }
  int num = splits.size() - 1;
  for (int i = 0; i < num; ++i) {
    const char* end = strchr(data, ',');
    splits[i] = nonstd::string_view(data, end - data);
    data = end + 1;
  }
  splits[num] = nonstd::string_view(data, strlen(data));
}
#endif

}  // namespace grape

#endif  // EXAMPLES_SNB_LDBC_UTILS_H_