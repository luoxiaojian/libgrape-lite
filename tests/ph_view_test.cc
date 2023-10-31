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

#include "grape/grape.h"
#include "grape/vertex_map/global_vertex_map.h"
#include "grape/vertex_map/ph_partitioned_vertex_map.h"
#include "grape/vertex_map/ph_vertex_map.h"
#include "tests/ph_test_utils.h"

void Init() {
  grape::InitMPIComm();
  grape::CommSpec comm_spec;
  comm_spec.Init(MPI_COMM_WORLD);
  if (comm_spec.worker_id() == grape::kCoordinatorRank) {
    VLOG(1) << "Workers of libgrape-lite initialized.";
  }
}

void Finalize() {
  grape::FinalizeMPIComm();
  VLOG(1) << "Workers finalized.";
}

template <typename OID_T>
std::shared_ptr<grape::PHVertexMap<OID_T, uint32_t>> ConstructPHVertexMap(
    const grape::CommSpec& comm_spec, const std::vector<OID_T>& oid_list,
    int thread_num) {
  auto vm_ptr =
      std::make_shared<grape::PHVertexMap<OID_T, uint32_t>>(comm_spec);
  MPI_Barrier(comm_spec.comm());
  double t = -grape::GetCurrentTime();
  vm_ptr->Init(oid_list, thread_num);
  MPI_Barrier(comm_spec.comm());
  t += grape::GetCurrentTime();
  if (comm_spec.fid() == 0) {
    LOG(INFO) << "Construct: " << t << " s";
  }
  return vm_ptr;
}

template <typename OID_T>
std::shared_ptr<grape::PHPartitionedVertexMap<OID_T, uint32_t>>
ConstructPPHVertexMapHash(const grape::CommSpec& comm_spec,
                          const std::vector<OID_T>& oid_list, int thread_num) {
  auto vm_ptr =
      std::make_shared<grape::PHPartitionedVertexMap<OID_T, uint32_t>>(
          comm_spec);
  MPI_Barrier(comm_spec.comm());
  double t = -grape::GetCurrentTime();
  vm_ptr->InitHash(oid_list, thread_num);
  MPI_Barrier(comm_spec.comm());
  t += grape::GetCurrentTime();
  if (comm_spec.fid() == 0) {
    LOG(INFO) << "Construct: " << t << " s";
  }

  return vm_ptr;
}

template <typename OID_T>
std::shared_ptr<grape::PHPartitionedVertexMap<OID_T, uint32_t>>
ConstructPPHVertexMapSeg(const grape::CommSpec& comm_spec,
                         const std::vector<OID_T>& oid_list, int thread_num) {
  auto vm_ptr =
      std::make_shared<grape::PHPartitionedVertexMap<OID_T, uint32_t>>(
          comm_spec);
  double t = -grape::GetCurrentTime();
  vm_ptr->InitSegmented(oid_list, thread_num);
  MPI_Barrier(comm_spec.comm());
  t += grape::GetCurrentTime();
  if (comm_spec.fid() == 0) {
    LOG(INFO) << "Construct: " << t << " s";
  }
  return vm_ptr;
}

template <typename OID_T, typename PARTITIONER_T>
std::shared_ptr<grape::GlobalVertexMap<OID_T, uint32_t, PARTITIONER_T>>
ConstructGlobalVertexMap(const grape::CommSpec& comm_spec,
                         const std::vector<OID_T>& oid_list) {
  PARTITIONER_T partitioner(comm_spec.fnum(), oid_list);
  auto vm_ptr =
      std::make_shared<grape::GlobalVertexMap<OID_T, uint32_t, PARTITIONER_T>>(
          comm_spec);
  vm_ptr->SetPartitioner(std::move(partitioner));
  double t0 = -grape::GetCurrentTime();
  vm_ptr->Init();
  auto builder = vm_ptr->GetLocalBuilder();
  for (auto& id : oid_list) {
    builder.add_vertex(id);
  }
  builder.finish(*vm_ptr);
  t0 += grape::GetCurrentTime();
  if (comm_spec.fid() == 0) {
    LOG(INFO) << "Construct: " << t0 << " s";
  }
  return vm_ptr;
}

template <typename VM_T>
void dump(std::shared_ptr<VM_T> vm, const std::string& prefix) {
  MPI_Barrier(vm->GetCommSpec().comm());
  double t = -grape::GetCurrentTime();
  vm->Serialize(prefix);
  MPI_Barrier(vm->GetCommSpec().comm());
  t += grape::GetCurrentTime();
  if (vm->GetCommSpec().fid() == 0) {
    LOG(INFO) << "Dump: " << t << " s";
  }
}

