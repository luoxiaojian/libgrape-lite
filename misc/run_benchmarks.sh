#!/bin/bash

echo "immutable 7"
mpirun -np 4 -f /home/admin/libgrape-lite/build/hostfile /home/admin/libgrape-lite/build/benchmark_app --directed --efile /home/admin/datasets/cf-7-1-1-1/cf.e.immutable --out_prefix ./immutable_output_7_directed --sssp_source 4
echo "mutable 7 10"
mpirun -np 4 -f /home/admin/libgrape-lite/build/hostfile /home/admin/libgrape-lite/build/benchmark_app --directed --efile /home/admin/datasets/cf-7-1-1-1/cf.e.mutable_base --delta_efile_prefix /home/admin/datasets/cf-7-1-1-1/part_10/cf --delta_efile_part_num 10 --out_prefix ./mutable_output_7_10_directed --sssp_source 4
echo "immutable 17"
mpirun -np 4 -f /home/admin/libgrape-lite/build/hostfile /home/admin/libgrape-lite/build/benchmark_app --directed --efile /home/admin/datasets/cf-17-1-1-1/cf.e.immutable --out_prefix ./immutable_output_17_directed --sssp_source 4
echo "mutable 17 10"
mpirun -np 4 -f /home/admin/libgrape-lite/build/hostfile /home/admin/libgrape-lite/build/benchmark_app --directed --efile /home/admin/datasets/cf-17-1-1-1/cf.e.mutable_base --delta_efile_prefix /home/admin/datasets/cf-17-1-1-1/part_10/cf --delta_efile_part_num 10 --out_prefix ./mutable_output_17_10_directed --sssp_source 4
echo "immutable 47"
mpirun -np 4 -f /home/admin/libgrape-lite/build/hostfile /home/admin/libgrape-lite/build/benchmark_app --directed --efile /home/admin/datasets/cf-47-1-1-1/cf.e.immutable --out_prefix ./immutable_output_47_directed --sssp_source 4
echo "mutable 47 10"
mpirun -np 4 -f /home/admin/libgrape-lite/build/hostfile /home/admin/libgrape-lite/build/benchmark_app --directed --efile /home/admin/datasets/cf-47-1-1-1/cf.e.mutable_base --delta_efile_prefix /home/admin/datasets/cf-47-1-1-1/part_10/cf --delta_efile_part_num 10 --out_prefix ./mutable_output_47_10_directed --sssp_source 4
echo "immutable 97"
mpirun -np 4 -f /home/admin/libgrape-lite/build/hostfile /home/admin/libgrape-lite/build/benchmark_app --directed --efile /home/admin/datasets/cf-97-1-1-1/cf.e.immutable --out_prefix ./immutable_output_97_directed --sssp_source 4
echo "mutable 97 10"
mpirun -np 4 -f /home/admin/libgrape-lite/build/hostfile /home/admin/libgrape-lite/build/benchmark_app --directed --efile /home/admin/datasets/cf-97-1-1-1/cf.e.mutable_base --delta_efile_prefix /home/admin/datasets/cf-97-1-1-1/part_10/cf --delta_efile_part_num 10 --out_prefix ./mutable_output_97_10_directed --sssp_source 4

echo "mutable 7 100"
mpirun -np 4 -f /home/admin/libgrape-lite/build/hostfile /home/admin/libgrape-lite/build/benchmark_app --directed --efile /home/admin/datasets/cf-7-1-1-1/cf.e.mutable_base --delta_efile_prefix /home/admin/datasets/cf-7-1-1-1/part_100/cf --delta_efile_part_num 100 --out_prefix ./mutable_output_7_100_directed --sssp_source 4
echo "mutable 17 100"
mpirun -np 4 -f /home/admin/libgrape-lite/build/hostfile /home/admin/libgrape-lite/build/benchmark_app --directed --efile /home/admin/datasets/cf-17-1-1-1/cf.e.mutable_base --delta_efile_prefix /home/admin/datasets/cf-17-1-1-1/part_100/cf --delta_efile_part_num 100 --out_prefix ./mutable_output_17_100_directed --sssp_source 4
echo "mutable 47 100"
mpirun -np 4 -f /home/admin/libgrape-lite/build/hostfile /home/admin/libgrape-lite/build/benchmark_app --directed --efile /home/admin/datasets/cf-47-1-1-1/cf.e.mutable_base --delta_efile_prefix /home/admin/datasets/cf-47-1-1-1/part_100/cf --delta_efile_part_num 100 --out_prefix ./mutable_output_47_100_directed --sssp_source 4
echo "mutable 97 100"
mpirun -np 4 -f /home/admin/libgrape-lite/build/hostfile /home/admin/libgrape-lite/build/benchmark_app --directed --efile /home/admin/datasets/cf-97-1-1-1/cf.e.mutable_base --delta_efile_prefix /home/admin/datasets/cf-97-1-1-1/part_100/cf --delta_efile_part_num 100 --out_prefix ./mutable_output_97_100_directed --sssp_source 4

echo "mutable 7 1"
mpirun -np 4 -f /home/admin/libgrape-lite/build/hostfile /home/admin/libgrape-lite/build/benchmark_app --directed --efile /home/admin/datasets/cf-7-1-1-1/cf.e.mutable_base --delta_efile_prefix /home/admin/datasets/cf-7-1-1-1/part_1/cf --delta_efile_part_num 1 --out_prefix ./mutable_output_7_1_directed --sssp_source 4
echo "mutable 17 1"
mpirun -np 4 -f /home/admin/libgrape-lite/build/hostfile /home/admin/libgrape-lite/build/benchmark_app --directed --efile /home/admin/datasets/cf-17-1-1-1/cf.e.mutable_base --delta_efile_prefix /home/admin/datasets/cf-17-1-1-1/part_1/cf --delta_efile_part_num 1 --out_prefix ./mutable_output_17_1_directed --sssp_source 4
echo "mutable 47 1"
mpirun -np 4 -f /home/admin/libgrape-lite/build/hostfile /home/admin/libgrape-lite/build/benchmark_app --directed --efile /home/admin/datasets/cf-47-1-1-1/cf.e.mutable_base --delta_efile_prefix /home/admin/datasets/cf-47-1-1-1/part_1/cf --delta_efile_part_num 1 --out_prefix ./mutable_output_47_1_directed --sssp_source 4
echo "mutable 97 1"
mpirun -np 4 -f /home/admin/libgrape-lite/build/hostfile /home/admin/libgrape-lite/build/benchmark_app --directed --efile /home/admin/datasets/cf-97-1-1-1/cf.e.mutable_base --delta_efile_prefix /home/admin/datasets/cf-97-1-1-1/part_1/cf --delta_efile_part_num 1 --out_prefix ./mutable_output_97_1_directed --sssp_source 4
