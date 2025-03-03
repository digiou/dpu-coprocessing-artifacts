import re
import time
import csv

from typing import Tuple

import re2

# Function to read regexes from a file
def read_regexes(file_path):
    regexes = []
    with open(file_path, 'r', encoding='utf-8') as file:
        for line in file:
            parts = line.strip().split(', ', 1)
            if len(parts) == 2:
                regexes.append(parts[1])
    return regexes

# Function to apply regex and measure time taken (in-memory clean approach)
def benchmark_regex_in_memory(file_path, regex, text_column) -> Tuple[float, float]:
    total_bytes = 0
    
    with open(file_path, newline='', encoding='utf-8') as csvfile:
        reader = csv.DictReader(csvfile)
        rows = list(reader)

    col_text = []
    for row in rows:
        if text_column in row:
            text = row[text_column]
            col_text.append(text)
            total_bytes += len(text.encode('utf-8'))

    start_time = time.time()
    for text in col_text:
        regex.search(text)

    # Calculate elapsed time
    elapsed_time = time.time() - start_time
    throughput = total_bytes / elapsed_time if elapsed_time > 0 else float('inf')

    return elapsed_time, throughput

# Function to apply regex and measure time taken (in-memory DOCA-like approach)
# DOCA benchmarks include file-parsing and logging overhead, this is "fair"
def benchmark_regex_in_memory_doca_like(file_path, regex, text_column) -> Tuple[float, float]:
    total_bytes = 0
    start_time = time.time()

    with open(file_path, newline='', encoding='utf-8') as csvfile:
        reader = csv.DictReader(csvfile)
        rows = list(reader)

    for row in rows:
        if text_column in row:
            text = row[text_column]
            regex.search(text)
            total_bytes += len(text.encode('utf-8'))

    end_time = time.time()

    # Calculate elapsed time
    elapsed_time = end_time - start_time
    throughput = total_bytes / elapsed_time if elapsed_time > 0 else float('inf')

    return elapsed_time, throughput

# Function to apply regex in a stream and measure throughput in bytes per second (streaming approach)
# Not measuring properly but DOCA benchmarks include file-parsing and logging overhead, this is "fair"
def benchmark_regex_streaming(file_path, regex, text_column) -> Tuple[float, float]:
    total_bytes = 0
    start_time = time.time()

    with open(file_path, newline='', encoding='utf-8') as csvfile:
        reader = csv.DictReader(csvfile)

        for row in reader:
            if text_column in row:
                text = row[text_column]
                regex.search(text)
                total_bytes += len(text.encode('utf-8'))

    end_time = time.time()

    # Calculate elapsed time
    elapsed_time = end_time - start_time
    throughput = total_bytes / elapsed_time if elapsed_time > 0 else float('inf')

    return elapsed_time, throughput

# Function to perform multiple runs and calculate averages
def benchmark(file_path, regexes, text_column, runs=10):
    results = []

    for i, regex in enumerate(regexes, start=1):
        print(f"Benchmarking regex {i}: {regex}")
        compiled_regex = re2.compile(regex)

        # In-memory benchmarking
        memory_times = []
        memory_throughputs = []
        for _ in range(runs):
            elapsed_time, throughput = benchmark_regex_in_memory(file_path, compiled_regex, text_column)
            memory_times.append(elapsed_time)
            memory_throughputs.append(throughput)

        avg_time_memory = sum(memory_times) / runs
        avg_throughput_memory = sum(memory_throughputs) / runs
        print(f"In-memory - Average time taken: {avg_time_memory:.2f} seconds")
        print(f"In-memory - Average throughput: {avg_throughput_memory:.2f} bytes per second")

        doca_like_memory_times = []
        doca_like_memory_throughputs = []
        for _ in range(runs):
            elapsed_time, throughput = benchmark_regex_in_memory_doca_like(file_path, compiled_regex, text_column)
            doca_like_memory_times.append(elapsed_time)
            doca_like_memory_throughputs.append(throughput)

        doca_like_avg_time_memory = sum(doca_like_memory_times) / runs
        doca_like_avg_throughput_memory = sum(doca_like_memory_throughputs) / runs
        print(f"In-memory (DOCA-like) - Average time taken: {doca_like_avg_time_memory:.2f} seconds")
        print(f"In-memory (DOCA-like) - Average throughput: {doca_like_avg_throughput_memory:.2f} bytes per second")

        # Streaming benchmarking
        streaming_times = []
        streaming_throughputs = []
        for _ in range(runs):
            elapsed_time, throughput = benchmark_regex_streaming(file_path, compiled_regex, text_column)
            streaming_times.append(elapsed_time)
            streaming_throughputs.append(throughput)

        avg_time_streaming = sum(streaming_times) / runs
        avg_throughput_streaming = sum(streaming_throughputs) / runs
        print(f"Streaming - Average time taken: {avg_time_streaming:.2f} seconds")
        print(f"Streaming - Average throughput: {avg_throughput_streaming:.2f} bytes per second")

        results.append({
            "regex": regex,
            "avg_time_memory": avg_time_memory,
            "avg_throughput_memory": avg_throughput_memory,
            "avg_doca_like_time_memory": doca_like_avg_time_memory,
            "avg_doca_like_throughput_memory": doca_like_avg_throughput_memory,
            "avg_time_streaming": avg_time_streaming,
            "avg_throughput_streaming": avg_throughput_streaming
        })

        print("\n" + "="*50 + "\n")

    return results

# Path to your CSV file and regex file
csv_file_path = 'data/US_Accidents_Dec21_updated.csv'
regex_file_path = 'us-accidents-queries'
text_column = 'Description'  # Column containing the text to be parsed

# Read regexes from the file
regexes = read_regexes(regex_file_path)

# Perform benchmarks
results = benchmark(csv_file_path, regexes, text_column)

# Specify the fieldnames (column headers) based on the keys in the dictionaries
fieldnames = ["regex", "avg_time_memory", "avg_throughput_memory", "avg_doca_like_time_memory", "avg_doca_like_throughput_memory", "avg_time_streaming", "avg_throughput_streaming"]

# Open the file in write mode
with open('cpu-regex.csv', mode='w', newline='') as file:
    writer = csv.DictWriter(file, fieldnames=fieldnames)
    
    # Write the header
    writer.writeheader()
    
    # Write the data rows
    for result in results:
        writer.writerow(result)