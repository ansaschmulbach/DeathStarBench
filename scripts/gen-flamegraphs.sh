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
	sudo perf record -e cpu-clock --cgroup=system.slice/docker-"$process_id".scope -a -g -o "$name".data -- sleep 60
	sudo perf script -i "$name".data > "$name".perf
	../FlameGraph/stackcollapse-perf.pl "$name".perf > "$name".folded
	../FlameGraph/flamegraph.pl "$name".folded > "$name".svg
done
cd ..
