#!/bin/bash -e
GRAPE_HOME="$( cd "$(dirname "$0")/.." >/dev/null 2>&1 ; pwd -P )"

# IMMUTABLE_GRAPH=all_tests/base/p2p-31
# MUTABLE_GRAPH=all_tests/mutable/p2p-31
IMMUTABLE_GRAPH=p2p-31
MUTABLE_GRAPH=add_tests/p2p-31

function ExactVerify() {
  if ! cmp $1 $2 > /dev/null 2>&1
  then
    echo "Wrong answer"
    # exit 1
  else
    rm -rf ./extra_tests_output/*
    rm -rf ./extra_tests_tmp.res
  fi
}

function EpsVerify() {
  if ! ./eps_check $1 $2 > /dev/null 2>&1
  then
    echo "Wrong answer"
    # exit 1
  else
    rm -rf ./extra_tests_output/*
    rm -rf ./extra_tests_tmp.res
  fi
}

function WCCVerify() {
  if ! ./wcc_check $1 $2 > /dev/null 2>&1
  then
    echo "Wrong answer"
    # exit 1
  else
    rm -rf ./extra_tests_output/*
    rm -rf ./extra_tests_tmp.res
  fi
}

function RunWithImmutable() {
  NP=$1; shift
  APP=$1; shift

  rm -rf ./extra_tests_output/*
  cmd="mpirun -n ${NP} ./run_app --nosegmented_partition --norebalance --vfile ${GRAPE_HOME}/dataset/${IMMUTABLE_GRAPH}.v --efile ${GRAPE_HOME}/dataset/${IMMUTABLE_GRAPH}.e --application ${APP} --out_prefix ./extra_tests_output $@"
  echo ${cmd}
  eval ${cmd}
  cat ./extra_tests_output/* | sort -k1n > tmp.resA
}

function RunWithMutable() {
  NP=$1; shift
  APP=$1; shift

  rm -rf ./extra_tests_output/*
  cmd="mpirun -n ${NP} ./run_mutable_app --vfile ${GRAPE_HOME}/dataset/${MUTABLE_GRAPH}.v --efile ${GRAPE_HOME}/dataset/${MUTABLE_GRAPH}.e --delta_vfile ${GRAPE_HOME}/dataset/${MUTABLE_GRAPH}.v.delta --delta_efile ${GRAPE_HOME}/dataset/${MUTABLE_GRAPH}.e.delta --application ${APP} --out_prefix ./extra_tests_output $@"
  echo ${cmd}
  eval ${cmd}
  cat ./extra_tests_output/* | sort -k1n > tmp.resB
}

g++ ${GRAPE_HOME}/misc/wcc_check.cc -std=c++11 -O3 -o ./wcc_check
g++ ${GRAPE_HOME}/misc/eps_check.cc -std=c++11 -O3 -o ./eps_check

nproc=$(getconf _NPROCESSORS_ONLN)
if [ ${nproc} -gt 8 ]; then
  nproc=8
fi
proc_list="1 $(seq 2 2 ${nproc})"

for np in ${proc_list}; do
    RunWithImmutable ${np} sssp --sssp_source=6
    RunWithMutable ${np} sssp --sssp_source=6
    ExactVerify tmp.resA tmp.resB

    RunWithImmutable ${np} sssp_auto --sssp_source=6
    RunWithMutable ${np} sssp_auto --sssp_source=6
    ExactVerify tmp.resA tmp.resB

    RunWithImmutable ${np} sssp --sssp_source=6 --directed
    RunWithMutable ${np} sssp --sssp_source=6 --directed
    ExactVerify tmp.resA tmp.resB

    RunWithImmutable ${np} sssp_auto --sssp_source=6 --directed
    RunWithMutable ${np} sssp_auto --sssp_source=6 --directed
    ExactVerify tmp.resA tmp.resB

    RunWithImmutable ${np} bfs --bfs_source=6
    RunWithMutable ${np} bfs --bfs_source=6
    ExactVerify tmp.resA tmp.resB

    RunWithImmutable ${np} bfs_auto --bfs_source=6
    RunWithMutable ${np} bfs_auto --bfs_source=6
    ExactVerify tmp.resA tmp.resB

    RunWithImmutable ${np} bfs --bfs_source=6 --directed
    RunWithMutable ${np} bfs --bfs_source=6 --directed
    ExactVerify tmp.resA tmp.resB

    RunWithImmutable ${np} bfs_auto --bfs_source=6 --directed
    RunWithMutable ${np} bfs_auto --bfs_source=6 --directed
    ExactVerify tmp.resA tmp.resB
    
    RunWithImmutable ${np} pagerank_local_parallel --pr_mr=10 --pr_d=0.85
    RunWithMutable ${np} pagerank_local_parallel --pr_mr=10 --pr_d=0.85
    EpsVerify tmp.resA tmp.resB

    RunWithImmutable ${np} pagerank_auto --pr_mr=10 --pr_d=0.85
    RunWithMutable ${np} pagerank_auto --pr_mr=10 --pr_d=0.85
    EpsVerify tmp.resA tmp.resB

    RunWithImmutable ${np} pagerank_parallel --pr_mr=10 --pr_d=0.85
    RunWithMutable ${np} pagerank_parallel --pr_mr=10 --pr_d=0.85
    EpsVerify tmp.resA tmp.resB

    RunWithImmutable ${np} cdlp --cdlp_mr=10
    RunWithMutable ${np} cdlp --cdlp_mr=10
    ExactVerify tmp.resA tmp.resB

    RunWithImmutable ${np} cdlp_auto --cdlp_mr=10
    RunWithMutable ${np} cdlp_auto --cdlp_mr=10
    ExactVerify tmp.resA tmp.resB

    RunWithImmutable ${np} lcc
    RunWithMutable ${np} lcc
    ExactVerify tmp.resA tmp.resB

    RunWithImmutable ${np} lcc_auto
    RunWithMutable ${np} lcc_auto
    ExactVerify tmp.resA tmp.resB

    RunWithImmutable ${np} wcc
    RunWithMutable ${np} wcc
    WCCVerify tmp.resA tmp.resB

    RunWithImmutable ${np} wcc_auto
    RunWithMutable ${np} wcc_auto
    WCCVerify tmp.resA tmp.resB
done
