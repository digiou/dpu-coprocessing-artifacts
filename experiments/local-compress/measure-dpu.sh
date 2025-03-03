#!/bin/bash

# Initialize the OLD flag to false
OLD=false
V2=false
V3=false

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    --old)
      OLD=true
      shift
      ;;
    --v2)
      V2=true
      shift
      ;;
    --v3)
      V3=true
      shift
      ;;
    *)
      # If you have other arguments, handle them here
      shift
      ;;
  esac
done

iters=2

mkdir -p build
rm -rf build/results/doca
cp meson.build meson.build.orig
if [ "$OLD" = true ]; then
    cp meson.build.2.2.0 meson.build
fi
cd build
meson ..
ninja clean
ninja
mv ../meson.build.orig ../meson.build

# create results dir
rm -rf results/doca ; mkdir -p results ; mkdir -p results/doca

parse_deflate_log_file() {
    # Use awk to parse each log file
    awk -v parentdir=$2 -v filename=$3 '
        BEGIN {
            OFMT = "%.6f";  # Set output format for numbers to ensure floating-point precision
            processing_compression = 0;
            processing_decompression = 0;
            error_decompression = 0; # False for no error in decompression
        }

        /Starting compression/ {
            processing_compression = 1;
        }

        /Starting decompression/ {
            processing_decompression = 1;
        }

        # Capture data for compression
        /In compress_file. file size/ && processing_compression {
            split($0, a, "file size ");
            input_size_c = a[2] + 0;
        }
        /Allocated dst buffer number:/ && processing_compression {
            split($0, b, "number: ");
            buffer_num_c = b[2] + 0;
        }
        /Allocated dst buffer size:/ && processing_compression {
            split($0, b, "size: ");
            buffer_size_c = b[2] + 0;
        }
        /Total time:/ && processing_compression {
            split($0, d, "Total time: ");
            split(d[2], time, " ");
            total_time_c = time[1];
        }
        /Task time:/ && processing_compression {
            split($0, d, "Task time: ");
            split(d[2], time, " ");
            task_time_c = time[1];
        }
        /Ctx time:/ && processing_compression {
            split($0, d, "Ctx time: ");
            split(d[2], time, " ");
            ctx_time_c = time[1];
        }
        /Memory time:/ && processing_compression {
            split($0, d, "Memory time: ");
            split(d[2], time, " ");
            memory_time_c = time[1];
        }
        /Device time:/ && processing_compression {
            split($0, d, "Device time: ");
            split(d[2], time, " ");
            device_time_c = time[1];
        }
        /Callback task time:/ && processing_compression {
            split($0, d, "Callback task time: ");
            split(d[2], time, " ");
            cb_task_time_c = time[1];
        }
        /Callback task throughput:/ && processing_compression {
            split($0, d, "Callback task throughput: ");
            split(d[2], mbps_string, " ");
            cb_mbps_task_c = mbps_string[1];
        }
        /Throughput from caller thread:/ && processing_compression {
            split($0, d, "Throughput from caller thread: ");
            split(d[2], mbps_string, " ");
            mbps_task_c = mbps_string[1];
        }
        /Callback task start latency:/ && processing_compression {
            split($0, d, "Callback task start latency: ");
            split(d[2], time, " ");
            cb_task_start = time[1];
        }
        /Callback task end latency:/ && processing_compression {
            split($0, d, "Callback task end latency: ");
            split(d[2], time, " ");
            cb_task_end = time[1];
        }
        /Compressed file size:/ && processing_compression {
            split($0, c, "size: ");
            output_size_c = c[2] + 0;
            if (total_time_c > 0) {
                printf "CDFLT,%s,%s,%d,%d,%d,%d,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f\n", parentdir, filename, input_size_c, output_size_c, buffer_size_c, buffer_num_c, total_time_c, task_time_c, ctx_time_c, memory_time_c, device_time_c, cb_task_time_c, cb_mbps_task_c, mbps_task_c, cb_task_start, cb_task_end;
            }
            processing_compression = 0;
        }
        # /Task with Memory init time:/ && processing_compression {
        #     split($0, d, "Task with Memory init time: ");
        #     split(d[2], time, " ");
        #     task_and_memory_time_c = time[1];
        #     if (total_time_c > 0) {
        #         mbps_c = (input_size_c / 1048576) / task_and_memory_time_c;
        #         mbps_task_c = (input_size_c / 1048576) / task_time_c;
        #         printf "CDFLT,%s,%s,%d,%d,%d,%f,%f,%f,1,%f,%f,%d\n", parentdir, filename, input_size_c, output_size_c, buffer_size_c, task_and_memory_time_c, task_time_c, memory_time_c, mbps_c, mbps_task_c, 0;
        #     }
        #     processing_compression = 0;
        # }
        # Capture data for compression

        # Capture data for decompression
        /Failed to retrieve job: Input\/Output Operation Failed/ {
            error_decompression = 1; # True if error occurred during decompression
        }

        /In decompress_file. file size/ && processing_decompression {
            split($0, a, "file size ");
            input_size = a[2] + 0;
        }
        /Allocated dst buffer number:/ && processing_decompression {
            split($0, b, "number: ");
            buffer_num = b[2] + 0;
        }
        /Allocated dst buffer size:/ && processing_decompression {
            split($0, b, "size: ");
            buffer_size = b[2] + 0;
        }
        /Total time:/ && processing_decompression {
            split($0, d, "Total time: ");
            split(d[2], time, " ");
            total_time = time[1];
        }
        /Task time:/ && processing_decompression {
            split($0, d, "Task time: ");
            split(d[2], time, " ");
            task_time = time[1];
        }
        /Ctx time:/ && processing_decompression {
            split($0, d, "Ctx time: ");
            split(d[2], time, " ");
            ctx_time = time[1];
        }
        /Memory time:/ && processing_decompression {
            split($0, d, "Memory time: ");
            split(d[2], time, " ");
            memory_time = time[1];
        }
        /Device time:/ && processing_decompression {
            split($0, d, "Device time: ");
            split(d[2], time, " ");
            device_time = time[1];
        }
        /Callback task time:/ && processing_decompression {
            split($0, d, "Callback task time: ");
            split(d[2], time, " ");
            cb_task_time = time[1];
        }
        /Callback task throughput:/ && processing_decompression {
            split($0, d, "Callback task throughput: ");
            split(d[2], mbps_string, " ");
            cb_mbps_task = mbps_string[1];
        }
        /Throughput from caller thread:/ && processing_decompression {
            split($0, d, "Throughput from caller thread: ");
            split(d[2], mbps_string, " ");
            mbps_task = mbps_string[1];
        }
        /Callback task start latency:/ && processing_decompression {
            split($0, d, "Callback task start latency: ");
            split(d[2], time, " ");
            cb_task_start = time[1];
        }
        /Callback task end latency:/ && processing_decompression {
            split($0, d, "Callback task end latency: ");
            split(d[2], time, " ");
            cb_task_end = time[1];
        }
        /Compressed file size:/ && processing_decompression {
            split($0, c, "size: ");
            output_size = c[2] + 0;
            if (total_time > 0) {
                printf "DDFLT,%s,%s,%d,%d,%d,%d,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f\n", parentdir, filename, input_size, output_size, buffer_size, buffer_num, total_time, task_time, ctx_time, memory_time, device_time, cb_task_time, cb_mbps_task, mbps_task, cb_task_start, cb_task_end;
            }
            processing_decompression = 0;
        }

        # /In compress_file. file size/ && processing_decompression {
        #     split($0, e, "file size ");
        #     input_size_d = e[2] + 0;
        # }
        # /Allocated dst buffer size:/ && processing_decompression {
        #     split($0, f, "size: ");
        #     buffer_size_d = f[2] + 0;
        # }
        # /Decompressed file size:/ && processing_decompression {
        #     split($0, g, "size: ");
        #     output_size_d = g[2] + 0;
        # }
        # /Decompression time:/ && processing_decompression {
        #     split($0, h, "Decompression time: ");
        #     split(h[2], time, " ");
        #     decompression_time_d = time[1];
        # }
        # /Task time:/ && processing_decompression {
        #     split($0, i, "Task time: ");
        #     split(i[2], time, " ");
        #     task_time_d = time[1];
        # }
        # /Memory time:/ && processing_decompression {
        #     split($0, j, "Memory time: ");
        #     split(j[2], time, " ");
        #     memory_time_d = time[1];
        # }
        # /Task with Memory init time:/ && processing_decompression {
        #     split($0, k, "Task with Memory init time: ");
        #     split(k[2], time, " ");
        #     task_and_memory_time_d = time[1];
        #     if (decompression_time_d > 0) {
        #         mbps_d = (input_size_d / 1048576) / task_and_memory_time_d;
        #         mbps_task_d = (input_size_d / 1048576) / task_time_d;
        #         printf "DDFLT,%s,%s,%d,%d,%d,%f,%f,%f,1,%f,%f,%d\n", parentdir, filename, input_size_d, output_size_d, buffer_size_d, task_and_memory_time_d, task_time_d, memory_time_d, mbps_d, mbps_task_d, error_decompression;
        #     }
        #     processing_decompression = 0;
        # }
    ' "$1"
}

