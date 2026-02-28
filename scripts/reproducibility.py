#!/usr/bin/python3
import argparse
import csv
import glob
import json
import logging
import os
import re
import shutil
import subprocess

from pathlib import Path

import pandas as pd
import numpy as np
import seaborn as sns
import matplotlib.pyplot as plt


def run_dma() -> None:
    logger.info("Need SUDO for PCIe IPs on DPUs (tmfifo_net1 on BF3, tmfifo_net0 on BF2)")
    try:
        subprocess.run(
            ["sudo", "ifconfig", "tmfifo_net1", "192.168.100.1/24"],
            check=True
        )
        logger.info("✔ tmfifo_net1 set up")
    except subprocess.CalledProcessError as e: # sudo or ifconfig returned non-zero
        logger.warning(f"Failed to assign BF2 IP (wrong password or command error: {e.returncode}).")
        return
    except FileNotFoundError: # "ifconfig" not installed (many distros ship only `ip`)
        logger.warning("`ifconfig` not found. Exiting...")
        return
    try:
        subprocess.run(  # -tt forces a TTY so sudo can ask for the password
            ["ssh", "-tt", "cloud-48", "sudo", "ifconfig", "tmfifo_net0", "192.168.100.1/24"],
            check=True
        )
        logger.info("✔ tmfifo_net0 set up")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Failed to assign BF2 IP (wrong password or command error: {e.returncode}).")
        return
    logger.info("Running dma experiments for host-BF3, BF3-host, and host-BF2...")
    os.chdir("../experiments/dma")  # run local experiment from Host to BF3
    try:
        subprocess.run(
            ["python3", "dma_bench.py", "--bf3"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
    except FileNotFoundError:
        logger.warning("dma_bench.py or python3 not found in PATH")
        return
    except subprocess.CalledProcessError as e:
        logger.warning(f"dma_bench.py failed with exit code {e.returncode}")
        return
    # copy results from host
    host_results = max(list(Path("/tmp/").expanduser().glob("dma-results*host*")), 
                       key=lambda p: p.stat().st_mtime)
    shutil.move(str(host_results), "../../scripts/tex/figures/results/bf3-host/dma-results-latest.csv")
    logger.info("✔ host-BF3 completed")
    try:  # copy python file to DPU
        subprocess.run(
            ["scp", "dma_bench.py", "bf-pcie:/tmp/dma_bench.py"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
    except subprocess.CalledProcessError as e:
        logger.warning(f"scp failed with exit code {e.returncode}")
    try:  # execute python file on DPU
        subprocess.run(
            ["ssh", "bf-pcie", "python3 /tmp/dma_bench.py --bf3 --on_dpu"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
    except subprocess.CalledProcessError as e:
        logger.warning(f"Remote command failed on bf-pcie with exit code {e.returncode}")
        return
    except FileNotFoundError:
        logger.warning("ssh not found on this system")
        return
    # copy BF3 results back to host
    result = subprocess.run(  # Ask the latest matching file
        ["ssh", "bf-pcie", "ls -t /tmp/dma-results* 2>/dev/null | head -n1"],
        check=True, capture_output=True, text=True,
    )
    latest_file = result.stdout.strip()
    if latest_file:  # Copy it back
        subprocess.run(
            ["scp", f"bf-pcie:{latest_file}", "../../scripts/tex/figures/results/bf3/dma-results-latest.csv"],
            check=True,)
    logger.info("✔ BF3-host completed")
    try:  # copy python file to BF2 Host
        subprocess.run(
            ["scp", "dma_bench.py", "cloud-48:/tmp/dma_bench.py"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
    except subprocess.CalledProcessError as e:
        logger.warning(f"scp failed with exit code {e.returncode}")
    try:  # execute python file on BF2 Host
        subprocess.run(
            ["ssh", "cloud-48", "python3 /tmp/dma_bench.py"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
    except subprocess.CalledProcessError as e:
        logger.warning(f"Remote command failed on cloud-48 with exit code {e.returncode}")
        return
    # copy BF2 Host results back to host
    result = subprocess.run(  # Ask the latest matching file
        ["ssh", "cloud-48", "ls -t /tmp/dma-results*host* 2>/dev/null | head -n1"],
        check=True, capture_output=True, text=True,
    )
    latest_file = result.stdout.strip()
    if latest_file:  # Copy it back
        subprocess.run(
            ["scp", f"cloud-48:{latest_file}", "../../scripts/tex/figures/results/bf2/dma-results-latest.csv"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    logger.info("✔ host-BF2 completed")
    os.chdir('../../scripts')

def figures_dma() -> None:
    # Base directories
    bf2_host_file = next(Path("tex/figures/results/bf2").glob("dma-results-latest.csv"), None)
    bf3_file = next(Path("tex/figures/results/bf3").glob("dma-results-latest.csv"), None)
    bf3_host_file = next(Path("tex/figures/results/bf3-host").glob("dma-results-latest.csv"), None)
    # Conversion maps
    byte_unit_multipliers = {
        "B/s": 1,
        "KiB/s": 2**10,
        "MiB/s": 2**20,
        "GiB/s": 2**30,
        "TiB/s": 2**40,
        "kb/s": 10**3 / 8,
        "Mb/s": 10**6 / 8,
        "Gb/s": 10**9 / 8,
        "Tb/s": 10**12 / 8,
        "Kib/s": 2**10 / 8,
        "Mib/s": 2**20 / 8,
        "Gib/s": 2**30 / 8,
        "Tib/s": 2**40 / 8,
    }
    # Check if files were found
    if not bf2_host_file or not bf3_file or not bf3_host_file:
        raise FileNotFoundError("One or some result files not found. Check filenames or paths.")
    def parse_bytes_throughput(value: str) -> float:
        try:
            num, unit = value.strip().split()
            return float(num) * byte_unit_multipliers[unit]
        except Exception:
            return np.nan

    def parse_ops_throughput(value: str) -> float:
        try:
            num, _ = value.strip().split()
            return float(num)
        except Exception:
            return np.nan

    def load_and_parse_csv(file_path: Path, platform_label: str) -> pd.DataFrame:
        df = pd.read_csv(file_path, low_memory=False)
        df["platform"] = platform_label
        # Parse throughput fields during load
        df["throughput_data_bytes_per_sec"] = df["stats.output.throughput.bytes"].apply(parse_bytes_throughput)
        df["throughput_ops_per_sec"] = df["stats.output.throughput.rate"].apply(parse_ops_throughput)
        return df
    
    # Load csvs from disk
    df_bf2 = load_and_parse_csv(bf2_host_file, "HostBF2")
    df_bf3 = load_and_parse_csv(bf3_file, "BlueField3")
    df_host = load_and_parse_csv(bf3_host_file, "HostBF3")
    df_all = pd.concat([df_bf2, df_bf3, df_host], ignore_index=True)

    # List of parameter combinations (row, col), inferred third is the one to average over
    param_combinations = [
        ("cfg.data_provider.output_buffer_size", "cfg.core_count"),
        ("cfg.data_provider.output_buffer_size", "cfg.data_provider_job_count"),
        ("cfg.core_count", "cfg.data_provider_job_count"),
    ]
    # use only these combinations for paper
    param_combinations = [
        ("cfg.data_provider.output_buffer_size", "cfg.data_provider_job_count"),
    ]

    # Throughput column
    throughput_col = "throughput_data_bytes_per_sec"

    def format_throughput_bytes(val):
        units = ['B/s', 'KiB/s', 'MiB/s', 'GiB/s', 'TiB/s']
        scale = 1024.0
        unit_index = 0
        while val >= scale and unit_index < len(units) - 1:
            val /= scale
            unit_index += 1
        return int(f"{val:.0f}"), units[unit_index]

    def format_bytes_label(val):
        scaled, unit = format_throughput_bytes(val)
        return f"{int(scaled)} {unit.replace('/s','')}" if scaled >= 1 else f"{int(val)} B"

    # Main plotting function
    def plot_heatmap(df, row_var, col_var, value_var, platform, aggfunc='mean', cmap='Greys_r'):
        all_params = ["cfg.data_provider.output_buffer_size", "cfg.core_count", "cfg.data_provider_job_count"]
        third_param = [p for p in all_params if p not in (row_var, col_var)][0]

        # Aggregate and scale values
        pivot = df.pivot_table(
            index=row_var,
            columns=col_var,
            values=value_var,
            aggfunc=aggfunc
        )

        # Determine best unit for the colorbar (from max value)
        max_val_raw = pivot.max().max()
        scaled_val, unit_label = format_throughput_bytes(max_val_raw)
        scaled_pivot = pivot / (max_val_raw / scaled_val)

        # Create the heatmap
        plt.figure(figsize=(9, 6))
        ax = sns.heatmap(scaled_pivot, annot=False, cmap=cmap, cbar=False)
        plt.gca().invert_yaxis()

        # Find location and config of max value
        max_idx = pivot.stack().idxmax()
        row_val, col_val = max_idx
        row_idx = list(pivot.index).index(row_val)
        col_idx = list(pivot.columns).index(col_val)
        max_val = pivot.loc[row_val, col_val]

        # Find matching row in original df to extract third param value
        matching_row = df[
            (df[row_var] == row_val) &
            (df[col_var] == col_val)
        ].sort_values(by=value_var, ascending=False).iloc[0]
        third_val = matching_row[third_param]

        # Format throughput + third param
        throughput_str, unit = format_throughput_bytes(max_val)
        if "core" in third_param:
            third_str = f"{int(third_val)} C"
        elif "job_count" in third_param:
            third_str = f"{int(third_val)} J"
        elif "output_buffer_size" in third_param:
            size_val, size_unit = format_throughput_bytes(third_val)
            third_str = f"{int(size_val)} {size_unit.replace('/s','')}"
        else:
            third_str = str(third_val)

        label_str = f"{throughput_str:.0f} {unit}\n{third_str}"
        label_str = f"{throughput_str:.0f}"

        # Annotate best-performing cell
        ax.text(col_idx + 0.5, row_idx + 0.5, label_str,
                ha='center', va='center',
                color='black', fontsize=36, fontweight='bold')
        

        # Format tick labels if output_buffer_size is on an axis
        if row_var == "cfg.data_provider.output_buffer_size":
            ax.set_yticklabels([format_bytes_label(v) for v in pivot.index])
        if col_var == "cfg.data_provider.output_buffer_size":
            ax.set_xticklabels([format_bytes_label(v) for v in pivot.columns], rotation=45, ha="right")

        # Set labels and title
        if "job_count" in col_var:
            plt.xlabel("Batch Size")
        else:
            plt.xlabel(col_var)
        if "output_buffer_size" in row_var:
            plt.ylabel("Buffer Size")
        else:
            plt.ylabel(row_var)
        plt.title("Throughput (GiB/s)")
        plt.tight_layout()
        plt.savefig(f"tex/figures/dma-buf-vs-jobs-{platform}.pdf", format="pdf", bbox_inches="tight", dpi=1200)
        plt.close()

    # Filter some dimensions for smaller plots in pdf
    max_buffer_size = 2 * 1024 * 1024  # 2 MiB in bytes
    max_job_count = 16
    selected_buffer_sizes = [2097152, 1048576, 524288, 131072, 65536, 8192]

    plt.rcParams.update({
        "font.size": 45,
        "axes.titlesize": 42,
        "axes.labelsize": 42,
        "xtick.labelsize": 42,
        "ytick.labelsize": 42,
    })

    # Loop over platforms and parameter combinations
    for platform in df_all["platform"].unique():
        platform_df = df_all[df_all["platform"] == platform]
        platform_df = platform_df[platform_df["cfg.data_provider.output_buffer_size"] <= max_buffer_size]
        platform_df = platform_df[platform_df["cfg.data_provider_job_count"] <= max_job_count]
        platform_df = platform_df[platform_df["cfg.data_provider.output_buffer_size"].isin(selected_buffer_sizes)]
        for row_var, col_var in param_combinations:
            plot_heatmap(platform_df, row_var, col_var, throughput_col, platform)

def run_compress() -> None:
    logger.info("Running compress experiments for bf-2, bf-3, bf2-host, and bf3-host...")
    os.chdir("../experiments/local-compress")  # bf3-host (CPU)
    try:
        subprocess.run(
            ["bash", "measure-cpu.sh", "--bf3-host"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf3-host CPU completed")
    except FileNotFoundError:
        logger.warning("measure-cpu.sh not found in PATH")
        return
    except subprocess.CalledProcessError as e:
        logger.warning(f"measure-cpu.sh failed with exit code {e.returncode}")
        return
    try:  # bf2-host (CPU)
        subprocess.run( # TODO: adjust script dir to artifacts
            ["ssh", "cloud-48", "cd dpu-paper/experiments/local-compress && bash measure-cpu.sh --bf2-host"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf2-host CPU completed")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Remote command failed on bf2-host with exit code {e.returncode}")
        return
    try:  # bf3-dpu (CPU)
        subprocess.run( # TODO: adjust script dir to artifacts
            ["ssh", "bf-pcie", "cd dpu-paper/experiments/local-compress && bash measure-cpu.sh --bf3-dpu"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf3-dpu CPU completed")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Remote command failed on bf3-dpu with exit code {e.returncode}")
        return
    try:  # bf2-dpu (CPU)
        subprocess.run( # TODO: adjust script dir to artifacts
            ["ssh", "cloud-48", "ssh bf-pcie 'cd dpu-paper/experiments/local-compress && bash measure-cpu.sh --bf2-dpu'"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf2-dpu CPU completed")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Remote command failed on bf2-dpu with exit code {e.returncode}")
        return
    try:  # bf3 (DOCA)
        subprocess.run( # TODO: adjust script dir to artifacts
            ["ssh", "bf-pcie", "cd dpu-paper/experiments/local-compress && bash measure-dpu.sh --v3"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf3 (DOCA) completed")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Remote command failed on bf3 (DOCA) with exit code {e.returncode}")
        return
    try:  # bf2 (DOCA)
        subprocess.run( # TODO: adjust script dir to artifacts
            ["ssh", "cloud-48", "ssh bf-pcie 'cd dpu-paper/experiments/local-compress && bash measure-dpu.sh --v2'"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf2 (DOCA) completed")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Remote command failed on bf2 (DOCA) with exit code {e.returncode}")
        return
    os.chdir("../..") # root of proj
    # gather data for CPUs
    # bf3-cpu
    src_dir = "experiments/local-compress/build/results/bf3-host"
    dst_dir = "scripts/tex/figures/results/bf3-host"
    for entry in os.listdir(src_dir):
        if entry.endswith(".csv"):
            shutil.move(os.path.join(src_dir, entry), os.path.join(dst_dir, entry))
    try:  # bf2-host
        subprocess.run(
            ["scp", "cloud-48:dpu-paper/experiments/local-compress/build/results/bf2-host/cpu*.csv",
             "scripts/tex/figures/results/bf2-host/"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
    except subprocess.CalledProcessError as e:
        logger.warning(f"Copying compress results from bf2-host failed with exit code {e.returncode}")
        return
    try:  # bf3-dpu
        subprocess.run(
            ["scp", "bf-pcie:dpu-paper/experiments/local-compress/build/results/bf3-dpu/cpu*.csv",
             "scripts/tex/figures/results/bf3/"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
    except subprocess.CalledProcessError as e:
        logger.warning(f"Copying compress results from bf2-host failed with exit code {e.returncode}")
        return
    try:  # bf2-dpu (double ssh) from cloud-48
        subprocess.run(
            ["ssh", "cloud-48", "cd dpu-paper && scp bf-pcie:dpu-paper/experiments/local-compress/build/results/bf2-dpu/cpu*.csv scripts/tex/figures/results/bf2/"], 
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    except subprocess.CalledProcessError as e:
        logger.warning(f"Copying compress results from bf2-dpu to cloud-48 failed with exit code {e.returncode}")
        return
    try:  # bf2-dpu (double ssh) from cloud-48
        subprocess.run(
            ["scp", "cloud-48:dpu-paper/scripts/tex/figures/results/bf2/cpu*.csv",
             "scripts/tex/figures/results/bf2/"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
    except subprocess.CalledProcessError as e:
        logger.warning(f"Copying compress results for bf2-dpu from cloud-48 failed with exit code {e.returncode}")
        return
    # gather data for DOCA
    try:  # bf3-dpu
        subprocess.run(
            ["scp", "bf-pcie:dpu-paper/experiments/local-compress/build/results/doca/*.csv",
             "scripts/tex/figures/results/bf3/"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
    except subprocess.CalledProcessError as e:
        logger.warning(f"Copying DOCA results from bf3 failed with exit code {e.returncode}")
        return
    try:  # bf2-dpu (double ssh) from cloud-48
        subprocess.run(
            ["ssh", "cloud-48", "cd dpu-paper && scp bf-pcie:dpu-paper/experiments/local-compress/build/results/doca/*.csv scripts/tex/figures/results/bf2/"], 
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    except subprocess.CalledProcessError as e:
        logger.warning(f"Copying DOCA results from bf2 to cloud-48 failed with exit code {e.returncode}")
        return
    try:  # bf2-dpu (double ssh) from cloud-48
        subprocess.run(
            ["scp", "cloud-48:dpu-paper/scripts/tex/figures/results/bf2/measurements*.csv",
             "scripts/tex/figures/results/bf2/"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
    except subprocess.CalledProcessError as e:
        logger.warning(f"Copying DOCA results for bf2 from cloud-48 failed with exit code {e.returncode}")
        return
    os.chdir('scripts')
    logger.info("✔ gathered compress data")

def figures_compress() -> None:
    cols = [
        'Configuration', 'Dataset', 'Filename', 'Input Size (bytes)', 'Output Size (bytes)',
        'DOCA Buffer (bytes)', 'DOCA Buffers (num)', 'Total Time (seconds)',
        'Task Time (seconds)', 'Ctx Task Time (seconds)', 'Mem Time (seconds)', 'Dev Time (seconds)',
        'Cb Task Time (seconds)', 'Cb Task Throughput (MB/s)', 'Task Throughput (MB/s)',
        'Cb Task Start Latency (seconds)', 'Cb Task End Latency (seconds)'
    ]
    base_path = 'tex/figures/results'
    doca_per_device_results = {"bf2": {"dflt": []}, "bf3": {"dflt": [], "lz4": []}}
    for bf_dir in doca_per_device_results.keys():
        cwd_dir = base_path + os.sep + bf_dir
        for algo in doca_per_device_results[bf_dir].keys():
            # Use glob to find the 'orig' CSV file in the latest directory
            orig_csv_path = glob.glob(f'{cwd_dir}/measurements-orig*{algo}.csv')[0]
            orig_df = pd.read_csv(orig_csv_path, header=None, names=cols)
            orig_df.index.name = 'orig'
            # Use glob to find the 'variable' CSV file
            variable_csv_path = glob.glob(f'{cwd_dir}/measurements-variable*{algo}.csv')[0]
            var_df = pd.read_csv(variable_csv_path, header=None, names=cols)
            var_df.index.name = 'var'

            for current_df in [orig_df, var_df]:
                if (current_df.drop(columns=['Configuration', 'Dataset', 'Filename']).values < 0).any():
                    logger.error("Rows with errors")

                # Drop the 'unnecesary' columns since we are focusing on a single dataset for now
                current_df = current_df.drop(columns=['Dataset'])
                doca_per_device_results[bf_dir][algo].append(current_df)
    # CPU results, both hosts and DPUs
    cols_to_drop = ['codec', 'param', 'cmem', 'dmem', 'cstack', 'dstack', 'time']
    # Get all names from the dirs
    cpu_per_device_results = {d: {"dflt": [], "libdeflate": [], "lz4": []} for d in os.listdir(base_path) if os.path.isdir(os.path.join(base_path, d))}
    for device in cpu_per_device_results.keys():
        current_dir = os.path.join(base_path, device)
        orig_cpu_csv_paths = glob.glob(f'{current_dir}/cpu-orig*.csv')
        var_cpu_csv_paths = glob.glob(f'{current_dir}/cpu-variable*.csv')
        for current_path in orig_cpu_csv_paths + var_cpu_csv_paths:
            cpu_df = pd.read_csv(current_path, sep="\\s+")
            cpu_df.drop(columns=cols_to_drop, inplace=True)
            if "orig" in current_path:
                cpu_df.index.name = "orig"
            else:
                cpu_df.index.name = "var"
            for algo in cpu_per_device_results[device].keys():
                if algo in current_path:
                    cpu_per_device_results[device][algo].append(cpu_df)
        assert(len({len(v) for v in cpu_per_device_results[device].values()}) == 1)
        assert(len(cpu_per_device_results[device][algo]) == 2)  # whatever the last one might be
    for device in ["bf2", "bf3"]: # rename to keys from initial experiments (bf2, bf2-host, bf2-dpu instead of bf2 overlap)
        cpu_per_device_results[f"{device}-dpu"] = cpu_per_device_results.pop(device)

    # Figure 7, Throughput vs input size for DEFLATE Compression, use the "var" results
    # TODO: has half results for BF2 due to buffer changes
    per_input_size_results = {"dflt": [], "deflate-decompress": [], "lz4": []}
    for device in doca_per_device_results.keys():
        orig_dflt_results = doca_per_device_results[device]["dflt"][1]
        assert orig_dflt_results.index.name == "var"
        orig_dflt_results = orig_dflt_results[orig_dflt_results['Configuration'] == 'CDFLT']
        orig_dflt_results = orig_dflt_results.drop(columns=['Configuration', 'Filename'])
        orig_dflt_results = orig_dflt_results.groupby(['Input Size (bytes)']).mean().reset_index()
        orig_dflt_results = orig_dflt_results.rename(columns={"Task Throughput (MB/s)": "compression_mbps",
                                                            "Cb Task Throughput (MB/s)": "cb_compression_mbps",
                                                            "Input Size (bytes)": "size"})
        doca_dflt_results = orig_dflt_results[["size", "compression_mbps"]]
        no_doca_dflt_results = orig_dflt_results[["size", "cb_compression_mbps"]]
        doca_dflt_results["device"] = device
        per_input_size_results["dflt"].append(doca_dflt_results)
        no_doca_dflt_results["device"] = device + "-asic"
        no_doca_dflt_results = no_doca_dflt_results.rename(columns={"cb_compression_mbps" : "compression_mbps"})
        per_input_size_results["dflt"].append(no_doca_dflt_results)

    for device in cpu_per_device_results.keys():
        orig_dflt_results = cpu_per_device_results[device]["dflt"][1]
        assert orig_dflt_results.index.name == "var"
        orig_dflt_results["compression_mbps"] = (orig_dflt_results["size"] / 1_048_576) / orig_dflt_results["ctime"]
        orig_dflt_results = orig_dflt_results.drop(columns=['dataset', 'level'])
        orig_dflt_results = orig_dflt_results.groupby(['size']).mean().reset_index()
        orig_dflt_results["device"] = device
        orig_dflt_results = orig_dflt_results[["size", "compression_mbps", "device"]]
        per_input_size_results["dflt"].append(orig_dflt_results)

    all_per_input_size_results_dflt = pd.concat(per_input_size_results["dflt"])
    dflt_pivoted = all_per_input_size_results_dflt.pivot(index="size", columns="device", values="compression_mbps")
    dflt_pivoted = dflt_pivoted.fillna(0)
    dflt_pivoted.to_csv(f"{base_path}/comp-dflt-size-vs-throughput.csv")

    # Figure 9
    # TODO: has half results for BF2 due to buffer changes
    # TODO: replace with doca-bench results?
    results = {"dflt": [], "lz4": [], "libdeflate": []}
    for device in doca_per_device_results.keys():
        orig_dflt_results = doca_per_device_results[device]["dflt"][1] # load var since orig doesn't work on new FW
        assert orig_dflt_results.index.name == "var"
        if device == "bf2":
            logger.debug("Debugging BF2 contents for fig 9:")
            print(f"Got {len(orig_dflt_results)} BF2 results for dflt")
            orig_dflt_results = orig_dflt_results[orig_dflt_results["Cb Task Throughput (MB/s)"] > 0.]
        orig_dflt_results = orig_dflt_results.groupby(['Configuration', 'Filename']).mean().reset_index()
        orig_dflt_results = orig_dflt_results[orig_dflt_results['Configuration'] == 'CDFLT']
        if device == "bf2":
            print(f"Got {len(orig_dflt_results)} BF2 results for dflt after filtering for CDFLT")
        orig_dflt_results = orig_dflt_results.rename(columns={"Filename": "dataset", 
                                                              "Task Throughput (MB/s)": "compression_mbps",
                                                              "Input Size (bytes)": "size",
                                                              "Output Size (bytes)": "csize"})
        orig_dflt_results["device"] = device
        orig_dflt_results["reduction_percentage"] = 100.0 - ((orig_dflt_results["csize"] / orig_dflt_results["size"]) * 100)
        orig_dflt_results = orig_dflt_results[["dataset", "compression_mbps", "device", "reduction_percentage"]]
        results["dflt"].append(orig_dflt_results)
    
    for device in cpu_per_device_results.keys():
        orig_dflt_results = cpu_per_device_results[device]["dflt"][0]
        assert orig_dflt_results.index.name == "orig"
        orig_dflt_results["compression_mbps"] = (orig_dflt_results["size"] / 1_048_576) / orig_dflt_results["ctime"]
        orig_dflt_results["device"] = device
        orig_dflt_results["algo"] = "dflt-" + orig_dflt_results["level"].astype(str)
        orig_dflt_results["reduction_percentage"] = 100.0 - ((orig_dflt_results["csize"] / orig_dflt_results["size"]) * 100)
        orig_dflt_results = orig_dflt_results[["dataset", "compression_mbps", "device", "reduction_percentage"]]
        results["dflt"].append(orig_dflt_results)
    df_dflt = pd.concat(results["dflt"], ignore_index=True)
    df_dflt_per_device = df_dflt.groupby(['device', 'dataset'], as_index=False).agg({"compression_mbps": "mean"})
    dflt_pivoted = df_dflt_per_device.pivot(index="dataset", columns="device", values="compression_mbps")
    dflt_pivoted.reset_index(inplace=True)  # make 'dataset' into a normal column again
    # dflt_pivoted.to_csv(f"{base_path}/comp-dflt.csv", index=False)
    df_dflt_per_algo = df_dflt.groupby(['device'], as_index=False).agg({"compression_mbps": "mean", 
                                                                        "reduction_percentage": "mean"})
    df_dflt_per_algo["algo"] = "dflt"
    for device in cpu_per_device_results.keys():
        orig_libdeflate_results = cpu_per_device_results[device]["libdeflate"][0]
        assert orig_libdeflate_results.index.name == "orig"
        orig_libdeflate_results = orig_libdeflate_results[orig_libdeflate_results["level"] != 2]
        orig_libdeflate_results["compression_mbps"] = (orig_libdeflate_results["size"] / 1_048_576) / orig_libdeflate_results["ctime"]
        orig_libdeflate_results["device"] = device
        orig_libdeflate_results["algo"] = "libdeflate-" + orig_libdeflate_results["level"].astype(str)
        orig_libdeflate_results["reduction_percentage"] = 100.0 - ((orig_libdeflate_results["csize"] / orig_libdeflate_results["size"]) * 100)
        orig_libdeflate_results = orig_libdeflate_results[["dataset", "compression_mbps", "reduction_percentage", "device", "algo"]]
        results["libdeflate"].append(orig_libdeflate_results)
    df_libdeflate = pd.concat(results["libdeflate"], ignore_index=True)
    df_libdeflate_per_algo = df_libdeflate.groupby(['device', 'algo'], as_index=False).agg({"compression_mbps": "mean",
                                                                                            "reduction_percentage": "mean"})
    for device in cpu_per_device_results.keys():
        orig_lz4_results = cpu_per_device_results[device]["lz4"][0]
        assert orig_lz4_results.index.name == "orig"
        orig_lz4_results = orig_lz4_results[orig_lz4_results["level"] != 6]
        orig_lz4_results["compression_mbps"] = (orig_lz4_results["size"] / 1_048_576) / orig_lz4_results["ctime"]
        orig_lz4_results["device"] = device
        orig_lz4_results["algo"] = "lz4-" + orig_lz4_results["level"].astype(str)
        orig_lz4_results["reduction_percentage"] = 100.0 - ((orig_lz4_results["csize"] / orig_lz4_results["size"]) * 100)
        orig_lz4_results = orig_lz4_results[["dataset", "compression_mbps", "reduction_percentage", "device", "algo"]]
        results["lz4"].append(orig_lz4_results)
    df_lz4 = pd.concat(results["lz4"], ignore_index=True)
    df_lz4_per_algo = df_lz4.groupby(['device', 'algo'], as_index=False).agg({"compression_mbps": "mean",
                                                                            "reduction_percentage": "mean"})
    df_all_algos = pd.concat([df_dflt_per_algo, df_libdeflate_per_algo, df_lz4_per_algo], ignore_index=True)
    all_algos_pivoted = df_all_algos.pivot(index="algo", columns="device", values="compression_mbps")
    all_algos_pivoted.fillna(0, inplace=True)
    all_algos_pivoted.to_csv(f"{base_path}/comp-algos-avg.csv")

    # Figure 10
    # TODO: has half results for BF2 due to buffer changes
    for device in doca_per_device_results.keys():
        orig_dflt_results = doca_per_device_results[device]["dflt"][1]
        assert orig_dflt_results.index.name == "var"
        orig_dflt_results = orig_dflt_results[orig_dflt_results['Configuration'] == 'DDFLT']
        if device == "bf2":
            orig_dflt_results = orig_dflt_results[orig_dflt_results["Cb Task Throughput (MB/s)"] > 0.]
        orig_dflt_results = orig_dflt_results.drop(columns=['Configuration', 'Filename'])
        orig_dflt_results = orig_dflt_results.groupby(['Input Size (bytes)']).mean().reset_index()
        orig_dflt_results = orig_dflt_results.rename(columns={"Task Throughput (MB/s)": "decompression_mbps",
                                                            "Cb Task Throughput (MB/s)": "cb_decompression_mbps",
                                                            "Input Size (bytes)": "size"})
        doca_dflt_results = orig_dflt_results[["size", "decompression_mbps"]]
        no_doca_dflt_results = orig_dflt_results[["size", "cb_decompression_mbps"]]
        doca_dflt_results["device"] = device
        per_input_size_results["deflate-decompress"].append(doca_dflt_results)
        no_doca_dflt_results["device"] = device + "-asic"
        no_doca_dflt_results = no_doca_dflt_results.rename(columns={"cb_decompression_mbps" : "decompression_mbps"})
        per_input_size_results["deflate-decompress"].append(no_doca_dflt_results)

    for device in cpu_per_device_results.keys():
        orig_dflt_results = cpu_per_device_results[device]["dflt"][1]
        assert orig_dflt_results.index.name == "var"
        orig_dflt_results["decompression_mbps"] = (orig_dflt_results["csize"] / 1_048_576) / orig_dflt_results["dtime"]
        orig_dflt_results = orig_dflt_results.drop(columns=['dataset', 'level'])
        orig_dflt_results = orig_dflt_results.groupby(['csize']).mean().reset_index()
        orig_dflt_results["device"] = device
        orig_dflt_results = orig_dflt_results[["csize", "decompression_mbps", "device"]]
        orig_dflt_results = orig_dflt_results.rename(columns={"csize": "size"})
        per_input_size_results["deflate-decompress"].append(orig_dflt_results)
    all_per_input_size_results_deflate_decompress = pd.concat(per_input_size_results["deflate-decompress"])
    deflate_decompress_pivoted = all_per_input_size_results_deflate_decompress.pivot(index="size", columns="device", values="decompression_mbps")
    deflate_decompress_pivoted = deflate_decompress_pivoted.fillna(0)
    deflate_decompress_pivoted.to_csv(f"{base_path}/decomp-dflt-size-vs-throughput.csv")

    # Figure 11
    for device in doca_per_device_results.keys():
        if device == "bf2":
            continue
        orig_dflt_results = doca_per_device_results[device]["lz4"][1]
        assert orig_dflt_results.index.name == "var"
        orig_dflt_results = orig_dflt_results[orig_dflt_results['Configuration'] == 'DLZ4']
        orig_dflt_results = orig_dflt_results.drop(columns=['Configuration', 'Filename'])
        orig_dflt_results = orig_dflt_results.groupby(['Input Size (bytes)']).mean().reset_index()
        orig_dflt_results = orig_dflt_results.rename(columns={"Task Throughput (MB/s)": "decompression_mbps",
                                                            "Cb Task Throughput (MB/s)": "cb_decompression_mbps",
                                                            "Input Size (bytes)": "size"})
        doca_dflt_results = orig_dflt_results[["size", "decompression_mbps"]]
        no_doca_dflt_results = orig_dflt_results[["size", "cb_decompression_mbps"]]
        doca_dflt_results["device"] = device
        per_input_size_results["lz4"].append(doca_dflt_results)
        no_doca_dflt_results["device"] = device + "-asic"
        no_doca_dflt_results = no_doca_dflt_results.rename(columns={"cb_decompression_mbps" : "decompression_mbps"})
        per_input_size_results["lz4"].append(no_doca_dflt_results)

    for device in cpu_per_device_results.keys():
        orig_dflt_results = cpu_per_device_results[device]["lz4"][1]
        assert orig_dflt_results.index.name == "var"
        orig_dflt_results["decompression_mbps"] = (orig_dflt_results["csize"] / 1_048_576) / orig_dflt_results["dtime"]
        orig_dflt_results = orig_dflt_results.drop(columns=['dataset', 'level'])
        orig_dflt_results = orig_dflt_results.groupby(['csize']).mean().reset_index()
        orig_dflt_results["device"] = device
        orig_dflt_results = orig_dflt_results[["csize", "decompression_mbps", "device"]]
        orig_dflt_results = orig_dflt_results.rename(columns={"csize": "size"})
        per_input_size_results["lz4"].append(orig_dflt_results)

    all_per_input_size_results_lz4_decompress = pd.concat(per_input_size_results["lz4"])
    lz4_decompress_pivoted = all_per_input_size_results_lz4_decompress.pivot(index="size", columns="device", values="decompression_mbps")
    lz4_decompress_pivoted = lz4_decompress_pivoted.fillna(0)
    lz4_decompress_pivoted.to_csv(f"{base_path}/decomp-lz4-size-vs-throughput.csv")

    # Figure 12
    results = {"dflt": [], "lz4": [], "libdeflate": []}
    for device in doca_per_device_results.keys():
        orig_dflt_results = doca_per_device_results[device]["dflt"][0]
        assert orig_dflt_results.index.name == "orig"
        orig_dflt_results = orig_dflt_results.groupby(['Configuration', 'Filename']).mean().reset_index()
        orig_dflt_results = orig_dflt_results[orig_dflt_results['Configuration'] == 'DDFLT']
        orig_dflt_results = orig_dflt_results.rename(columns={"Filename": "dataset",
                                                            "Cb Task Throughput (MB/s)": "cb_decompression_mbps",
                                                            "Task Throughput (MB/s)": "decompression_mbps"})
        orig_dflt_results["device"] = device
        doca_dflt_results = orig_dflt_results[["dataset", "decompression_mbps", "device"]]
        results["dflt"].append(doca_dflt_results)
        no_doca_dflt_results = orig_dflt_results[["dataset", "cb_decompression_mbps"]]
        no_doca_dflt_results["device"] = device + "-asic"
        no_doca_dflt_results = no_doca_dflt_results.rename(columns={"cb_decompression_mbps" : "decompression_mbps"})
        results["dflt"].append(no_doca_dflt_results)

    for device in doca_per_device_results.keys():
        if device == "bf2":
            continue
        orig_lz4_results = doca_per_device_results[device]["lz4"][0]
        assert orig_lz4_results.index.name == "orig"
        orig_lz4_results = orig_lz4_results.groupby(['Configuration', 'Filename']).mean().reset_index()
        orig_lz4_results = orig_lz4_results[orig_lz4_results['Configuration'] == 'DLZ4']
        orig_lz4_results = orig_lz4_results.rename(columns={"Filename": "dataset",
                                                            "Cb Task Throughput (MB/s)": "cb_decompression_mbps",
                                                            "Task Throughput (MB/s)": "decompression_mbps"})
        orig_lz4_results["device"] = device
        orig_lz4_results["algo"] = "lz4"
        doca_lz4_results = orig_lz4_results[["dataset", "decompression_mbps", "device", "algo"]]
        results["lz4"].append(orig_lz4_results)
        no_doca_lz4_results = orig_lz4_results[["dataset", "cb_decompression_mbps", "algo"]]
        no_doca_lz4_results["device"] = device + "-asic"
        no_doca_lz4_results = no_doca_lz4_results.rename(columns={"cb_decompression_mbps" : "decompression_mbps"})
        results["lz4"].append(no_doca_lz4_results)

    for device in cpu_per_device_results.keys():
        orig_dflt_results = cpu_per_device_results[device]["dflt"][0]
        assert orig_dflt_results.index.name == "orig"
        orig_dflt_results["decompression_mbps"] = (orig_dflt_results["csize"] / 1_048_576) / orig_dflt_results["dtime"]
        orig_dflt_results["device"] = device
        orig_dflt_results["algo"] = "dflt-" + orig_dflt_results["level"].astype(str)
        orig_dflt_results = orig_dflt_results[["dataset", "decompression_mbps", "device"]]
        results["dflt"].append(orig_dflt_results)
    df_dflt = pd.concat(results["dflt"], ignore_index=True)
    df_dflt_per_algo = df_dflt.groupby(['device'], as_index=False).agg({"decompression_mbps": "mean"})
    df_dflt_per_algo["algo"] = "dflt"
    for device in cpu_per_device_results.keys():
        orig_libdeflate_results = cpu_per_device_results[device]["libdeflate"][0]
        assert orig_libdeflate_results.index.name == "orig"
        orig_libdeflate_results = orig_libdeflate_results[orig_libdeflate_results["level"] != 2]
        orig_libdeflate_results["decompression_mbps"] = (orig_libdeflate_results["csize"] / 1_048_576) / orig_libdeflate_results["dtime"]
        orig_libdeflate_results["device"] = device
        orig_libdeflate_results["algo"] = "libdeflate-" + orig_libdeflate_results["level"].astype(str)
        orig_libdeflate_results = orig_libdeflate_results[["dataset", "decompression_mbps", "device", "algo"]]
        results["libdeflate"].append(orig_libdeflate_results)
    df_libdeflate = pd.concat(results["libdeflate"], ignore_index=True)
    df_libdeflate_per_algo = df_libdeflate.groupby(['device'], as_index=False).agg({"decompression_mbps": "mean"})
    df_libdeflate_per_algo["algo"] = "libdeflate"
    for device in cpu_per_device_results.keys():
        orig_lz4_results = cpu_per_device_results[device]["lz4"][0]
        assert orig_lz4_results.index.name == "orig"
        orig_lz4_results = orig_lz4_results[orig_lz4_results["level"] != 6]
        orig_lz4_results["decompression_mbps"] = (orig_lz4_results["csize"] / 1_048_576) / orig_lz4_results["dtime"]
        orig_lz4_results["device"] = device
        orig_lz4_results["algo"] = "lz4-" + orig_lz4_results["level"].astype(str)
        orig_lz4_results = orig_lz4_results[["dataset", "decompression_mbps", "device", "algo"]]
        results["lz4"].append(orig_lz4_results)
    df_lz4 = pd.concat(results["lz4"], ignore_index=True)
    df_lz4_per_algo = df_lz4.groupby(['device'], as_index=False).agg({"decompression_mbps": "mean"})
    df_lz4_per_algo = df_lz4_per_algo.groupby(['device'], as_index=False).agg({"decompression_mbps": "mean"})
    df_lz4_per_algo["algo"] = "lz4"
    df_all_algos = pd.concat([df_dflt_per_algo, df_libdeflate_per_algo, df_lz4_per_algo], ignore_index=True)
    all_algos_pivoted = df_all_algos.pivot(index="algo", columns="device", values="decompression_mbps")
    all_algos_pivoted.fillna(1, inplace=True)
    all_algos_pivoted.to_csv(f"{base_path}/decomp-algos-avg.csv")

    # Figure 6 (both subfigs)
    # DEFLATE Compression time breakdown
    time_breakdown_res = {"dflt": [], "ddflt": []}

    ## Average times for DEFLATE
    for device in doca_per_device_results.keys():
        if "bf3" in device:
            continue
        orig_dflt_results = doca_per_device_results[device]["dflt"][1]
        assert orig_dflt_results.index.name == "var"
        orig_dflt_results = orig_dflt_results[orig_dflt_results["Cb Task Throughput (MB/s)"] > 0.]
        orig_dflt_results = orig_dflt_results.groupby(['Configuration', 'Filename']).mean().reset_index()
        orig_dflt_results = orig_dflt_results[orig_dflt_results['Configuration'] == 'CDFLT']
        orig_dflt_results = orig_dflt_results.rename(columns={"Filename": "dataset", 
                                                            "Task Throughput (MB/s)": "e2e_mbps",
                                                            'Cb Task Throughput (MB/s)': "cb_mbps",
                                                            "Total Time (seconds)": "total_time",
                                                            'Task Time (seconds)': "e2e_time",
                                                            "Ctx Task Time (seconds)": "ctx_time",
                                                            "Mem Time (seconds)": "mem_time",
                                                            "Dev Time (seconds)": "dev_time",
                                                            "Cb Task Time (seconds)": "cb_time",
                                                            "Cb Task Start Latency (seconds)": "cb_start",
                                                            "Cb Task End Latency (seconds)": "cb_end"})
        orig_dflt_results = orig_dflt_results[["e2e_mbps", "cb_mbps", "ctx_time", "mem_time", "dev_time", 
                                            "cb_time", "cb_start", "cb_end"]]
        orig_dflt_results = orig_dflt_results.mean()
        orig_dflt_results["agg_time"] = orig_dflt_results["dev_time"] + orig_dflt_results["mem_time"] \
                                        + orig_dflt_results["ctx_time"] + orig_dflt_results["cb_time"] \
                                        + orig_dflt_results["cb_end"] - orig_dflt_results["cb_start"]
        mean_df = orig_dflt_results.to_frame().T
        mean_df["device"] = device
        mean_df["algo"] = "dflt"
        time_breakdown_res["dflt"] = mean_df

    ## Average times for decompress DEFLATE
    for device in doca_per_device_results.keys():
        if device == "bf3":
            orig_dflt_results = doca_per_device_results[device]["dflt"][0]
            assert orig_dflt_results.index.name == "orig"
        else:
            orig_dflt_results = doca_per_device_results[device]["dflt"][1]
            assert orig_dflt_results.index.name == "var"
            orig_dflt_results = orig_dflt_results[orig_dflt_results["Cb Task Throughput (MB/s)"] > 0.]
        orig_dflt_results = orig_dflt_results.groupby(['Configuration', 'Filename']).mean().reset_index()
        orig_dflt_results = orig_dflt_results[orig_dflt_results['Configuration'] == 'DDFLT']
        orig_dflt_results = orig_dflt_results.rename(columns={"Filename": "dataset", 
                                                            "Task Throughput (MB/s)": "e2e_mbps",
                                                            'Cb Task Throughput (MB/s)': "cb_mbps",
                                                            "Total Time (seconds)": "total_time",
                                                            'Task Time (seconds)': "e2e_time",
                                                            "Ctx Task Time (seconds)": "ctx_time",
                                                            "Mem Time (seconds)": "mem_time",
                                                            "Dev Time (seconds)": "dev_time",
                                                            "Cb Task Time (seconds)": "cb_time",
                                                            "Cb Task Start Latency (seconds)": "cb_start",
                                                            "Cb Task End Latency (seconds)": "cb_end"})
        orig_dflt_results = orig_dflt_results[["e2e_mbps", "cb_mbps", "ctx_time", "mem_time", "dev_time", 
                                            "cb_time", "cb_start", "cb_end"]]
        orig_dflt_results = orig_dflt_results.mean()
        orig_dflt_results["agg_time"] = orig_dflt_results["dev_time"] + orig_dflt_results["mem_time"] \
                                        + orig_dflt_results["ctx_time"] + orig_dflt_results["cb_time"] \
                                        + orig_dflt_results["cb_end"] - orig_dflt_results["cb_start"]
        mean_df = orig_dflt_results.to_frame().T
        mean_df["device"] = device
        mean_df["algo"] = "ddflt"
        time_breakdown_res["ddflt"].append(mean_df)
    ddflate_results = pd.concat(time_breakdown_res["ddflt"], ignore_index=True)
    time_breakdown_res["ddflt"] = ddflate_results

    ## Average times for decompress LZ4
    for device in doca_per_device_results.keys():
        if "bf2" in device:
            continue
        orig_lz4_results = doca_per_device_results[device]["lz4"][0]
        assert orig_lz4_results.index.name == "orig"
        orig_lz4_results = orig_lz4_results.groupby(['Configuration', 'Filename']).mean().reset_index()
        orig_lz4_results = orig_lz4_results[orig_lz4_results['Configuration'] == 'DLZ4']
        orig_lz4_results = orig_lz4_results.rename(columns={"Filename": "dataset", 
                                                            "Task Throughput (MB/s)": "e2e_mbps",
                                                            'Cb Task Throughput (MB/s)': "cb_mbps",
                                                            "Total Time (seconds)": "total_time",
                                                            'Task Time (seconds)': "e2e_time",
                                                            "Ctx Task Time (seconds)": "ctx_time",
                                                            "Mem Time (seconds)": "mem_time",
                                                            "Dev Time (seconds)": "dev_time",
                                                            "Cb Task Time (seconds)": "cb_time",
                                                            "Cb Task Start Latency (seconds)": "cb_start",
                                                            "Cb Task End Latency (seconds)": "cb_end"})
        orig_lz4_results = orig_lz4_results[["e2e_mbps", "cb_mbps", "ctx_time", "mem_time", "dev_time", 
                                            "cb_time", "cb_start", "cb_end"]]
        orig_lz4_results = orig_lz4_results.mean()
        orig_lz4_results["agg_time"] = orig_lz4_results["dev_time"] + orig_lz4_results["mem_time"] \
                                        + orig_lz4_results["ctx_time"] + orig_lz4_results["cb_time"] \
                                        + orig_lz4_results["cb_end"] - orig_lz4_results["cb_start"]
        mean_df = orig_lz4_results.to_frame().T
        mean_df["device"] = device
        mean_df["algo"] = "lz4"
        time_breakdown_res["lz4"] = mean_df
    all_df = pd.concat(time_breakdown_res.values(), ignore_index=True)
    avg_time = all_df[["cb_end", "cb_time", "dev_time", "mem_time", "ctx_time"]].mean()
    time_breakdown = avg_time.to_frame().T
    time_breakdown.to_csv(f"{base_path}/avg_time_breakdown.csv", index=False)
    tput_breakdown = all_df[["e2e_mbps", "cb_mbps", "device"]]
    tput_breakdown["e2e_mbps"] = tput_breakdown["e2e_mbps"] / 102
    tput_breakdown["cb_mbps"] = tput_breakdown["cb_mbps"] / 1024
    tput_breakdown = tput_breakdown.groupby(['device']).mean().reset_index()
    tput_breakdown.to_csv(f"{base_path}/avg_tput_breakdown.csv", index=False)

def run_coprocess() -> None:
    logger.info("Running compress experiments for bf2-host, bf3-host, bf2-arm, bf3-arm...")
    os.chdir("../experiments/co-processing")
    try:  # bf2-host compress
        subprocess.run(
            ["ssh", "cloud-48", "rm -rf dpu-paper/experiments/co-processing/results-*.json"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "cloud-48", "rm -rf dpu-paper/experiments/co-processing/results-*.size"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "cloud-48", "cd dpu-paper/experiments/co-processing && bash measure-compress.sh"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf2-host compress completed")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Remote command failed on cloud-48 with exit code {e.returncode}")
        return
    try:  # bf2-host, copy and clean
        subprocess.run(
            ["scp", "cloud-48:dpu-paper/experiments/co-processing/results-*.json",
             "../../scripts/tex/figures/results/coprocess/compress/bf2/"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["scp", "cloud-48:dpu-paper/experiments/co-processing/results-*.size",
             "../../scripts/tex/figures/results/coprocess/compress/bf2/"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "cloud-48", "rm -rf dpu-paper/experiments/co-processing/results-*.json"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "cloud-48", "rm -rf dpu-paper/experiments/co-processing/results-*.size"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf2-host CPU compress copied")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Copy and clean coprocess compress results in bf2-host failed with exit code {e.returncode}")
        return
    try:  # bf2-dpu (CPU) compress
        subprocess.run(
            ["ssh", "cloud-48", "ssh bf-pcie 'cd dpu-paper/experiments/co-processing && rm -rf *.size *.json'"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "cloud-48", "ssh bf-pcie 'cd dpu-paper/experiments/co-processing && bash measure-compress.sh --arm'"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf2-dpu CPU compress completed")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Remote command failed on bf2-dpu with exit code {e.returncode}")
        return
    try:  # bf2-dpu from cloud-48
        subprocess.run( # first from bf -> cloud-48
            ["ssh", "cloud-48", "cd dpu-paper && scp bf-pcie:dpu-paper/experiments/co-processing/results-*.json ."], 
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        subprocess.run(
            ["ssh", "cloud-48", "cd dpu-paper && scp bf-pcie:dpu-paper/experiments/co-processing/results-*.size ."], 
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        subprocess.run(
            ["ssh", "cloud-48", "ssh bf-pcie 'cd dpu-paper/experiments/co-processing && rm -rf *.size *.json'"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["scp", "cloud-48:dpu-paper/results-*.json",
             "../../scripts/tex/figures/results/coprocess/compress/bf2-arm/"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run( # second from cloud-48 -> sr675
            ["scp", "cloud-48:dpu-paper/results-*.size",
             "../../scripts/tex/figures/results/coprocess/compress/bf2-arm/"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "cloud-48", "rm -rf dpu-paper/results-*.json"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "cloud-48", "rm -rf dpu-paper/results-*.size"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf2-dpu CPU compress copied")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Copying compress results from bf2-dpu to cloud-48 to sr675 failed with exit code {e.returncode}")
        return
    try:  # bf2-host decompress deflate
        subprocess.run(
            ["ssh", "cloud-48", "rm -rf dpu-paper/experiments/co-processing/results-*.json"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "cloud-48", "rm -rf dpu-paper/experiments/co-processing/results-*.size"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "cloud-48", "cd dpu-paper/experiments/co-processing && bash measure-decompress-deflate.sh --v2"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf2-host decompress-deflate completed")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Remote command failed on cloud-48 with exit code {e.returncode}")
        return
    try:  # bf2-host, copy and clean
        subprocess.run(
            ["scp", "cloud-48:dpu-paper/experiments/co-processing/results-*.json",
             "../../scripts/tex/figures/results/coprocess/decompress-deflate/bf2/"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["scp", "cloud-48:dpu-paper/experiments/co-processing/results-*.size",
             "../../scripts/tex/figures/results/coprocess/decompress-deflate/bf2/"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "cloud-48", "rm -rf dpu-paper/experiments/co-processing/results-*.json"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "cloud-48", "rm -rf dpu-paper/experiments/co-processing/results-*.size"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf2-host CPU decompress-deflate copied")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Copy and clean coprocess decompress-deflate results in bf2-host failed with exit code {e.returncode}")
        return
    try:  # bf2-dpu (CPU) decompress deflate
        subprocess.run(
            ["ssh", "cloud-48", "ssh bf-pcie 'cd dpu-paper/experiments/co-processing && rm -rf *.size *.json'"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "cloud-48", "ssh bf-pcie 'cd dpu-paper/experiments/co-processing && bash measure-decompress-deflate.sh --arm --v2'"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf2-dpu CPU decompress-deflate completed")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Remote command failed on bf2-dpu with exit code {e.returncode}")
        return
    try:  # bf2-dpu from cloud-48
        subprocess.run( # first from bf -> cloud-48
            ["ssh", "cloud-48", "cd dpu-paper && scp bf-pcie:dpu-paper/experiments/co-processing/results-*.json ."], 
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        subprocess.run(
            ["ssh", "cloud-48", "cd dpu-paper && scp bf-pcie:dpu-paper/experiments/co-processing/results-*.size ."], 
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        subprocess.run(
            ["scp", "cloud-48:dpu-paper/results-*.json",
             "../../scripts/tex/figures/results/coprocess/decompress-deflate/bf2-arm/"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run( # second from cloud-48 -> sr675
            ["scp", "cloud-48:dpu-paper/results-*.size",
             "../../scripts/tex/figures/results/coprocess/decompress-deflate/bf2-arm/"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "cloud-48", "rm -rf dpu-paper/results-*.json"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "cloud-48", "rm -rf dpu-paper/results-*.size"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf2-dpu CPU decompress-deflate copied")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Copying decompress-deflate results from bf2-dpu to cloud-48 to sr675 failed with exit code {e.returncode}")
        return
    try:  # bf3-host decompress deflate
        subprocess.run(
            ["rm", "-rf", "results-*.size", "results-*.json"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["bash", "measure-decompress-deflate.sh", "--v3"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf3-host decompress-deflate completed")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Remote command failed on bf3-host with exit code {e.returncode}")
        return
    try:  # bf3-host decompress deflate results
        subprocess.run(
            ["mv results-*.json ../../scripts/tex/figures/results/coprocess/decompress-deflate/bf3/"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True, shell=True
        )
        subprocess.run(
            ["mv results-*.size ../../scripts/tex/figures/results/coprocess/decompress-deflate/bf3/"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True, shell=True
        )
        logger.info("✔ bf3-host decompress-deflate copied")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Copying decompress-deflate results from bf3-host failed with exit code {e.returncode}")
        return
    try:  # bf3-dpu (CPU) decompress deflate
        subprocess.run(
            ["ssh", "bf-pcie", "rm -rf dpu-paper/experiments/co-processing/results-*.json"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "bf-pcie", "rm -rf dpu-paper/experiments/co-processing/results-*.size"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run( # TODO: adjust script dir to artifacts
            ["ssh", "bf-pcie", "cd dpu-paper/experiments/co-processing && bash measure-decompress-deflate.sh --arm --v3"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf3-dpu (CPU) decompress-deflate completed")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Remote command failed on bf3-dpu (CPU) with exit code {e.returncode}")
        return
    try:
        subprocess.run(
            ["scp", "bf-pcie:dpu-paper/experiments/co-processing/results-*.json",
            "../../scripts/tex/figures/results/coprocess/decompress-deflate/bf3-arm/"], 
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["scp", "bf-pcie:dpu-paper/experiments/co-processing/results-*.size",
            "../../scripts/tex/figures/results/coprocess/decompress-deflate/bf3-arm/"], 
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "bf-pcie", "rm -rf dpu-paper/experiments/co-processing/results-*.json"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "bf-pcie", "rm -rf dpu-paper/experiments/co-processing/results-*.size"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf3-dpu (CPU) decompress deflate copied")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Copying decompress-deflate results from bf3-dpu failed with exit code {e.returncode}")
        return
    try:  # bf3-host decompress lz4
        subprocess.run(
            ["rm -rf results-*.size results-*.json"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True, shell=True
        )
        subprocess.run(
            ["bash", "measure-decompress-lz4.sh"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf3-host decompress lz4 completed")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Remote command failed on bf3-host with exit code {e.returncode}")
        return
    try:  # bf3-host decompress lz4 results
        subprocess.run(
            ["mv results-*.json ../../scripts/tex/figures/results/coprocess/decompress-lz4/bf3/"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True, shell=True
        )
        subprocess.run(
            ["mv results-*.size ../../scripts/tex/figures/results/coprocess/decompress-lz4/bf3/"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True, shell=True
        )
        logger.info("✔ bf3-host decompress lz4 copied")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Copying compress results for bf3-host failed with exit code {e.returncode}")
        return
    try:  # bf3-dpu (CPU) decompress lz4
        subprocess.run(
            ["ssh", "bf-pcie", "rm -rf dpu-paper/experiments/co-processing/results-*.json"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "bf-pcie", "rm -rf dpu-paper/experiments/co-processing/results-*.size"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run( # TODO: adjust script dir to artifacts
            ["ssh", "bf-pcie", "cd dpu-paper/experiments/co-processing && bash measure-decompress-lz4.sh --arm"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf3-dpu (CPU) decompress lz4 completed")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Remote command failed on bf3-dpu (CPU) with exit code {e.returncode}")
        return
    try:
        subprocess.run(
            ["scp", "bf-pcie:dpu-paper/experiments/co-processing/results-*.json",
            "../../scripts/tex/figures/results/coprocess/decompress-lz4/bf3-arm/"], 
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["scp", "bf-pcie:dpu-paper/experiments/co-processing/results-*.size",
            "../../scripts/tex/figures/results/coprocess/decompress-lz4/bf3-arm/"], 
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "bf-pcie", "rm -rf dpu-paper/experiments/co-processing/results-*.json"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        subprocess.run(
            ["ssh", "bf-pcie", "rm -rf dpu-paper/experiments/co-processing/results-*.size"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True
        )
        logger.info("✔ bf3-dpu (CPU) decompress lz4 copied")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Copying decompress-deflate results from bf3-dpu failed with exit code {e.returncode}")
        return
    os.chdir("../../scripts") # root of proj
    logger.info("✔ co-processing completed")

def figures_coprocess() -> None:
    os.chdir("tex/figures/results/coprocess")
    # results dir
    dir = "compress"
    # devices
    devices = ["bf2"]

    # Dictionary to store results indexed by (i, j)
    results = {device: {} for device in devices}
    reconfiguration_results = {device: {} for device in devices}

    # averages for plotting
    averaged_results = {device: {} for device in devices}

    # Pattern to match results
    cpu_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-cpu-compress\.json")
    doca_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-doca-compress\.json")

    # Load all matching JSON files
    for device in devices:
        for file in glob.glob(f"{dir}/{device}/results-*-*-*-cpu-compress.json"):
            match = cpu_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers

                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                with open(f"{dir}/{device}/results-{i}-{j}-{filename}.size", 'r') as ssize:
                    full_file_size = int(ssize.readline().strip().split()[-1])

                # Store in results dictionary, use doca as "sharing percentage"
                key = j
                if key not in results[device]:
                    results[device][key] = []

                if key not in reconfiguration_results[device]:
                        reconfiguration_results[device][key] = []

                joined_runtime_seconds = float(data["joined_submission_elapsed"])

                if 0 < i < 100:
                    with open(f"{dir}/{device}/results-{i}-{j}-{filename}-doca-compress.json", "r") as f:
                        data = json.load(f)
                    
                    reconfiguration_runtime_seconds = float(data["overall_submission_elapsed"]) + float(data["ctx_stop_elapsed"])
                    reconfiguration_runtime_seconds = max(joined_runtime_seconds, reconfiguration_runtime_seconds)
                    reconfiguration_results[device][key].append((full_file_size / 1_048_576) / reconfiguration_runtime_seconds)
                elif i == 100:
                    reconfiguration_results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)

                results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)
        
        # no cpu file, only dpu
        for file in glob.glob(f"{dir}/{device}/results-0-100-*-doca-compress.json"):
            match = doca_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers
    
                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                with open(f"{dir}/{device}/results-{i}-{j}-{filename}.size", 'r') as ssize:
                    full_file_size = int(ssize.readline().strip().split()[-1])

                # Store in results dictionary, use doca as "sharing percentage"
                key = j
                if key not in results[device]:
                    results[device][key] = []

                joined_runtime_seconds = float(data["joined_submission_elapsed"])
                results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)

                if key not in reconfiguration_results[device]:
                    reconfiguration_results[device][key] = []

                reconfiguration_runtime_seconds = float(data["overall_submission_elapsed"]) + float(data["ctx_stop_elapsed"])
                reconfiguration_runtime_seconds = max(joined_runtime_seconds, reconfiguration_runtime_seconds)
                reconfiguration_results[device][key].append((full_file_size / 1_048_576) / reconfiguration_runtime_seconds)

        averaged_results[device] = {key: {"static": 0, "reconfiguration": 0} for key in results[device].keys()}
        for key in results[device].keys():
            key_avg = sum(results[device][key]) / len(results[device][key])
            averaged_results[device][key]["static"] = key_avg
            reconfiguration_key_avg = sum(reconfiguration_results[device][key]) / len(reconfiguration_results[device][key])
            averaged_results[device][key]["reconfiguration"] = reconfiguration_key_avg
    # Write to CSV
    with open(f"../coprocessing-compress-deflate.csv", "w", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Write header
        writer.writerow(["sharing_percentage", "static", "reconfiguration"])
        # Write rows
        sorted_keys = sorted(averaged_results["bf2"].keys())
        for key in sorted_keys: # only bf2 has this item
            writer.writerow([key, averaged_results["bf2"][key]["static"], averaged_results["bf2"][key]["reconfiguration"]])
    # devices
    devices = ["bf2-arm"]

    # Dictionary to store results indexed by (i, j)
    results = {device: {} for device in devices}
    reconfiguration_results = {device: {} for device in devices}

    # averages for plotting
    averaged_results = {device: {} for device in devices}

    # Pattern to match results
    cpu_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-cpu-compress\.json")
    doca_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-doca-compress\.json")

    # Load all matching JSON files
    for device in devices:
        for file in glob.glob(f"{dir}/{device}/results-*-*-*-cpu-compress.json"):
            match = cpu_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers

                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                with open(f"{dir}/{device}/results-{i}-{j}-{filename}.size", 'r') as ssize:
                    full_file_size = int(ssize.readline().strip().split()[-1])

                # Store in results dictionary, use doca as "sharing percentage"
                key = j
                if key not in results[device]:
                    results[device][key] = []

                if key not in reconfiguration_results[device]:
                        reconfiguration_results[device][key] = []

                joined_runtime_seconds = float(data["joined_submission_elapsed"])

                if 0 < i < 100:
                    with open(f"{dir}/{device}/results-{i}-{j}-{filename}-doca-compress.json", "r") as f:
                        data = json.load(f)
                    
                    reconfiguration_runtime_seconds = float(data["overall_submission_elapsed"]) + float(data["ctx_stop_elapsed"])
                    reconfiguration_runtime_seconds = max(joined_runtime_seconds, reconfiguration_runtime_seconds)
                    reconfiguration_results[device][key].append((full_file_size / 1_048_576) / reconfiguration_runtime_seconds)
                elif i == 100:
                    reconfiguration_results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)

                results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)
        
        # no cpu file, only dpu
        for file in glob.glob(f"{dir}/{device}/results-0-100-*-doca-compress.json"):
            match = doca_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers
    
                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                with open(f"{dir}/{device}/results-{i}-{j}-{filename}.size", 'r') as ssize:
                    full_file_size = int(ssize.readline().strip().split()[-1])

                # Store in results dictionary, use doca as "sharing percentage"
                key = j
                if key not in results[device]:
                    results[device][key] = []

                joined_runtime_seconds = float(data["joined_submission_elapsed"])
                results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)

                if key not in reconfiguration_results[device]:
                    reconfiguration_results[device][key] = []

                reconfiguration_runtime_seconds = float(data["overall_submission_elapsed"]) + float(data["ctx_stop_elapsed"])
                reconfiguration_runtime_seconds = max(joined_runtime_seconds, reconfiguration_runtime_seconds)
                reconfiguration_results[device][key].append((full_file_size / 1_048_576) / reconfiguration_runtime_seconds)

        averaged_results[device] = {key: {"static": 0, "reconfiguration": 0} for key in results[device].keys()}
        for key in results[device].keys():
            key_avg = sum(results[device][key]) / len(results[device][key])
            averaged_results[device][key]["static"] = key_avg
            reconfiguration_key_avg = sum(reconfiguration_results[device][key]) / len(reconfiguration_results[device][key])
            averaged_results[device][key]["reconfiguration"] = reconfiguration_key_avg
    # Write to CSV
    with open(f"../coprocessing-compress-deflate-arm.csv", "w", newline="") as csvfile:
        writer = csv.writer(csvfile)

        # Write header
        writer.writerow(["sharing_percentage", "static", "reconfiguration"])

        # Write rows
        sorted_keys = sorted(averaged_results["bf2-arm"].keys())
        for key in sorted_keys: # only bf2 has this item
            writer.writerow([key, averaged_results["bf2-arm"][key]["static"], averaged_results["bf2-arm"][key]["reconfiguration"]])
    # results dir
    dir = "decompress-deflate"

    # devices
    devices = ["bf2"]

    # Dictionary to store results indexed by (i, j)
    results = {device: {} for device in devices}
    reconfiguration_results = {device: {} for device in devices}

    # averages for plotting
    averaged_results = {device: {} for device in devices}

    # Pattern to match results
    cpu_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-cpu-decompress-deflate\.json")
    doca_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-doca-decompress-deflate\.json")

    # Load all matching JSON files
    for device in devices:
        for file in glob.glob(f"{dir}/{device}/results-*-*-*-cpu-decompress-deflate.json"):
            match = cpu_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers

                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                with open(f"{dir}/{device}/results-{i}-{j}-{filename}.size", 'r') as ssize:
                    full_file_size = int(ssize.readline().strip().split()[-1])

                # Store in results dictionary, use doca as "sharing percentage"
                key = j
                if key not in results[device]:
                    results[device][key] = []

                if key not in reconfiguration_results[device]:
                        reconfiguration_results[device][key] = []

                joined_runtime_seconds = float(data["joined_submission_elapsed"])

                if 0 < i < 100:
                    with open(f"{dir}/{device}/results-{i}-{j}-{filename}-doca-decompress-deflate.json", "r") as f:
                        data = json.load(f)
                    
                    reconfiguration_runtime_seconds = float(data["overall_submission_elapsed"]) + float(data["ctx_stop_elapsed"])
                    reconfiguration_runtime_seconds = max(joined_runtime_seconds, reconfiguration_runtime_seconds)
                    reconfiguration_results[device][key].append((full_file_size / 1_048_576) / reconfiguration_runtime_seconds)
                elif i == 100:
                    reconfiguration_results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)

                results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)
        
        # no cpu file, only dpu
        for file in glob.glob(f"{dir}/{device}/results-0-100-*-doca-decompress-deflate.json"):
            match = doca_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers
    
                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                with open(f"{dir}/{device}/results-{i}-{j}-{filename}.size", 'r') as ssize:
                    full_file_size = int(ssize.readline().strip().split()[-1])

                # Store in results dictionary, use doca as "sharing percentage"
                key = j
                if key not in results[device]:
                    results[device][key] = []

                joined_runtime_seconds = float(data["joined_submission_elapsed"])
                results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)

                if key not in reconfiguration_results[device]:
                    reconfiguration_results[device][key] = []

                reconfiguration_runtime_seconds = float(data["overall_submission_elapsed"]) + float(data["ctx_stop_elapsed"])
                reconfiguration_runtime_seconds = max(joined_runtime_seconds, reconfiguration_runtime_seconds)
                reconfiguration_results[device][key].append((full_file_size / 1_048_576) / reconfiguration_runtime_seconds)

        averaged_results[device] = {key: {"static": 0, "reconfiguration": 0} for key in results[device].keys()}
        for key in results[device].keys():
            key_avg = sum(results[device][key]) / len(results[device][key])
            averaged_results[device][key]["static"] = key_avg
            reconfiguration_key_avg = sum(reconfiguration_results[device][key]) / len(reconfiguration_results[device][key])
            averaged_results[device][key]["reconfiguration"] = reconfiguration_key_avg

    # Write to CSV
    with open(f"../coprocessing-decompress-deflate.csv", "w", newline="") as csvfile:
        writer = csv.writer(csvfile)

        # Write header
        writer.writerow(["sharing_percentage", "static", "reconfiguration"])

        # Write rows
        sorted_keys = sorted(averaged_results["bf2"].keys())
        for key in sorted_keys: # only bf2 has this item
            writer.writerow([key, averaged_results["bf2"][key]["static"], averaged_results["bf2"][key]["reconfiguration"]])
    # devices
    devices = ["bf2-arm"]

    # Dictionary to store results indexed by (i, j)
    results = {device: {} for device in devices}
    reconfiguration_results = {device: {} for device in devices}

    # averages for plotting
    averaged_results = {device: {} for device in devices}

    # Pattern to match results
    cpu_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-cpu-decompress-deflate\.json")
    doca_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-doca-decompress-deflate\.json")

    # Load all matching JSON files
    for device in devices:
        for file in glob.glob(f"{dir}/{device}/results-*-*-*-cpu-decompress-deflate.json"):
            match = cpu_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers

                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                with open(f"{dir}/{device}/results-{i}-{j}-{filename}.size", 'r') as ssize:
                    full_file_size = int(ssize.readline().strip().split()[-1])

                # Store in results dictionary, use doca as "sharing percentage"
                key = j
                if key not in results[device]:
                    results[device][key] = []

                if key not in reconfiguration_results[device]:
                        reconfiguration_results[device][key] = []

                joined_runtime_seconds = float(data["joined_submission_elapsed"])

                if 0 < i < 100:
                    with open(f"{dir}/{device}/results-{i}-{j}-{filename}-doca-decompress-deflate.json", "r") as f:
                        data = json.load(f)
                    
                    reconfiguration_runtime_seconds = float(data["overall_submission_elapsed"]) + float(data["ctx_stop_elapsed"])
                    reconfiguration_runtime_seconds = max(joined_runtime_seconds, reconfiguration_runtime_seconds)
                    reconfiguration_results[device][key].append((full_file_size / 1_048_576) / reconfiguration_runtime_seconds)
                elif i == 100:
                    reconfiguration_results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)

                results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)
        
        # no cpu file, only dpu
        for file in glob.glob(f"{dir}/{device}/results-0-100-*-doca-decompress-deflate.json"):
            match = doca_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers
    
                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                with open(f"{dir}/{device}/results-{i}-{j}-{filename}.size", 'r') as ssize:
                    full_file_size = int(ssize.readline().strip().split()[-1])

                # Store in results dictionary, use doca as "sharing percentage"
                key = j
                if key not in results[device]:
                    results[device][key] = []

                joined_runtime_seconds = float(data["joined_submission_elapsed"])
                results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)

                if key not in reconfiguration_results[device]:
                    reconfiguration_results[device][key] = []

                reconfiguration_runtime_seconds = float(data["overall_submission_elapsed"]) + float(data["ctx_stop_elapsed"])
                reconfiguration_runtime_seconds = max(joined_runtime_seconds, reconfiguration_runtime_seconds)
                reconfiguration_results[device][key].append((full_file_size / 1_048_576) / reconfiguration_runtime_seconds)

        averaged_results[device] = {key: {"static": 0, "reconfiguration": 0} for key in results[device].keys()}
        for key in results[device].keys():
            key_avg = sum(results[device][key]) / len(results[device][key])
            averaged_results[device][key]["static"] = key_avg
            reconfiguration_key_avg = sum(reconfiguration_results[device][key]) / len(reconfiguration_results[device][key])
            averaged_results[device][key]["reconfiguration"] = reconfiguration_key_avg
    # Write to CSV
    with open(f"../coprocessing-decompress-deflate-arm.csv", "w", newline="") as csvfile:
        writer = csv.writer(csvfile)

        # Write header
        writer.writerow(["sharing_percentage", "static", "reconfiguration"])

        # Write rows
        sorted_keys = sorted(averaged_results["bf2-arm"].keys())
        for key in sorted_keys: # only bf2 has this item
            writer.writerow([key, averaged_results["bf2-arm"][key]["static"], averaged_results["bf2-arm"][key]["reconfiguration"]])
    # results dir
    dir = "decompress-lz4"
    # devices
    devices = ["bf3"]

    # Dictionary to store results indexed by (i, j)
    results = {device: {} for device in devices}
    reconfiguration_results = {device: {} for device in devices}

    # averages for plotting
    averaged_results = {device: {} for device in devices}

    # Pattern to match results
    cpu_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-cpu-decompress-lz4\.json")
    doca_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-doca-decompress-lz4\.json")

    # Load all matching JSON files
    for device in devices:
        for file in glob.glob(f"{dir}/{device}/results-*-*-*-cpu-decompress-lz4.json"):
            match = cpu_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers

                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                with open(f"{dir}/{device}/results-{i}-{j}-{filename}.size", 'r') as ssize:
                    full_file_size = int(ssize.readline().strip().split()[-1])

                # Store in results dictionary, use doca as "sharing percentage"
                key = j
                if key not in results[device]:
                    results[device][key] = []

                if key not in reconfiguration_results[device]:
                        reconfiguration_results[device][key] = []

                joined_runtime_seconds = float(data["joined_submission_elapsed"])

                if 0 < i < 100:
                    with open(f"{dir}/{device}/results-{i}-{j}-{filename}-doca-decompress-lz4.json", "r") as f:
                        data = json.load(f)
                    
                    reconfiguration_runtime_seconds = float(data["overall_submission_elapsed"]) + float(data["ctx_stop_elapsed"])
                    reconfiguration_runtime_seconds = max(joined_runtime_seconds, reconfiguration_runtime_seconds)
                    reconfiguration_results[device][key].append((full_file_size / 1_048_576) / reconfiguration_runtime_seconds)
                elif i == 100:
                    reconfiguration_results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)

                results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)
        
        # no cpu file, only dpu
        for file in glob.glob(f"{dir}/{device}/results-0-100-*-doca-decompress-lz4.json"):
            match = doca_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers
    
                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                with open(f"{dir}/{device}/results-{i}-{j}-{filename}.size", 'r') as ssize:
                    full_file_size = int(ssize.readline().strip().split()[-1])

                # Store in results dictionary, use doca as "sharing percentage"
                key = j
                if key not in results[device]:
                    results[device][key] = []

                joined_runtime_seconds = float(data["joined_submission_elapsed"])
                results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)

                if key not in reconfiguration_results[device]:
                    reconfiguration_results[device][key] = []

                reconfiguration_runtime_seconds = float(data["overall_submission_elapsed"]) + float(data["ctx_stop_elapsed"])
                reconfiguration_runtime_seconds = max(joined_runtime_seconds, reconfiguration_runtime_seconds)
                reconfiguration_results[device][key].append((full_file_size / 1_048_576) / reconfiguration_runtime_seconds)

        averaged_results[device] = {key: {"static": 0, "reconfiguration": 0} for key in results[device].keys()}
        for key in results[device].keys():
            key_avg = sum(results[device][key]) / len(results[device][key])
            averaged_results[device][key]["static"] = key_avg
            reconfiguration_key_avg = sum(reconfiguration_results[device][key]) / len(reconfiguration_results[device][key])
            averaged_results[device][key]["reconfiguration"] = reconfiguration_key_avg
    # Write to CSV
    with open(f"../coprocessing-decompress-lz4-r.csv", "w", newline="") as csvfile:
        writer = csv.writer(csvfile)

        # Write header
        writer.writerow(["sharing_percentage", "static", "reconfiguration"])

        # Write rows
        sorted_keys = sorted(averaged_results["bf3"].keys())
        for key in sorted_keys: # only bf3 has this item
            writer.writerow([key, averaged_results["bf3"][key]["static"], averaged_results["bf3"][key]["reconfiguration"]])
    # devices
    devices = ["bf3-arm"]

    # Dictionary to store results indexed by (i, j)
    results = {device: {} for device in devices}
    reconfiguration_results = {device: {} for device in devices}

    # averages for plotting
    averaged_results = {device: {} for device in devices}

    # Pattern to match results
    cpu_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-cpu-decompress-lz4\.json")
    doca_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-doca-decompress-lz4\.json")

    # Load all matching JSON files
    for device in devices:
        for file in glob.glob(f"{dir}/{device}/results-*-*-*-cpu-decompress-lz4.json"):
            match = cpu_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers

                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                with open(f"{dir}/{device}/results-{i}-{j}-{filename}.size", 'r') as ssize:
                    full_file_size = int(ssize.readline().strip().split()[-1])

                # Store in results dictionary, use doca as "sharing percentage"
                key = j
                if key not in results[device]:
                    results[device][key] = []

                if key not in reconfiguration_results[device]:
                        reconfiguration_results[device][key] = []

                joined_runtime_seconds = float(data["joined_submission_elapsed"])

                if 0 < i < 100:
                    with open(f"{dir}/{device}/results-{i}-{j}-{filename}-doca-decompress-lz4.json", "r") as f:
                        data = json.load(f)
                    
                    reconfiguration_runtime_seconds = float(data["overall_submission_elapsed"]) + float(data["ctx_stop_elapsed"])
                    reconfiguration_runtime_seconds = max(joined_runtime_seconds, reconfiguration_runtime_seconds)
                    reconfiguration_results[device][key].append((full_file_size / 1_048_576) / reconfiguration_runtime_seconds)
                elif i == 100:
                    reconfiguration_results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)

                results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)
        
        # no cpu file, only dpu
        for file in glob.glob(f"{dir}/{device}/results-0-100-*-doca-decompress-lz4.json"):
            match = doca_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers
    
                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                with open(f"{dir}/{device}/results-{i}-{j}-{filename}.size", 'r') as ssize:
                    full_file_size = int(ssize.readline().strip().split()[-1])

                # Store in results dictionary, use doca as "sharing percentage"
                key = j
                if key not in results[device]:
                    results[device][key] = []

                joined_runtime_seconds = float(data["joined_submission_elapsed"])
                results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)

                if key not in reconfiguration_results[device]:
                    reconfiguration_results[device][key] = []

                reconfiguration_runtime_seconds = float(data["overall_submission_elapsed"]) + float(data["ctx_stop_elapsed"])
                reconfiguration_runtime_seconds = max(joined_runtime_seconds, reconfiguration_runtime_seconds)
                reconfiguration_results[device][key].append((full_file_size / 1_048_576) / reconfiguration_runtime_seconds)

        averaged_results[device] = {key: {"static": 0, "reconfiguration": 0} for key in results[device].keys()}
        for key in results[device].keys():
            key_avg = sum(results[device][key]) / len(results[device][key])
            averaged_results[device][key]["static"] = key_avg
            reconfiguration_key_avg = sum(reconfiguration_results[device][key]) / len(reconfiguration_results[device][key])
            averaged_results[device][key]["reconfiguration"] = reconfiguration_key_avg
    # Write to CSV
    with open(f"../coprocessing-decompress-lz4-arm.csv", "w", newline="") as csvfile:
        writer = csv.writer(csvfile)

        # Write header
        writer.writerow(["sharing_percentage", "static", "reconfiguration"])

        # Write rows
        sorted_keys = sorted(averaged_results["bf3-arm"].keys())
        for key in sorted_keys: # only bf3 has this item
            writer.writerow([key, averaged_results["bf3-arm"][key]["static"], averaged_results["bf3-arm"][key]["reconfiguration"]])
    # devices
    devices = ["host-results"]

    # Dictionary to store results indexed by (i, j)
    results = {device: {} for device in devices}
    reconfiguration_results = {device: {} for device in devices}

    # averages for plotting
    averaged_results = {device: {} for device in devices}

    # Pattern to match results
    cpu_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-cpu-decompress-lz4\.json")
    doca_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-doca-decompress-lz4\.json")

    # Load all matching JSON files
    for device in devices:
        for file in glob.glob(f"{dir}/{device}/results-*-*-*-cpu-decompress-lz4.json"):
            match = cpu_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers

                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                with open(f"{dir}/{device}/results-{i}-{j}-{filename}.size", 'r') as ssize:
                    full_file_size = int(ssize.readline().strip().split()[-1])

                # Store in results dictionary, use doca as "sharing percentage"
                key = j
                if key not in results[device]:
                    results[device][key] = []

                if key not in reconfiguration_results[device]:
                        reconfiguration_results[device][key] = []

                joined_runtime_seconds = float(data["joined_submission_elapsed"])

                if 0 < i < 100:
                    with open(f"{dir}/{device}/results-{i}-{j}-{filename}-doca-decompress-lz4.json", "r") as f:
                        data = json.load(f)
                    
                    reconfiguration_runtime_seconds = float(data["overall_submission_elapsed"]) + float(data["ctx_stop_elapsed"])
                    reconfiguration_runtime_seconds = max(joined_runtime_seconds, reconfiguration_runtime_seconds)
                    reconfiguration_results[device][key].append((full_file_size / 1_048_576) / reconfiguration_runtime_seconds)
                elif i == 100:
                    reconfiguration_results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)

                results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)
        
        # no cpu file, only dpu
        for file in glob.glob(f"{dir}/{device}/results-0-100-*-doca-decompress-lz4.json"):
            match = doca_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers
    
                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                with open(f"{dir}/{device}/results-{i}-{j}-{filename}.size", 'r') as ssize:
                    full_file_size = int(ssize.readline().strip().split()[-1])

                # Store in results dictionary, use doca as "sharing percentage"
                key = j
                if key not in results[device]:
                    results[device][key] = []

                joined_runtime_seconds = float(data["joined_submission_elapsed"])
                results[device][key].append((full_file_size / 1_048_576) / joined_runtime_seconds)

                if key not in reconfiguration_results[device]:
                    reconfiguration_results[device][key] = []

                reconfiguration_runtime_seconds = float(data["overall_submission_elapsed"]) + float(data["ctx_stop_elapsed"])
                reconfiguration_runtime_seconds = max(joined_runtime_seconds, reconfiguration_runtime_seconds)
                reconfiguration_results[device][key].append((full_file_size / 1_048_576) / reconfiguration_runtime_seconds)

        averaged_results[device] = {key: {"static": 0, "reconfiguration": 0} for key in results[device].keys()}
        for key in results[device].keys():
            key_avg = sum(results[device][key]) / len(results[device][key])
            averaged_results[device][key]["static"] = key_avg
            reconfiguration_key_avg = sum(reconfiguration_results[device][key]) / len(reconfiguration_results[device][key])
            averaged_results[device][key]["reconfiguration"] = reconfiguration_key_avg
    # Write to CSV
    with open(f"../coprocessing-decompress-lz4-host.csv", "w", newline="") as csvfile:
        writer = csv.writer(csvfile)

        # Write header
        writer.writerow(["sharing_percentage", "static", "reconfiguration"])

        # Write rows
        sorted_keys = sorted(averaged_results["host-results"].keys())
        for key in sorted_keys: # only bf3 has this item
            writer.writerow([key, averaged_results["host-results"][key]["static"], averaged_results["host-results"][key]["reconfiguration"]])
    # CPU Reduction
    dir = "compress"
    # devices
    devices = ["bf2"]

    # Dictionary to store results indexed by (i, j)
    results_compress = {device: {} for device in devices}

    # Pattern to match results
    cpu_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-cpu-compress\.json")

    # Load all matching JSON files
    for device in devices:
        for file in glob.glob(f"{dir}/{device}/results-*-*-*-cpu-compress.json"):
            match = cpu_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers

                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                if filename not in results_compress[device]:
                    results_compress[device][filename] = {}

                results_compress[device][filename][i] = float(data["cpu_time_elapsed"])
        
        total_reduction = 0
        for filename in results_compress[device].keys():
            max_time = max(results_compress[device][filename].values())
            min_time = min(results_compress[device][filename].values())
            reduction_pct = (max_time - min_time) / max_time * 100
            total_reduction += reduction_pct
        results_compress[device]['avg_reduction_pct'] = total_reduction / len(results_compress[device].keys())
    # results dir
    dir = "decompress-deflate"

    # devices
    devices = ["bf2", "bf3"]

    # Dictionary to store results indexed by (i, j)
    results_dflt = {device: {} for device in devices}

    # Pattern to match results
    cpu_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-cpu-decompress-deflate\.json")

    # Load all matching JSON files
    for device in devices:
        for file in glob.glob(f"{dir}/{device}/results-*-*-*-cpu-decompress-deflate.json"):
            match = cpu_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers

                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                if filename not in results_dflt[device]:
                    results_dflt[device][filename] = {}

                results_dflt[device][filename][i] = float(data["cpu_time_elapsed"])
        
        total_reduction = 0
        for filename in results_dflt[device].keys():
            max_time = max(results_dflt[device][filename].values())
            min_time = min(results_dflt[device][filename].values())
            reduction_pct = (max_time - min_time) / max_time * 100
            total_reduction += reduction_pct
        results_dflt[device]['avg_reduction_pct'] = total_reduction / len(results_dflt[device].keys())
    # results dir
    dir = "decompress-lz4"

    # devices
    devices = ["bf3"]

    # Dictionary to store results indexed by (i, j)
    results_lz4 = {device: {} for device in devices}

    # Pattern to match results
    cpu_pattern = re.compile(r"results-(\d+)-(\d+)-(.+)-cpu-decompress-lz4\.json")

    # Load all matching JSON files
    for device in devices:
        for file in glob.glob(f"{dir}/{device}/results-*-*-*-cpu-decompress-lz4.json"):
            match = cpu_pattern.match(os.path.basename(file))
            if match:
                i, j, filename = match.groups()
                i, j = int(i), int(j)  # Convert i, j to integers

                # Read JSON content
                with open(file, "r") as f:
                    data = json.load(f)

                if filename not in results_lz4[device]:
                    results_lz4[device][filename] = {}

                results_lz4[device][filename][i] = float(data["cpu_time_elapsed"])
        
        total_reduction = 0
        total_best_reduction = 0
        for filename in results_lz4[device].keys():
            max_time = max(results_lz4[device][filename].values())
            min_time = min(results_lz4[device][filename].values())
            reduction_pct = (max_time - min_time) / max_time * 100
            total_reduction += reduction_pct

            max_tput_time = results_lz4[device][filename][10]  # best tput is 10-90 split
            best_tput_reduction_pct = (max_time - max_tput_time) / max_time * 100
            total_best_reduction += best_tput_reduction_pct

        results_lz4[device]['avg_reduction_pct'] = total_reduction / len(results_lz4[device].keys())
        results_lz4[device]['avg_best_tput_reduction_pct'] = total_best_reduction / len(results_lz4[device].keys())
    # Write to CSV
    with open(f"../cpu-reduction.csv", "w", newline="") as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(["category", "bf2", "bf3"])
        writer.writerow(["Compr", results_compress["bf2"]['avg_reduction_pct'], -1])
        writer.writerow(["Dec-Defl", results_dflt["bf2"]['avg_reduction_pct'], results_dflt["bf3"]['avg_reduction_pct']])
        writer.writerow(["Dec-LZ4", -1, results_lz4["bf3"]['avg_best_tput_reduction_pct']])


EXPERIMENT_ARGS_AND_HELP = {
    "dma": "Run DMA bi-directional experiments (on host and dpu).",
    "compress": "Run (de)compress experiments (on host and dpu).",
    "coprocess": "Run co-processing experiments (on host and dpu)."
}

EXPERIMENT_RUNNERS = {
    "dma": run_dma,
    "compress": run_compress,
    "coprocess": run_coprocess
}

EXPERIMENT_FIGURES = {
    "dma": figures_dma,
    "compress": figures_compress,
    "coprocess": figures_coprocess
}

def build_pdf() -> None:
    logger.info("Building pdf...")
    os.chdir("tex")
    try:
        subprocess.run(
            ["latexmk", "-C", "-silent", "-interaction=nonstopmode"], check=True
        )
        subprocess.run(
            ["latexmk", "-pdf", "-silent", "-interaction=nonstopmode", "main.tex"], check=True
        )
        subprocess.run(
            ["latexmk", "-c", "-silent"], check=True
        )
        logger.info("✔ pdf ready in `tex/main.pdf`!")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Failed to build pdf, command error: {e.returncode}.")
    os.chdir("..")
    return

def consume_arguments():
    parser = argparse.ArgumentParser(description="DPU and co-processing experiments.")
    parser.add_argument("--all", action="store_true", help="Run all, overrides other conf")
    for exp_arg, exp_help in EXPERIMENT_ARGS_AND_HELP.items():
        parser.add_argument("--"+exp_arg, action="store_true", help=exp_help)
    parser.add_argument("--only_figs", action="store_true", help="Prepare figures with existing data")
    return parser.parse_args()

def init_logger() -> logging.Logger:
    # create logger
    logger = logging.getLogger('reprologger')
    logger.setLevel(logging.INFO)

    # create console handler and set level to debug
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)

    # create formatter
    formatter = logging.Formatter('[%(asctime)s][%(levelname)s] %(message)s')

    # add formatter to ch
    ch.setFormatter(formatter)

    # add ch to logger
    logger.addHandler(ch)
    return logger

if __name__ == '__main__':
    args = consume_arguments()
    logger = init_logger()

    # collect requested experiments (those flags explicitly passed)
    if args.all:
        logger.info("Requested all experiments...")
        requested = list(EXPERIMENT_ARGS_AND_HELP.keys())
    else:
        requested = [name for name in EXPERIMENT_ARGS_AND_HELP if getattr(args, name)]

    for name in requested:
        if not args.only_figs:
            EXPERIMENT_RUNNERS[name]()
        EXPERIMENT_FIGURES[name]()

    build_pdf()
