#!/bin/bash

HOSTFILE=$1

for h in `cat $HOSTFILE`
do
	rsync -av --delete /home/admin/libgrape-lite $h:/home/admin
done