parse_lz4_log_file() {
    # Use awk to parse each log file
    awk -v parentdir=$2 -v filename=$3 '
        BEGIN {
            OFMT = "%.6f";  # Set output format for numbers to ensure floating-point precision
            processing_decompression = 0;
            error_decompression = 0; # False for no error in decompression
        }

        # Capture data for decompression
        /Failed to retrieve job: Input\/Output Operation Failed/ {
            error_decompression = 1; # True if error occurred during decompression
        }

        /Starting decompression/ {
            processing_decompression = 1;
        }

        /In decompress_file. file size/ && processing_decompression {
            split($0, a, "file size ");
            input_size = a[2] + 0;
        }
        /Allocated dst buffer number:/ && processing_decompression {
            split($0, b, "number: ");
            buffer_num = b[2] + 0;
        }
        /Allocated dst buffer size:/ && processing_decompression {
            split($0, b, "size: ");
            buffer_size = b[2] + 0;
        }
        /Total time:/ && processing_decompression {
            split($0, d, "Total time: ");
            split(d[2], time, " ");
            total_time = time[1];
        }
        /Task time:/ && processing_decompression {
            split($0, d, "Task time: ");
            split(d[2], time, " ");
            task_time = time[1];
        }
        /Ctx time:/ && processing_decompression {
            split($0, d, "Ctx time: ");
            split(d[2], time, " ");
            ctx_time = time[1];
        }
        /Memory time:/ && processing_decompression {
            split($0, d, "Memory time: ");
            split(d[2], time, " ");
            memory_time = time[1];
        }
        /Device time:/ && processing_decompression {
            split($0, d, "Device time: ");
            split(d[2], time, " ");
            device_time = time[1];
        }
        /Callback task time:/ && processing_decompression {
            split($0, d, "Callback task time: ");
            split(d[2], time, " ");
            cb_task_time = time[1];
        }
        /Callback task throughput:/ && processing_decompression {
            split($0, d, "Callback task throughput: ");
            split(d[2], mbps_string, " ");
            cb_mbps_task = mbps_string[1];
        }
        /Throughput from caller thread:/ && processing_decompression {
            split($0, d, "Throughput from caller thread: ");
            split(d[2], mbps_string, " ");
            mbps_task = mbps_string[1];
        }
        /Callback task start latency:/ && processing_decompression {
            split($0, d, "Callback task start latency: ");
            split(d[2], time, " ");
            cb_task_start = time[1];
        }
        /Callback task end latency:/ && processing_decompression {
            split($0, d, "Callback task end latency: ");
            split(d[2], time, " ");
            cb_task_end = time[1];
        }
        /Compressed file size:/ && processing_decompression {
            split($0, c, "size: ");
            output_size = c[2] + 0;
            if (total_time > 0) {
                printf "DLZ4,%s,%s,%d,%d,%d,%d,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f\n", parentdir, filename, input_size, output_size, buffer_size, buffer_num, total_time, task_time, ctx_time, memory_time, device_time, cb_task_time, cb_mbps_task, mbps_task, cb_task_start, cb_task_end;
            }
            processing_decompression = 0;
        }

        # # Capture data for decompression
        # /Failed to retrieve job: Input\/Output Operation Failed/ {
        #     error_decompression = 1; # True if error occurred during decompression
        # }
        # /In compress_file. file size/ && processing_decompression {
        #     split($0, e, "file size ");
        #     input_size_d = e[2] + 0;
        # }
        # /Allocated dst buffer size:/ && processing_decompression {
        #     split($0, f, "size: ");
        #     buffer_size_d = f[2] + 0;
        # }
        # /Decompressed file size:/ && processing_decompression {
        #     split($0, g, "size: ");
        #     output_size_d = g[2] + 0;
        # }
        # /Decompression time:/ && processing_decompression {
        #     split($0, h, "Decompression time: ");
        #     split(h[2], time, " ");
        #     decompression_time_d = time[1];
        # }
        # /Task time:/ && processing_decompression {
        #     split($0, i, "Task time: ");
        #     split(i[2], time, " ");
        #     task_time_d = time[1];
        # }
        # /Memory time:/ && processing_decompression {
        #     split($0, j, "Memory time: ");
        #     split(j[2], time, " ");
        #     memory_time_d = time[1];
        # }
        # /Task with Memory init time:/ && processing_decompression {
        #     split($0, k, "Task with Memory init time: ");
        #     split(k[2], time, " ");
        #     task_and_memory_time_d = time[1];
        #     if (decompression_time_d > 0) {
        #         mbps_d = (input_size_d / 1048576) / task_and_memory_time_d;
        #         mbps_task_d = (input_size_d / 1048576) / task_time_d;
        #         printf "DLZ4,%s,%s,%d,%d,%d,%f,%f,%f,1,%f,%f,%d\n", parentdir, filename, input_size_d, output_size_d, buffer_size_d, task_and_memory_time_d, task_time_d, memory_time_d, mbps_d, mbps_task_d, error_decompression;
        #     }
        #     processing_decompression = 0;
        # }
    ' "$1"
}

