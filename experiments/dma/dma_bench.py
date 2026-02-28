#!/usr/bin/env python3

import argparse
import subprocess
from datetime import datetime

def parse_args():
    parser = argparse.ArgumentParser(description="DOCA DMA throughput experiments.")
    parser.add_argument("--bf3", action="store_true", help="Use BlueField 3 device config.")
    parser.add_argument("--on_dpu", action="store_true", help="If script runs on DPU or Host.")
    return parser.parse_args()

def execute_experiment(device_address: str, device_str: str, on_dpu: bool):
    # experiment parameters
    if device_str == "bf2":
        job_sizes = [2**i for i in range(6, 28)]  # 64B to 128MiB
        thread_counts = [1, 2, 4, 8]
        if on_dpu:
            thread_counts[-1] = 7
    else:
        job_sizes = [2**i for i in range(6, 22)]  # 64B to 2MiB
        thread_counts = [1, 2, 4, 8, 15]
    batch_sizes = [1, 2, 4, 8, 16, 32, 64, 128, 256]
    direction_flag = "--use-remote-output-buffers"

    # system-specific settings
    doca_bench_path = "/opt/mellanox/doca/tools/doca_bench"
    core_list = "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15"  # host or BF3 cores
    if on_dpu and device_str == "bf2":
        core_list = "1,2,3,4,5,6,7"

    # system parameters
    # companion = f"proto=tcp,user=ubuntu,port=12345,addr=192.168.100.2,dev=03:00.0,rep={device_address}"
    if on_dpu:
        user = "dimitrios"
        if device_str == "bf3":
            user += "-ldap"
        companion = f"proto=tcp,user={user},port=12345,addr=192.168.100.1,dev={device_address}"
    else:
        companion = f"proto=tcp,user=ubuntu,port=12345,addr=192.168.100.2,dev=03:00.0,rep={device_address}"
        device_str += "-host"
    timestamp = datetime.now().strftime("%Y-%m-%d-%H-%M-%S")
    csv_output_path = f"/tmp/dma-results-{device_str}-{timestamp}.csv"

    for job_size in job_sizes:
        for threads in thread_counts:
            for batch in batch_sizes:
                cmd = [
                    doca_bench_path,
                    "--mode", "throughput",
                    "--pipeline-steps", "doca_dma",
                    "--device", device_address if not on_dpu else "03:00.0",
                    "--data-provider", "random-data",
                    "--uniform-job-size", str(job_size),
                    "--job-output-buffer-size", str(job_size),
                    "--data-provider-job-count", str(batch),
                    direction_flag,
                    "--companion-connection-string", companion,
                    "--core-count", str(threads),
                    "--core-list", core_list,
                    "--run-limit-seconds", "5",
                    "--csv-append-mode",
                    "--csv-output-file", csv_output_path,
                ]
                if on_dpu:
                    cmd += "--representor", "03:00.0",
                try:
                    result = subprocess.run(cmd, capture_output=True, text=True, check=True)
                except subprocess.CalledProcessError as e:
                    print(f"Error: {e}")
                    continue

if __name__ == "__main__":
    args = parse_args()
    device_add, device_str = ("87:00.0", "bf3") if args.bf3 else ("41:00.0", "bf2")
    print(f"Running on {device_str}, dev={device_add}, on_dpu={args.on_dpu}")
    execute_experiment(device_address=device_add, device_str=device_str, 
                       on_dpu=args.on_dpu)