template <typename OID_T>
void choose_oid(const grape::CommSpec& comm_spec, const std::string& vfile,
                const std::string& output_file, int type, int thread_num) {
  std::vector<OID_T> oid_list;
  load_oid_list<OID_T, grape::EmptyType>(vfile, oid_list);

  if (type == 0) {
    LOG(INFO) << "ph, thread_num = " << thread_num;
    auto vm_ptr = ConstructPHVertexMap<OID_T>(comm_spec, oid_list, thread_num);
    test1(vm_ptr);
    std::shuffle(oid_list.begin(), oid_list.end(),
                 std::mt19937(std::random_device()()));
    test2(vm_ptr, oid_list);
    test3(vm_ptr, oid_list);
    if (comm_spec.fid() == 0) {
      LOG(INFO) << "memory usage: " << bytes_to_mb(vm_ptr->memory_usage())
                << "M";
    }
    dump(vm_ptr, output_file);
  } else if (type == 1) {
    LOG(INFO) << "pph hash, thread_num = " << thread_num;
    auto vm_ptr =
        ConstructPPHVertexMapHash<OID_T>(comm_spec, oid_list, thread_num);
    test1(vm_ptr);
    std::shuffle(oid_list.begin(), oid_list.end(),
                 std::mt19937(std::random_device()()));
    test2(vm_ptr, oid_list);
    test3(vm_ptr, oid_list);
    if (comm_spec.fid() == 0) {
      LOG(INFO) << "memory usage: " << bytes_to_mb(vm_ptr->memory_usage())
                << "M";
    }
    dump(vm_ptr, output_file);
  } else if (type == 2) {
    LOG(INFO) << "pph seg, thread_num = " << thread_num;
    grape::DistinctSort(oid_list);
    auto vm_ptr =
        ConstructPPHVertexMapSeg<OID_T>(comm_spec, oid_list, thread_num);
    test1(vm_ptr);
    std::shuffle(oid_list.begin(), oid_list.end(),
                 std::mt19937(std::random_device()()));
    test2(vm_ptr, oid_list);
    test3(vm_ptr, oid_list);
    if (comm_spec.fid() == 0) {
      LOG(INFO) << "memory usage: " << bytes_to_mb(vm_ptr->memory_usage())
                << "M";
    }
    dump(vm_ptr, output_file);
  } else if (type == 3) {
    LOG(INFO) << "gvm hash";
    auto vm_ptr =
        ConstructGlobalVertexMap<OID_T, grape::HashPartitioner<OID_T>>(
            comm_spec, oid_list);
    test1(vm_ptr);
    std::shuffle(oid_list.begin(), oid_list.end(),
                 std::mt19937(std::random_device()()));
    test2(vm_ptr, oid_list);
    test3(vm_ptr, oid_list);
    if (comm_spec.fid() == 0) {
      LOG(INFO) << "memory usage: " << bytes_to_mb(vm_ptr->memory_usage())
                << "M";
    }
    dump(vm_ptr, output_file);
  } else if (type == 4) {
    LOG(INFO) << "gvm seg";
    grape::DistinctSort(oid_list);
    auto vm_ptr =
        ConstructGlobalVertexMap<OID_T, grape::SegmentedPartitioner<OID_T>>(
            comm_spec, oid_list);
    test1(vm_ptr);
    std::shuffle(oid_list.begin(), oid_list.end(),
                 std::mt19937(std::random_device()()));
    test2(vm_ptr, oid_list);
    test3(vm_ptr, oid_list);
    if (comm_spec.fid() == 0) {
      LOG(INFO) << "memory usage: " << bytes_to_mb(vm_ptr->memory_usage())
                << "M";
    }
    dump(vm_ptr, output_file);
  }
}

int main(int argc, char* argv[]) {
  FLAGS_stderrthreshold = 0;
  google::InitGoogleLogging("analytical_apps");
  google::InstallFailureSignalHandler();

  Init();

  int type = atoi(argv[1]);
  std::string vfile = argv[2];
  std::string output_file = argv[3];
  int thread_num = atoi(argv[4]);

  grape::CommSpec comm_spec;
  comm_spec.Init(MPI_COMM_WORLD);

  if (type < 5) {
    if (comm_spec.fid() == 0) {
      std::cout << "OID = int64_t" << std::endl;
    }
    choose_oid<int64_t>(comm_spec, vfile, output_file, type, thread_num);
  } else {
    if (comm_spec.fid() == 0) {
      std::cout << "OID = string" << std::endl;
    }
    type -= 5;
    choose_oid<std::string>(comm_spec, vfile, output_file, type, thread_num);
  }
}