#!/usr/bin/python3

import subprocess
import sys
import os

worker_num = int(sys.argv[1])
hostfile = sys.argv[2]
executable = sys.argv[3]

extra_args = sys.argv[4:]

host_list = []

fin = open(hostfile, 'r')
for line in fin.readlines():
    host_list.append(line.strip().split(':')[0])

host_num = len(host_list)

exe_path = os.path.realpath(executable)
cmd = exe_path + " " + " ".join(extra_args) + " --hostfile " + hostfile + " --worker_num " + str(worker_num) + " --worker_id "

processes = []

cwd = os.getcwd()

for worker_id in range(worker_num):
    worker_cmd = cmd + str(worker_id)
    # ssh = subprocess.Popen(["ssh", "%s" % host_list[worker_id % host_num], "GLOG_v=10 " +  worker_cmd], shell=False)
    ssh = subprocess.Popen(["ssh", "%s" % host_list[worker_id % host_num], worker_cmd], shell=False)
    processes.append(ssh)

for proc in processes:
    proc.wait()
