#!/bin/bash

# CLI switches
# Parse command-line arguments
per_regex=false
while [[ $# -gt 0 ]]; do
    case $1 in
        --per-regex)
            per_regex=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Variables
local_file="data/US_Accidents_Dec21_updated.csv"
only_descriptions_file="/dev/shm/us-accidents-descriptions"
regex_rules_files="us-accidents-queries"
compiled_regex_rules_file="/tmp/regex_rules"
dpdk_params='"-l 0,1,2,3 -n 1 -a 03:00.0,class=regex"'
iter_times=10

# Function to run experiments
run_experiments() {
    local regex_counter=$1
    local regex=$2

    echo "Running experiments for regex $regex_counter: $regex..."

    # make results dir
    mkdir -p results

    # run experiment with no parameters (buf size=1024, grp size=64)
    echo "Running full-file experiment, Regex: $regex_counter..."
    sudo rxpbench -D "$dpdk_params" --input-mode text_file -f "$only_descriptions_file" -d doca_regex -r "$compiled_regex_rules_file".rof2.binary -c 1 -n "$iter_times" -b 192408069 > results/whole-regex-"$regex_counter".txt

    # run experiment with variable buffer size
    buffer_sizes=(1 2 4 8 16 32 64 128 256 512 1024 2048 4096 16383)
    for buffer_size in "${buffer_sizes[@]}"
    do
        echo "Buffer size: $buffer_size, Regex: $regex_counter..."
        sudo rxpbench -D "$dpdk_params" --input-mode text_file -f "$only_descriptions_file" -d doca_regex -r "$compiled_regex_rules_file".rof2.binary -c 1 -l "$buffer_size" -n "$iter_times" -b 192408069 > results/buffer-"$buffer_size"-regex-"$regex_counter".txt
    done

    # run experiment with variable group size (default=64, maximum=<1024)
    group_sizes=(1 2 4 8 16 32 64 128 256 512 1023)
    for group_size in "${group_sizes[@]}"
    do
        echo "Group size: $group_size, Regex: $regex_counter..."
        sudo rxpbench -D "$dpdk_params" --input-mode text_file -f "$only_descriptions_file" -d doca_regex -r "$compiled_regex_rules_file".rof2.binary -c 1 -g "$group_size" -n "$iter_times" -b 192408069 > results/group-"$group_size"-regex-"$regex_counter".txt
    done

    # run combination of buffer and group size
    for buffer_size in "${buffer_sizes[@]}"
    do
        for group_size in "${group_sizes[@]}"
        do
            echo "Buffer size: $buffer_size, Group size: $group_size, Regex: $regex_counter..."
            sudo rxpbench -D "$dpdk_params" --input-mode text_file -f "$only_descriptions_file" -d doca_regex -r "$compiled_regex_rules_file".rof2.binary -c 1 -l "$buffer_size" -g "$group_size" -n "$iter_times" -b 192408069 > results/buffer-"$buffer_size"-group-"$group_size"-regex-"$regex_counter".txt
        done
    done
}

# create the file on shm
cut -d',' -f10 "$local_file" | tail -n +2 > "$only_descriptions_file"

if [ "$per_regex" = true ]; then
    # Read the regex file line by line
    while IFS=, read -r counter regex; do
        # Trim leading and trailing whitespace from regex
        regex=$(echo "$regex" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
        echo "Compiled regex $counter: $regex..."

        # compile the current regex
        echo "$counter, $regex" > current_regex.txt
        rxpc -f current_regex.txt -p 0.01 -o "$compiled_regex_rules_file"
        
        # run experiments for the compiled regex
        run_experiments "$counter" "$regex"
    done < "$regex_rules_files"
fi

# All at the same time
rxpc -f "$regex_rules_files" -p 0.01 -o "$compiled_regex_rules_file"
run_experiments "all" "all"
