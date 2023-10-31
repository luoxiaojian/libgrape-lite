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

template <typename VM_T>
std::shared_ptr<VM_T> load(const grape::CommSpec& comm_spec,
                           const std::string& prefix) {
  auto vm_ptr = std::make_shared<VM_T>(comm_spec);
  MPI_Barrier(vm_ptr->GetCommSpec().comm());
  double t = -grape::GetCurrentTime();
  vm_ptr->Deserialize(prefix, comm_spec.fid());
  MPI_Barrier(vm_ptr->GetCommSpec().comm());
  t += grape::GetCurrentTime();
  if (vm_ptr->GetCommSpec().fid() == 0) {
    LOG(INFO) << "Load: " << t << " s";
  }
  return vm_ptr;
}

template <typename OID_T>
void choose_oid(const grape::CommSpec& comm_spec, const std::string& vfile,
                const std::string& input_file, int type) {
  std::vector<OID_T> oid_list;
  load_oid_list<OID_T, grape::EmptyType>(vfile, oid_list);

  if (type == 0) {
    LOG(INFO) << "ph";
    auto vm_ptr =
        load<grape::PHVertexMap<OID_T, uint32_t>>(comm_spec, input_file);
    test1(vm_ptr);
    std::shuffle(oid_list.begin(), oid_list.end(),
                 std::mt19937(std::random_device()()));
    test2(vm_ptr, oid_list);
    test3(vm_ptr, oid_list);
    if (comm_spec.fid() == 0) {
      LOG(INFO) << "memory usage: " << bytes_to_mb(vm_ptr->memory_usage())
                << "M";
    }
  } else if (type == 1) {
    LOG(INFO) << "pph hash";
    auto vm_ptr = load<grape::PHPartitionedVertexMap<OID_T, uint32_t>>(
        comm_spec, input_file);
    test1(vm_ptr);
    std::shuffle(oid_list.begin(), oid_list.end(),
                 std::mt19937(std::random_device()()));
    test2(vm_ptr, oid_list);
    test3(vm_ptr, oid_list);
    if (comm_spec.fid() == 0) {
      LOG(INFO) << "memory usage: " << bytes_to_mb(vm_ptr->memory_usage())
                << "M";
    }
  } else if (type == 2) {
    LOG(INFO) << "pph seg";
    auto vm_ptr = load<grape::PHPartitionedVertexMap<OID_T, uint32_t>>(
        comm_spec, input_file);
    test1(vm_ptr);
    std::shuffle(oid_list.begin(), oid_list.end(),
                 std::mt19937(std::random_device()()));
    test2(vm_ptr, oid_list);
    test3(vm_ptr, oid_list);
    if (comm_spec.fid() == 0) {
      LOG(INFO) << "memory usage: " << bytes_to_mb(vm_ptr->memory_usage())
                << "M";
    }
  } else if (type == 3) {
    LOG(INFO) << "gvm hash";
    auto vm_ptr = load<
        grape::GlobalVertexMap<OID_T, uint32_t, grape::HashPartitioner<OID_T>>>(
        comm_spec, input_file);
    test1(vm_ptr);
    std::shuffle(oid_list.begin(), oid_list.end(),
                 std::mt19937(std::random_device()()));
    test2(vm_ptr, oid_list);
    test3(vm_ptr, oid_list);
    if (comm_spec.fid() == 0) {
      LOG(INFO) << "memory usage: " << bytes_to_mb(vm_ptr->memory_usage())
                << "M";
    }
  } else if (type == 4) {
    LOG(INFO) << "gvm seg";
    auto vm_ptr =
        load<grape::GlobalVertexMap<OID_T, uint32_t,
                                    grape::SegmentedPartitioner<OID_T>>>(
            comm_spec, input_file);
    test1(vm_ptr);
    std::shuffle(oid_list.begin(), oid_list.end(),
                 std::mt19937(std::random_device()()));
    test2(vm_ptr, oid_list);
    test3(vm_ptr, oid_list);
    if (comm_spec.fid() == 0) {
      LOG(INFO) << "memory usage: " << bytes_to_mb(vm_ptr->memory_usage())
                << "M";
    }
  }
}

int main(int argc, char** argv) {
  FLAGS_stderrthreshold = 0;
  google::InitGoogleLogging("analytical_apps");
  google::InstallFailureSignalHandler();

  Init();

  int type = atoi(argv[1]);
  std::string vfile = argv[2];
  std::string input_file = argv[3];

  grape::CommSpec comm_spec;
  comm_spec.Init(MPI_COMM_WORLD);

  if (type < 5) {
    if (comm_spec.fid() == 0) {
      std::cout << "OID = int64_t" << std::endl;
    }
    choose_oid<int64_t>(comm_spec, vfile, input_file, type);
  } else {
    if (comm_spec.fid() == 0) {
      std::cout << "OID = string" << std::endl;
    }
    choose_oid<std::string>(comm_spec, vfile, input_file, type - 5);
  }

  // Finalize();
}