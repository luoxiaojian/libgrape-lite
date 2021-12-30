#!/bin/bash

EXE=$1
HOSTFILE=$2
DATASETS_DIR=$3

for h in `cat $HOSTFILE`
do
  scp /home/admin/.ossutilconfig $h:/home/admin
done

HOSTNUM=`wc -l < $HOSTFILE`

mpirun -n $HOSTNUM -f $HOSTFILE rm -rf $DATASETS_DIR
mpirun -n $HOSTNUM -f $HOSTFILE mkdir $DATASETS_DIR
# mpirun -n $HOSTNUM -f $HOSTFILE $EXE cp oss://grape-data/com-friendster/evr/com-friendster.v $DATASETS_DIR/
mpirun -n $HOSTNUM -f $HOSTFILE $EXE cp --recursive oss://xiaojian-repo/mutate/cf-7-1-1-1 $DATASETS_DIR/
mpirun -n $HOSTNUM -f $HOSTFILE $EXE cp --recursive oss://xiaojian-repo/mutate/cf-17-1-1-1 $DATASETS_DIR/
mpirun -n $HOSTNUM -f $HOSTFILE $EXE cp --recursive oss://xiaojian-repo/mutate/cf-47-1-1-1 $DATASETS_DIR/
mpirun -n $HOSTNUM -f $HOSTFILE $EXE cp --recursive oss://xiaojian-repo/mutate/cf-97-1-1-1 $DATASETS_DIR/
