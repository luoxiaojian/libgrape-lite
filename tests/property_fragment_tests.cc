#include "grape/fragment/property_fragment.h"
#include "grape/util.h"

#include "examples/snb_ldbc/ic6.h"

#include <string>
#include <iostream>
#include <fstream>

using grape::PropertyType;

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
  std::string graph_path = argv[1];
  std::string query_path = argv[2];
  std::string output_path = argv[3];

  grape::PropertyFragment fragment;
  fragment.Deserialize(graph_path);
  LOG(INFO) << "Finished init graph...";

  grape::IC6 ic6(fragment);
  LOG(INFO) << "Finished init application...";

  std::ofstream ostrm(output_path, std::ios::binary);
  FILE* fin = fopen(query_path.c_str(), "r");
  char line_buf[4096];
#if 0
  while (fgets(line_buf, 4096, fin) != NULL) {
    preprocessLine(line_buf);
    if (line_buf[0] == 'i' && line_buf[1] == 'c' && line_buf[2] == '6') {
      ic6.Query(line_buf, ostrm);
    }
  }
#else
  std::vector<std::pair<int64_t, std::string>> params;
  std::vector<nonstd::string_view> splits;
  while (fgets(line_buf, 4096, fin) != NULL) {
    preprocessLine(line_buf);
    grape::split(line_buf, splits, ',');
    params.emplace_back(std::stol(splits[1].to_string()), splits[2].to_string());
  }

  const int iteration = 10000;
  int params_num = params.size();
  LOG(INFO) << "Start to run queries...";
  double t0 = -grape::GetCurrentTime();
  for (int i = 0; i < iteration; ++i) {
    auto& pair = params[i % params_num];
    ic6.Query(pair.first, pair.second, ostrm);
  }
  t0 += grape::GetCurrentTime();
  LOG(INFO) << "Finished queries...";
  LOG(INFO) << t0 / static_cast<double>(iteration) << " (s)";
#endif

  ostrm.flush();
  ostrm.close();

  return 0;
}
