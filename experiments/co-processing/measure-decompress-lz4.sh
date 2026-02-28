#!/bin/bash

# Initialize the arm flag to false
arm=false

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    --arm)
      arm=true
      shift
      ;;
    *)
      # If you have other arguments, handle them here
      shift
      ;;
  esac
done

if ! $arm; then
    if [ ! -d "vcpkg" ]; then
        git clone https://github.com/Microsoft/vcpkg.git
        ./vcpkg/bootstrap-vcpkg.sh -disableMetrics
    fi

    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
    cmake --build build
else
    rm -rf build
    cp -r arm-build build
fi

# create results dir
rm -rf results/doca ; mkdir -p results ; mkdir -p results/doca

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
        cp $file /dev/shm/lz4 # cpu
        cp $file /dev/shm/input-comp.lz4 # doca
        
        truncate -s "$SIZE_CPU" /dev/shm/lz4 # -> /dev/shm/lz4-input
        truncate -s "$SIZE_DPU" /dev/shm/input-comp.lz4

        read total_original chunk_index first_compressed_chunk first_compressed_chunk_lz4 < <(python3 ../local-compress/decompressor-preparer.py --file /dev/shm/input-comp.lz4)
        rm compressed*.deflate
        mv compressed-$chunk_index-$first_compressed_chunk_lz4.lz4 /dev/shm/input-comp.lz4

        ./build/co-processing-decompress-lz4 $j $i $SIZE_DPU 3 $first_compressed_chunk_lz4 $chunk_index >> /dev/null
        sleep 1
        mv results-cpu-decompress-lz4.json results-$j-$i-$filename-cpu-decompress-lz4.json
        mv results-doca-decompress-lz4.json results-$j-$i-$filename-doca-decompress-lz4.json
        compressed_filesize_cpu=$(stat -c '%s' /dev/shm/lz4-input)
        compressed_filesize_dpu=$(stat -c '%s' /dev/shm/input-comp.lz4)
        echo $compressed_filesize_cpu $compressed_filesize_dpu $filesize >> results-$j-$i-$filename.size
    done
done
