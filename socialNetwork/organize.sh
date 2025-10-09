#!/bin/bash

mkdir -p organized

for file in *.out; do
    # Remove prefix and suffix
    core=$(basename "$file" .out | sed 's/^out\.//')

    # Extract parts
    service=$(echo "$core" | cut -d_ -f1)
    mem_type=$(echo "$core" | grep -oE 'dmem|imem')
    thread=$(echo "$core" | grep -oE 'thread_[0-9]+' | cut -d_ -f2)
    request=$(echo "$core" | grep -oE 'request_[0-9]+' | cut -d_ -f2)
    # timestamp=$(echo "$core" | grep -oE 'timestep_[0-9]+' | cut -d_ -f2)

    # Build destination path
    # dir="organized/$mem_type/$service/thread_$thread/timestamp_$timestamp"
    dir="organized/$mem_type/$service/thread_$thread"
    mkdir -p "$dir"

    # Move file
    mv "$file" "$dir/request_$request.out"
done


