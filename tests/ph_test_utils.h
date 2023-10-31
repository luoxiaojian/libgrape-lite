#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <glog/logging.h>
#include "grape/grape.h"
#include "grape/io/local_io_adaptor.h"
#include "grape/io/tsv_line_parser.h"

template <typename OID_T, typename VDATA_T>
void load_oid_list(const std::string& vfile, std::vector<OID_T>& oid_list) {
  oid_list.clear();

  auto io_adaptor =
      std::unique_ptr<grape::LocalIOAdaptor>(new grape::LocalIOAdaptor(vfile));
  io_adaptor->Open();
  std::string line;
  VDATA_T v_data;
  OID_T vertex_id;

  grape::TSVLineParser<OID_T, VDATA_T, grape::EmptyType> line_parser;

  while (io_adaptor->ReadLine(line)) {
    line_parser.LineParserForVFile(line, vertex_id, v_data);
    oid_list.push_back(vertex_id);
  }

  io_adaptor->Close();
}

template <typename VM_T>
void test1(std::shared_ptr<VM_T> vm) {
  grape::fid_t fnum = vm->GetCommSpec().fnum();
  for (grape::fid_t fid = 0; fid < fnum; ++fid) {
    uint32_t vnum = vm->GetInnerVertexSize(fid);
    for (uint32_t lid = 0; lid != vnum; ++lid) {
      typename VM_T::oid_t oid;
      uint32_t gid = vm->Lid2Gid(fid, lid);
      CHECK(vm->GetOid(fid, lid, oid));
      uint32_t got_gid;
      CHECK(vm->GetGid(fid, oid, got_gid));
      CHECK_EQ(got_gid, gid);
    }
  }
  double t0 = -grape::GetCurrentTime();
  for (grape::fid_t fid = 0; fid < fnum; ++fid) {
    uint32_t vnum = vm->GetInnerVertexSize(fid);
    for (uint32_t lid = 0; lid != vnum; ++lid) {
      typename VM_T::oid_t oid;
      uint32_t gid = vm->Lid2Gid(fid, lid);
      CHECK(vm->GetOid(fid, lid, oid));
      uint32_t got_gid;
      CHECK(vm->GetGid(fid, oid, got_gid));
      CHECK_EQ(got_gid, gid);
    }
  }
  t0 += grape::GetCurrentTime();
  if (vm->GetCommSpec().fid() == 0) {
    LOG(INFO) << "test1 " << t0 << " s";
  }
}

template <typename VM_T, typename OID_T>
void test2(std::shared_ptr<VM_T> vm, const std::vector<OID_T>& oid_list) {
  size_t num = oid_list.size();
  for (size_t i = 0; i != num; ++i) {
    auto& oid = oid_list[i];
    uint32_t gid;
    CHECK(vm->GetGid(oid, gid));
    typename VM_T::oid_t got_oid;
    CHECK(vm->GetOid(gid, got_oid));
    CHECK_EQ(got_oid, oid);
  }

  double t0 = -grape::GetCurrentTime();
  for (size_t i = 0; i != num; ++i) {
    auto& oid = oid_list[i];
    uint32_t gid;
    CHECK(vm->GetGid(oid, gid));
    typename VM_T::oid_t got_oid;
    CHECK(vm->GetOid(gid, got_oid));
    CHECK_EQ(got_oid, oid);
  }
  t0 += grape::GetCurrentTime();
  if (vm->GetCommSpec().fid() == 0) {
    LOG(INFO) << "test2 " << t0 << " s";
  }
}

template <typename VM_T, typename OID_T>
void test3(std::shared_ptr<VM_T> vm, const std::vector<OID_T>& oid_list) {
  size_t num = oid_list.size();
  std::atomic<size_t> offset(0);
  std::vector<std::thread> threads;
  const size_t chunk = 8192;
  double t0 = -grape::GetCurrentTime();
  int thread_num = std::thread::hardware_concurrency();
  for (int i = 0; i < thread_num; ++i) {
    threads.emplace_back([&]() {
      while (true) {
        size_t begin = offset.fetch_add(chunk);
        if (begin >= num) {
          break;
        }
        size_t end = std::min(begin + chunk, num);
        while (begin != end) {
          auto& oid = oid_list[begin];
          uint32_t gid;
          CHECK(vm->GetGid(oid, gid));
          typename VM_T::oid_t got_oid;
          CHECK(vm->GetOid(gid, got_oid));
          CHECK_EQ(got_oid, oid);
          ++begin;
        }
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }

  t0 += grape::GetCurrentTime();
  if (vm->GetCommSpec().fid() == 0) {
    LOG(INFO) << "test3 " << t0 << " s";
  }
}

inline double bytes_to_mb(size_t bytes) {
  return static_cast<double>(bytes) / 1024.0 / 1024.0;
}