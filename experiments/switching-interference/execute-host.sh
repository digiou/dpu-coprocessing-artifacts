#!/usr/bin/env bash
# Input: nothing so far
set -Eeuo pipefail

INFO_DIR=/dev/shm  # from `prepare-host.sh`
vars=(
  total_original
  chunk_index
  first_compressed_chunk
  first_compressed_chunk_lz4
)

for v in "${vars[@]}"; do
    f="$INFO_DIR/$v.info"
    if [[ -r $f ]]; then
        # read first line from the file
        read -r value < "$f"
        # assign to the variable whose name is in $v
        printf -v "$v" '%s' "$value"
    else
        echo "warning: $f not found" >&2
    fi
done
rm -rf /dev/shm/total_original.info /dev/shm/chunk_index.info /dev/shm/first_compressed_chunk.info 
rm -rf /dev/shm/first_compressed_chunk_lz4.info /dev/shm/switching-decomp-lz4.log /dev/shm/switching-decomp-dflt.log

run_deflate=false
run_lz4=false
while [[ $# -gt 0 ]]; do
  case "$1" in
    --deflate)
      run_deflate=true
      shift
      ;;
    --lz4)
      run_lz4=true
      shift
      ;;
    *)
      # If you have other arguments, handle them here
      shift
      ;;
  esac
done

cd /local-data/dimitrios/dpu-paper/experiments/co-processing
filesize=$(stat -c '%s' /dev/shm/input)
if [ "$run_lz4" = true ]; then
    start=$(date +%s) # current epoch-seconds, run for 10s
    while (( $(date +%s) - start < 10 )); do
        # ---- your work here ----
        ./build/co-processing-decompress-lz4 0 100 $filesize 3 $first_compressed_chunk_lz4 $chunk_index > /dev/shm/switching-decomp-lz4.log 2>&1
        # optional short sleep so the loop doesn’t spin at 100 % CPU
        sleep 0.1
    done
fi

if [ "$run_deflate" = true ]; then
    start=$(date +%s) # current epoch-seconds, run for 15s
    while (( $(date +%s) - start < 15 )); do
        # ---- your work here ----
        ./build/co-processing-decompress-deflate 0 100 $filesize 3 $first_compressed_chunk $chunk_index > /dev/shm/switching-decomp-dflt.log 2>&1
        # optional short sleep so the loop doesn’t spin at 100 % CPU
        sleep 0.1
    done
fi