#!/usr/bin/env bash
# Input: 
    # $1 = original file
    # $2 = target size
    # $3 = python decompression preparer (from local-compress)
set -Eeuo pipefail


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

fill_buffer $1 $2

read total_original chunk_index first_compressed_chunk first_compressed_chunk_lz4 < <(python3 $3 --file /dev/shm/input)
cp compressed-$chunk_index-$first_compressed_chunk_lz4.lz4 /dev/shm/input-comp.lz4
cp compressed-$chunk_index-$first_compressed_chunk.deflate /dev/shm/input-comp.deflate
rm -rf compressed-$chunk_index-$first_compressed_chunk_lz4.lz4 compressed-$chunk_index-$first_compressed_chunk.deflate

# for next script to pass into co-processing binary
rm -rf /dev/shm/total_original.info /dev/shm/chunk_index.info /dev/shm/first_compressed_chunk.info /dev/shm/first_compressed_chunk_lz4.info
echo $total_original > /dev/shm/total_original.info
echo $chunk_index > /dev/shm/chunk_index.info
echo $first_compressed_chunk > /dev/shm/first_compressed_chunk.info
echo $first_compressed_chunk_lz4 > /dev/shm/first_compressed_chunk_lz4.info

# prepare binaries, copied from `co-processing/measure-decompress-lz4.sh`
cd /local-data/dimitrios/dpu-paper/experiments/co-processing
if [ ! -d "vcpkg" ]; then
    git clone https://github.com/Microsoft/vcpkg.git
    ./vcpkg/bootstrap-vcpkg.sh -disableMetrics
fi

/home/dimitrios-ldap/miniconda3/bin/cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
/home/dimitrios-ldap/miniconda3/bin/cmake --build build

# create results dir
rm -rf results/doca ; mkdir -p results ; mkdir -p results/doca