# Function to fill the buffer with repeated content of the original file
fill_buffer() {
    local input_file="$1"
    local buffer_size="$2"
    
    # Remove previous temp file
    rm -rf /dev/shm/input

    # Get the size of the original file
    local original_size=$(stat -c%s "$input_file")
    local repeat_count=$((buffer_size / original_size + 1))

    # Create the temporary file by repeating the original file's content
    truncate -s 0 /dev/shm/input
    for ((i=0; i<repeat_count; i++)); do
        cat "$input_file" >> /dev/shm/input
    done

    # Truncate the file to the exact buffer size
    truncate -s "$buffer_size" /dev/shm/input
}

# Define the specific buffer sizes to test (in bytes)
buffer_sizes=(
  131072      # 128 KB
  524288      # 512 KB
  1048576     # 1 MB
  5242880     # 5 MB
  10485760    # 10 MB
  52428800    # 50 MB
  134217728   # 128 MB
)

if [ "$V2" = true ]; then
    # Iterate over each file found within the directory and subdirectories
    find ../corpora/silesia -type d -name ".git" -prune -o -type f ! -name "README.md" -size -120M -print0 | while IFS= read -r -d '' file; do
        filename=$(basename "$file")
        parentdir=$(basename "$(dirname "$file")")
        if [ "$filename" = ".git" ]; then
            continue
        fi
        echo "Copying $file as input..."
        cp $file /dev/shm/input
        filesize=$(stat -c '%s' $file)

        for ((i=1; i<=iters; i++))
        do
            # Call doca_compression_local with file path argument
            if [ "$OLD" = true ]; then
                    ./doca_compression_local &> results/output.log
            else
                    ./doca_compress_deflate_faster 2 &> results/output.log
            fi

            cp results/output.log results/$parentdir.$filename.$i.dflt.log
            parse_deflate_log_file results/output.log $parentdir $filename >> results/doca/measurements-orig-dflt.csv

            if [ "$OLD" = false ]; then
                python3 ../compressor-sw.py /dev/shm/input 2 && mv python-comp.txt /dev/shm/input-comp.deflate
                ./doca_decompress_deflate_faster 2 1 1 $filesize &> results/output.log
                cp results/output.log results/$parentdir.$filename.$i.dflt.decomp.log
                parse_deflate_log_file results/output.log $parentdir $filename >> results/doca/measurements-orig-dflt.csv
            fi
        done

        # Test with specific buffer sizes
        for size in "${buffer_sizes[@]}"; do
            # Fill the buffer with the content of the original file
            echo "Using $size KB with $file as input..."
            fill_buffer "$file" "$size"
            sleep 1
            for ((i=1; i<=iters; i++))
            do
                # Call your binary with the file path as an argument
                if [ "$OLD" = true ]; then
                    ./doca_compression_local &> results/output.log
                else
                    ./doca_compress_deflate_faster 2 &> results/output.log
                fi
                sleep 1
                cp results/output.log results/$parentdir.$filename.$size.$i.dflt.log
                parse_deflate_log_file results/output.log $parentdir $filename >> results/doca/measurements-variable-dflt.csv

                if [ "$OLD" = false ]; then
                    python3 ../compressor-sw.py /dev/shm/input 3 && mv python-comp.txt /dev/shm/input-comp.deflate
                    ./doca_decompress_deflate_faster 2 1 1 $size &> results/output.log
                    cp results/output.log results/$parentdir.$filename.$i.dflt.decomp.log
                    parse_deflate_log_file results/output.log $parentdir $filename >> results/doca/measurements-variable-dflt.csv
                fi
            done
        done
    done
