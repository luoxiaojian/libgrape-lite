#include "grape/fragment/property_fragment.h"
#include "grape/util.h"

#include "examples/snb_ldbc/ic6.h"
#include "examples/snb_ldbc/ic6_v2.h"

#include <atomic>
#include <string>
#include <iostream>
#include <thread>
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
  int thread_num = atoi(argv[4]);
  int iteration = atoi(argv[5]);

  grape::PropertyFragment fragment;
  LOG(INFO) << "Start to deserialize graph...";
  fragment.Deserialize(graph_path);
  LOG(INFO) << "Finished init graph...";
  FILE* fin = fopen(query_path.c_str(), "r");
  char line_buf[4096];
  std::vector<std::pair<int64_t, std::string>> params;
  std::vector<nonstd::string_view> splits(3);
  while (fgets(line_buf, 4096, fin) != NULL) {
    preprocessLine(line_buf);
    grape::split(line_buf, splits, ',');
    params.emplace_back(std::stol(splits[1].to_string()), splits[2].to_string());
  }
  std::atomic<int> cur(0);
  int params_num = params.size();
  LOG(INFO) << "before init apps...";
  std::vector<std::thread> threads(thread_num);
  std::vector<grape::IC6V2> apps;
  std::vector<std::ofstream> ostrms;
  for (int i = 0; i < thread_num; ++i) {
    apps.emplace_back(fragment);
    ostrms.emplace_back(output_path + "_t_" + std::to_string(i), std::ios::binary);
  }
  LOG(INFO) << "Start to run queries...";
  double t0 = -grape::GetCurrentTime();
  for (int i = 0; i < thread_num; ++i) {
    threads[i] = std::thread([&](int tid) {
      auto& app = apps[tid];
      auto& ostrm = ostrms[tid];
      while (true) {
        int got = cur.fetch_add(1);
        if (got >= iteration) {
          break;
        }
        auto& pair = params[got % params_num];
        app.Query(pair.first, pair.second, ostrm);
      }
    }, i);
  }
  for (auto& thrd : threads) {
    thrd.join();
  }
  t0 += grape::GetCurrentTime();
  LOG(INFO) << "Finished queries...";
  LOG(INFO) << t0 << " (s)";

  for (auto& ostrm : ostrms) {
    ostrm.flush();
    ostrm.close();
  }

  return 0;
}
