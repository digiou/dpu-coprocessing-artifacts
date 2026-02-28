#!/usr/bin/env python3

import argparse, lz4.block, os, pathlib, shutil, struct, subprocess, random
from datetime import datetime

def parse_args():
    parser = argparse.ArgumentParser(description="DOCA LZ4 throughput experiments.")
    parser.add_argument("--bf3", action="store_true", help="Use BlueField 3 device config.")
    parser.add_argument("--on_dpu", action="store_true", help="If script runs on DPU or Host.")
    return parser.parse_args()

def make_lz4_fs(num_files: int=1024, block_size: int=64*1024, 
                root_dir: pathlib.Path=pathlib.Path("/dev/shm")) :
    cmp_dir = root_dir / "lz4_raw"
    shutil.rmtree(path=cmp_dir, ignore_errors=True)
    cmp_dir.mkdir(parents=True, exist_ok=True)
    fs_path = root_dir / "lz4_raw_stream.fs"
    fs_path.unlink(missing_ok=True)

    for i in range(num_files):
        plain = os.urandom(block_size)  # random data
        # Each call is one independent compressed block 
        # store_size=False ⇒ no 4-byte size footer
        comp_block = lz4.block.compress(
            plain, 
            mode="high_compression",  # doesn't matter in decompress
            store_size=False  # keep the raw
        )
        stream = struct.pack("<I", len(comp_block)) + comp_block
        (cmp_dir / f"{i:04}.lz4s").write_bytes(stream)

    with fs_path.open("w") as fs:
        for f in sorted(cmp_dir.glob("*.lz4s")):
            fs.write(str(f) + "\n")
        print(f"Wrote file with block size={block_size}")

def execute_experiment(device_address: str, device_str: str, on_dpu: bool):
    # experiment parameters
    if device_str == "bf2":
        job_sizes = [2**i for i in range(6, 28)]  # 64B to 128MiB
        thread_counts = [1, 2, 4, 8]
        if on_dpu:
            thread_counts[-1] = 7
    else:
        job_sizes = [2**i for i in range(6, 22)]  # 64B to 2MiB
        # 2MiB (2_097_152) of input can expand to larger sizes due to lz4 prefix
        # use safest largest size (2_088_940) instead
        job_sizes[-1] = 2_088_940
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
        device_str += "-host"
    else:
        companion = f"proto=tcp,user=ubuntu,port=12345,addr=192.168.100.2,dev=03:00.0,rep={device_address}"
    timestamp = datetime.now().strftime("%Y-%m-%d-%H-%M-%S")
    csv_output_path = f"/tmp/lz4-results-{device_str}-{timestamp}.csv"

    for job_size in job_sizes:
        make_lz4_fs(block_size=job_size)
        for threads in thread_counts:
            for batch in batch_sizes:
                cmd = [
                    doca_bench_path,
                    "--mode", "throughput",
                    "--core-list", core_list,
                    "--core-count", str(threads),
                    "--threads-per-core", "1",
                    "--pipeline-steps", "doca_compress::decompress",
                    "--device", device_address if not on_dpu else "03:00.0",
                    "--data-provider", "file-set",
                    "--data-provider-input-file", "/dev/shm/lz4_raw_stream.fs",
                    "--job-output-buffer-size", str(job_size),
                    "--data-provider-job-count", str(batch),
                    "--attribute", 'doca_compress.algorithm="lz4_stream"',
                    "--attribute", 'doca_compress.algorithm.has_block_checksum=false',
                    "--attribute", 'doca_compress.algorithm.are_blocks_independent=true',
                    "--run-limit-seconds", "5",
                    "--csv-append-mode",
                    "--csv-output-file", csv_output_path,
                ]
                try:
                    result = subprocess.run(cmd, capture_output=True, text=True, check=True)
                except subprocess.CalledProcessError as e:
                    print(f"Error: {e}")
                    continue

if __name__ == "__main__":
    args = parse_args()
    args.bf3, args.bf2 = True, False  # eff it
    device_add, device_str = ("87:00.0", "bf3") if args.bf3 else ("41:00.0", "bf2")
    print(f"Running on {device_str}, dev={device_add}, on_dpu={args.on_dpu}")
    execute_experiment(device_address=device_add, device_str=device_str, 
                       on_dpu=args.on_dpu)
