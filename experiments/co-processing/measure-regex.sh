#!/bin/bash

# create results dir
rm -rf results/doca ; mkdir -p results ; mkdir -p results/doca

# Iterate over each file found within the directory and subdirectories
find ../regex/data/ -type d -name ".git" -prune -o -type f ! -name "README.md" -print0 | while IFS= read -r -d '' file; do
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
        cp $file /dev/shm/cpu-regex # cpu
        cp $file /dev/shm/dpu-regex # doca
        
        truncate -s "$SIZE_CPU" /dev/shm/cpu-regex
        truncate -s "$SIZE_DPU" /dev/shm/dpu-regex

        ./build/co-processing-regex $j $i $SIZE_DPU >> /dev/null
        mv results-cpu-regex.json results-$j-$i-$filename-cpu-regex.json
        mv results-doca-regex.json results-$j-$i-$filename-doca-regex.json
        compressed_filesize_cpu=$(stat -c '%s' /dev/shm/cpu-regex)
        compressed_filesize_dpu=$(stat -c '%s' /dev/shm/dpu-regex)
        echo $compressed_filesize_cpu $compressed_filesize_dpu $filesize >> results-$j-$i-$filename.size
    done
done
