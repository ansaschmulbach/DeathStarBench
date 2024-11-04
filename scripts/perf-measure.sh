#!/bin/bash

timestamp=$(date +"%Y%m%d_%H%M%S")
output_dir="output_$timestamp"
mkdir "$output_dir"
cd "$output_dir"
echo "Writing results to $output_dir"
docker ps --no-trunc | tail -n +2 | while read -r line; do
	process_id=$(echo $line | awk '{print $1}')
	name=$(echo $line | awk '{print $3}' | tr -cd '[:alnum:]')
	echo "recording for service $name"
	sudo perf stat -e cycles:u,cycles:k,instructions:u,instructions:k,iTLB-load-misses,dTLB-load-misses,branch-misses --cgroup=system.slice/docker-"$process_id".scope -a sleep 60 > "$name".data
done
cd ..
