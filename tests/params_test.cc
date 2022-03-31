#include <string.h>
#include <glog/logging.h>

#include <string>
#include <vector>

#include "string_view/string_view.hpp"
#include "examples/snb_ldbc/utils.h"

void preprocessLine(char* line) {
  size_t len = strlen(line);
  while (len >= 0) {
    if (line[len] != '\0' && line[len] != '\n' &&
        line[len] != '\r' && line[len] != ' ' && line[len] != '\t') {
      break;
    } else {
      --len;
    }
  }
  line[len + 1] = '\0';
}

int main(int argc, char** argv) {
  std::string query_path = argv[1];
  int k = atoi(argv[2]);

  FILE* fin = fopen(query_path.c_str(), "r");
  char line_buf[4096];
  std::vector<std::pair<int64_t, std::string>> params;
  std::vector<nonstd::string_view> splits(3);
  while (fgets(line_buf, 4096, fin) != NULL) {
    preprocessLine(line_buf);
    grape::split(line_buf, splits, ',');
    params.emplace_back(std::stol(splits[1].to_string()), splits[2].to_string());
  }

  LOG(INFO) << params[k].first << ", " << params[k].second;

  return 0;
}
