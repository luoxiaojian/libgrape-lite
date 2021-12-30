#!/bin/bash

EFILE_PATH=$1
DATASET_DIR=$2

function Generate() {
  EFILE=$1; shift
  DS_DIR=$1; shift
  GRAPH_NAME=$1; shift
  BASE=$1; shift
  ADD_RATE=$1; shift
  REMOVE_RATE=$1; shift
  UPDATE_RATE=$1; shift

  LOCATION=$DS_DIR/$GRAPH_NAME-$BASE-$ADD_RATE-$REMOVE_RATE-$UPDATE_RATE
  mkdir -p $LOCATION
  ./delta_generator $EFILE $LOCATION/$GRAPH_NAME.e $BASE $ADD_RATE $REMOVE_RATE $UPDATE_RATE
  lineNum=`wc -l < $LOCATION/$GRAPH_NAME.e.mutable_delta`
  shuf $LOCATION/$GRAPH_NAME.e.mutable_delta > $LOCATION/$GRAPH_NAME.e.mutable_delta.tmp
  mv $LOCATION/$GRAPH_NAME.e.mutable_delta.tmp $LOCATION/$GRAPH_NAME.e.mutable_delta
  for part_num in "$@"
  do
    echo $part_num
    mkdir -p $LOCATION/part_$part_num
    ./delta_spliter $LOCATION/$GRAPH_NAME.e.mutable_delta $LOCATION/part_$part_num/$GRAPH_NAME $lineNum $part_num
  done
}

Generate $EFILE_PATH $DATASET_DIR cf 7 1 1 1 100 10 1
Generate $EFILE_PATH $DATASET_DIR cf 17 1 1 1 100 10 1
Generate $EFILE_PATH $DATASET_DIR cf 47 1 1 1 100 10 1
Generate $EFILE_PATH $DATASET_DIR cf 97 1 1 1 100 10 1
