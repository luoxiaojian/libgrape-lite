#!/usr/bin/python3

import subprocess
import sys
import os

worker_num = int(sys.argv[1])
executable = sys.argv[2]

extra_args = sys.argv[3:]

host_list = []

exe_path = os.path.realpath(executable)
cmd = exe_path + " " + " ".join(extra_args) + " --worker_num" + str(worker_num) + " --worker_id"

processes = []

cwd = os.getcwd()

for worker_id in range(worker_num):
    # fout = open("./out_" + str(worker_id), 'wb')
    # p = subprocess.Popen([executable] + extra_args + ["--worker_num", str(worker_num), "--worker_id"] + [str(worker_id)], stdout=fout, stderr=fout, shell=False)
    p = subprocess.Popen([executable] + extra_args + ["--worker_num", str(worker_num), "--worker_id"] + [str(worker_id)], shell=False)
    processes.append(p)

for proc in processes:
    proc.wait()
