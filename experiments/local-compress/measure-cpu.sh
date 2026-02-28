#!/usr/bin/env bash
set -euo pipefail
# set -x

prefix=""
count=0

while (( $# )); do
  case "$1" in
    --bf2-host) prefix="bf2-host"; ((++count)); shift ;;
    --bf2-dpu)  prefix="bf2-dpu";  ((++count)); shift ;;
    --bf3-host) prefix="bf3-host"; ((++count)); shift ;;
    --bf3-dpu)  prefix="bf3-dpu";  ((++count)); shift ;;
    --) shift; break ;;
    --*) echo "Unknown option: $1" >&2; exit 2 ;;
    *)  break ;;
  esac
done

if (( count != 1 )); then
  echo "Error: you must specify exactly one of --bf2-host, --bf2-dpu, --bf3-host, --bf3-dpu" >&2
  exit 1
fi

# create results dir
mkdir -p build && mkdir -p build/results
rm -rf build/results/${prefix} && mkdir -p build/results/${prefix}

# Default binary path
turbobench_path="./bin/TurboBench/turbobench"

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

# Build binary if missing
# if [ ! -f "bin/TurboBench/turbobench" ]; then
#   echo "Building turbobench binary..."
#   cd bin
#   git clone --depth=1 --recursive https://github.com/powturbo/TurboBench.git >/dev/null 2>&1
#   cp TurboBench/makefile TurboBench/makefile.orig
#   cp makefile_turbobench TurboBench/makefile
#   cd TurboBench
#   make clean >/dev/null 2>&1
#   make >/dev/null 2>&1
#   mv makefile.orig makefile
#   cd ../..
# fi

echo "Benchmarking zlib from $turbobench_path..."
# Iterate over each file found within the directory and subdirectories
find corpora/silesia -type d -name ".git" -prune -o -type f ! -name "README.md" -size -120M -print0 | while IFS= read -r -d '' file; do
    filename=$(basename "$file")
    parentdir=$(basename "$(dirname "$file")")
    if [ "$filename" = ".git" ]; then
        continue
    fi
    # Call your binary with the file path as an argument
    echo "Testing $file as input..."
    cp $file $filename
    sleep 1
    $turbobench_path -ezlib,1,2,3 -p=7 $filename
    mv $filename.tbb build/results/${prefix}/$parentdir.$filename.orig.dflt.tbb
    rm -rf $filename
    # Test with specific buffer sizes
    for size in "${buffer_sizes[@]}"; do
        # Fill the buffer with the content of the original file
        echo "Filling $size buffers with $file as input..."
        fill_buffer "$file" "$size"
        cp /dev/shm/input $filename
        sleep 1
        # Call your binary with the file path as an argument
        $turbobench_path -ezlib,1,2,3  -p=7 $filename
        mv $filename.tbb build/results/${prefix}/$parentdir.$filename.$size.dflt.tbb
        rm -rf $filename
    done
done

cd build/results/${prefix}
# Use awk to merge the original file size CSVs results
awk '(NR == 1) || (FNR > 1)' *orig.dflt.tbb > cpu-orig-dflt.csv
# Use awk to merge all variable file size CSVs results
find . -type f -name '*dflt.tbb' ! -name '*orig.dflt.tbb' -print0 | xargs -0 awk '(NR == 1) || (FNR > 1)' > cpu-variable-dflt.csv

cd ../../..
echo "Benchmarking lz4 from $turbobench_path..."
# Iterate over each file found within the directory and subdirectories
find corpora/silesia -type d -name ".git" -prune -o -type f ! -name "README.md" -size -120M -print0 | while IFS= read -r -d '' file; do
    filename=$(basename "$file")
    parentdir=$(basename "$(dirname "$file")")
    if [ "$filename" = ".git" ]; then
        continue
    fi
    # Call your binary with the file path as an argument
    echo "Testing $file as input..."
    cp $file $filename
    sleep 1
    $turbobench_path -elz4,1,3,6 -p=7 $filename
    mv $filename.tbb build/results/${prefix}/$parentdir.$filename.orig.lz4.tbb
    rm -rf $filename
    # Test with specific buffer sizes
    for size in "${buffer_sizes[@]}"; do
        # Fill the buffer with the content of the original file
        echo "Filling $size buffers with $file as input..."
        fill_buffer "$file" "$size"
        cp /dev/shm/input $filename
        sleep 1
        # Call your binary with the file path as an argument
        $turbobench_path -elz4,1,3,6 -p=7 $filename
        mv $filename.tbb build/results/${prefix}/$parentdir.$filename.$size.lz4.tbb
        rm -rf $filename
    done
done

cd build/results/${prefix}
# Use awk to merge the original file size CSVs results
awk '(NR == 1) || (FNR > 1)' *orig.lz4.tbb > cpu-orig-lz4.csv
# Use awk to merge all variable file size CSVs results
find . -type f -name '*lz4.tbb' ! -name '*orig.lz4.tbb' -print0 | xargs -0 awk '(NR == 1) || (FNR > 1)' > cpu-variable-lz4.csv

cd ../../..
echo "Benchmarking libdeflate from $turbobench_path..."
# Iterate over each file found within the directory and subdirectories
find corpora/silesia -type d -name ".git" -prune -o -type f ! -name "README.md" -size -120M -print0 | while IFS= read -r -d '' file; do
    filename=$(basename "$file")
    parentdir=$(basename "$(dirname "$file")")
    if [ "$filename" = ".git" ]; then
        continue
    fi
    # Call your binary with the file path as an argument
    echo "Testing $file as input..."
    cp $file $filename
    sleep 1
    $turbobench_path -elibdeflate,1,2,3 -p=7 $filename
    mv $filename.tbb build/results/${prefix}/$parentdir.$filename.orig.libdeflate.tbb
    rm -rf $filename
    # Test with specific buffer sizes
    for size in "${buffer_sizes[@]}"; do
        # Fill the buffer with the content of the original file
        echo "Filling $size buffers with $file as input..."
        fill_buffer "$file" "$size"
        cp /dev/shm/input $filename
        sleep 1
        # Call your binary with the file path as an argument
        $turbobench_path -elibdeflate,1,2,3 -p=7 $filename
        mv $filename.tbb build/results/${prefix}/$parentdir.$filename.$size.libdeflate.tbb
        rm -rf $filename
    done
done

cd build/results/${prefix}
# Use awk to merge the original file size CSVs results
awk '(NR == 1) || (FNR > 1)' *orig.libdeflate.tbb > cpu-orig-libdeflate.csv
# Use awk to merge all variable file size CSVs results
find . -type f -name '*libdeflate.tbb' ! -name '*orig.libdeflate.tbb' -print0 | xargs -0 awk '(NR == 1) || (FNR > 1)' > cpu-variable-libdeflate.csv
