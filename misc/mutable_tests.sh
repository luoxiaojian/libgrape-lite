#!/bin/bash

rm -rf ./output
mkdir output

# EXE=./run_mutable_app
EXE=./run_mutable_app_int

rm -rf ./extra_tests_output/result_frag_*
mpirun -n 4 $EXE --vfile ../dataset/add_tests/p2p-31.v --efile ../dataset/add_tests/p2p-31.e --application traverse --out_prefix ./extra_tests_output --delta_efile ../dataset/add_tests/p2p-31.e.delta --delta_vfile ../dataset/add_tests/p2p-31.v.delta --directed
cat ./extra_tests_output/result_frag_* | sort -k1n > ./output/add_traverse.out

rm -rf ./extra_tests_output/result_frag_*
mpirun -n 4 $EXE --vfile ../dataset/add_tests/p2p-31.v --efile ../dataset/add_tests/p2p-31.e --application sssp --out_prefix ./extra_tests_output --delta_efile ../dataset/add_tests/p2p-31.e.delta --delta_vfile ../dataset/add_tests/p2p-31.v.delta --directed --sssp_source 6
cat ./extra_tests_output/result_frag_* | sort -k1n > ./output/add_sssp_directed.out
rm -rf ./extra_tests_output/result_frag_*
mpirun -n 4 ./run_app --vfile ../dataset/p2p-31.v --efile ../dataset/p2p-31.e --application sssp --out_prefix ./extra_tests_output --directed --sssp_source 6
cat ./extra_tests_output/result_frag_* | sort -k1n > ./output/immutable_sssp_directed.out

rm -rf ./extra_tests_output/result_frag_*
mpirun -n 4 $EXE --vfile ../dataset/add_tests/p2p-31.v --efile ../dataset/add_tests/p2p-31.e --application sssp --out_prefix ./extra_tests_output --delta_efile ../dataset/add_tests/p2p-31.e.delta --delta_vfile ../dataset/add_tests/p2p-31.v.delta --sssp_source 6
cat ./extra_tests_output/result_frag_* | sort -k1n > ./output/add_sssp_undirected.out
rm -rf ./extra_tests_output/result_frag_*
mpirun -n 4 ./run_app --vfile ../dataset/p2p-31.v --efile ../dataset/p2p-31.e --application sssp --out_prefix ./extra_tests_output --sssp_source 6
cat ./extra_tests_output/result_frag_* | sort -k1n > ./output/immutable_sssp_undirected.out

rm -rf ./extra_tests_output/result_frag_*
mpirun -n 4 $EXE --vfile ../dataset/add_remove_tests/mutable/p2p-31.v --efile ../dataset/add_remove_tests/mutable/p2p-31.e --application traverse --out_prefix ./extra_tests_output --delta_efile ../dataset/add_remove_tests/mutable/p2p-31.e.delta --delta_vfile ../dataset/add_remove_tests/mutable/p2p-31.v.delta --directed
cat ./extra_tests_output/result_frag_* | sort -k1n > ./output/add_remove_traverse.out
rm -rf ./extra_tests_output/result_frag_*
mpirun -n 4 ./run_app --vfile ../dataset/add_remove_tests/base/p2p-31.v --efile ../dataset/add_remove_tests/base/p2p-31.e --application traverse --out_prefix ./extra_tests_output --directed
cat ./extra_tests_output/result_frag_* | sort -k1n > ./output/add_remove_immutable_traverse.out

rm -rf ./extra_tests_output/result_frag_*
mpirun -n 4 $EXE --vfile ../dataset/all_tests/mutable/p2p-31.v --efile ../dataset/all_tests/mutable/p2p-31.e --application traverse --out_prefix ./extra_tests_output --delta_efile ../dataset/all_tests/mutable/p2p-31.e.delta --delta_vfile ../dataset/all_tests/mutable/p2p-31.v.delta --directed
cat ./extra_tests_output/result_frag_* | sort -k1n > ./output/all_traverse.out
rm -rf ./extra_tests_output/result_frag_*
mpirun -n 4 ./run_app --vfile ../dataset/all_tests/base/p2p-31.v --efile ../dataset/all_tests/base/p2p-31.e --application traverse --out_prefix ./extra_tests_output --directed
cat ./extra_tests_output/result_frag_* | sort -k1n > ./output/all_immutable_traverse.out
