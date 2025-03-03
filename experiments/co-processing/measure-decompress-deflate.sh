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

if [ "$V2" = true ]; then
    if [ ! -d "vcpkg" ]; then
        git clone https://github.com/Microsoft/vcpkg.git
        ./vcpkg/bootstrap-vcpkg.sh -disableMetrics
    fi

    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
    cmake --build build
fi

# create results dir
rm -rf results/doca ; mkdir -p results ; mkdir -p results/doca

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

if [ "$V2" = true ] || [ "$V3" = true ]; then
    # Iterate over each file found within the directory and subdirectories
    find ../local-compress/corpora/silesia -type d -name ".git" -prune -o -type f ! -name "README.md" -size -120M -print0 | while IFS= read -r -d '' file; do
        filename=$(basename "$file")
        parentdir=$(basename "$(dirname "$file")")
        if [ "$filename" = ".git" ]; then
            continue
        fi
        filesize=$(stat -c '%s' $file)

        # Loop over percentage pairs
        for (( i=0, j=100; i<=100; i+=10, j-=10 )); do
            # Calculate sizes
            SIZE_CPU=$(( filesize * j / 100 ))
            SIZE_DPU=$(( filesize * i / 100 ))

            # Create temp copies and truncate
            cp $file /dev/shm/infl # cpu
            cp $file /dev/shm/input-comp.deflate # doca
            
            truncate -s "$SIZE_CPU" /dev/shm/infl # -> /dev/shm/infl-input
            truncate -s "$SIZE_DPU" /dev/shm/input-comp.deflate

            if [ "$V2" = true ]; then
                python3 ../local-compress/compressor-sw.py /dev/shm/input-comp.deflate 3 && mv python-comp.txt /dev/shm/input-comp.deflate
                version=2
            fi

            if [ "$V3" = true ]; then
                read total_original chunk_index first_compressed_chunk first_compressed_chunk_lz4 < <(python3 ../local-compress/decompressor-preparer.py --file /dev/shm/input-comp.deflate)
                mv compressed-$chunk_index-$first_compressed_chunk.deflate /dev/shm/input-comp.deflate
                rm compressed*.lz4
                version=3
            fi

            if [ "$V2" = true ]; then
                ./build/co-processing-decompress-deflate $j $i $SIZE_DPU $version 1 1 >> /dev/null
            fi

            if [ "$V3" = true ]; then
                ./build/co-processing-decompress-deflate $j $i $SIZE_DPU $version $first_compressed_chunk $chunk_index >> /dev/null
            fi
            mv results-cpu-decompress-deflate.json results-$j-$i-$filename-cpu-decompress-deflate.json
            mv results-doca-decompress-deflate.json results-$j-$i-$filename-doca-decompress-deflate.json
            compressed_filesize_cpu=$(stat -c '%s' /dev/shm/infl-input)
            compressed_filesize_dpu=$(stat -c '%s' /dev/shm/input-comp.deflate)
            echo $compressed_filesize_cpu $compressed_filesize_dpu $filesize >> results-$j-$i-$filename.size
        done
    done
fi