fi

if [ "$V3" = true ]; then
    # Iterate over each file found within the directory and subdirectories
    find ../corpora/silesia -type d -name ".git" -prune -o -type f ! -name "README.md" -size -120M -print0 | while IFS= read -r -d '' file; do
        filename=$(basename "$file")
        parentdir=$(basename "$(dirname "$file")")
        if [ "$filename" = ".git" ]; then
            continue
        fi
        echo "Copying $file as input..."
        cp $file /dev/shm/input
        size=$(stat -c '%s' $file)

        read total_original chunk_index first_compressed_chunk first_compressed_chunk_lz4 < <(python3 ../decompressor-preparer.py --file /dev/shm/input)
        mv compressed-$chunk_index-$first_compressed_chunk.deflate /dev/shm/input-comp.deflate
        mv compressed-$chunk_index-$first_compressed_chunk_lz4.lz4 /dev/shm/input-comp.lz4

        for ((i=1; i<=iters; i++))
        do
            ./doca_decompress_deflate_faster 3 $first_compressed_chunk $chunk_index $total_original &> results/output.log
            sleep 1
            cp results/output.log results/$parentdir.$filename.$i.dflt.decomp.log
            parse_deflate_log_file results/output.log $parentdir $filename >> results/doca/measurements-orig-dflt.csv

            ./doca_decompress_lz4_block_faster 3 $first_compressed_chunk_lz4 $chunk_index $total_original &> results/output.log
            sleep 1
            cp results/output.log results/$parentdir.$filename.$size.$i.lz4.decomp.log
            parse_lz4_log_file results/output.log $parentdir $filename >> results/doca/measurements-orig-lz4.csv
        done

        # Test with specific buffer sizes
        for size in "${buffer_sizes[@]}"; do
            # Fill the buffer with the content of the original file
            echo "Using $size KB with $file as input..."
            fill_buffer "$file" "$size"
            
            read total_original chunk_index first_compressed_chunk first_compressed_chunk_lz4 < <(python3 ../decompressor-preparer.py --file /dev/shm/input)
            mv compressed-$chunk_index-$first_compressed_chunk.deflate /dev/shm/input-comp.deflate
            mv compressed-$chunk_index-$first_compressed_chunk_lz4.lz4 /dev/shm/input-comp.lz4

            for ((i=1; i<=iters; i++))
            do
                ./doca_decompress_deflate_faster 3 $first_compressed_chunk $chunk_index $total_original &> results/output.log
                sleep 1
                cp results/output.log results/$parentdir.$filename.$i.dflt.decomp.log
                parse_deflate_log_file results/output.log $parentdir $filename >> results/doca/measurements-variable-dflt.csv
                
                ./doca_decompress_lz4_block_faster 3 $first_compressed_chunk_lz4 $chunk_index $total_original &> results/output.log
                sleep 1
                cp results/output.log results/$parentdir.$filename.$size.$i.lz4.decomp.log
                parse_lz4_log_file results/output.log $parentdir $filename >> results/doca/measurements-variable-lz4.csv
            done
        done
    done
fi
