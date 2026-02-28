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
    filesize=2097152 # only runs on bf2, max is 2 MiB

    # Loop over percentage pairs
    for (( i=0, j=100; i<=100; i+=10, j-=10 )); do
        # Calculate sizes
        SIZE_CPU=$(( filesize * j / 100 ))
        SIZE_DPU=$(( filesize * i / 100 ))

        # Create temp copies and truncate
        cp $file /dev/shm/deflt-input # cpu
        cp $file /dev/shm/input.deflate # doca
        
        truncate -s "$SIZE_CPU" /dev/shm/deflt-input
        truncate -s "$SIZE_DPU" /dev/shm/input.deflate 

        ./build/co-processing-compress $j $i >> /dev/null
        sleep 1
        mv results-cpu-compress.json results-$j-$i-$filename-cpu-compress.json
        mv results-doca-compress.json results-$j-$i-$filename-doca-compress.json
        echo $SIZE_CPU $SIZE_DPU $filesize >> results-$j-$i-$filename.size
    done
done