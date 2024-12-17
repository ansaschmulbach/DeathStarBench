#!/bin/bash

timestamp=$(date +"%Y%m%d_%H%M%S")
output_dir="perfstat_output_$timestamp"
echo 0 | sudo tee /proc/sys/kernel/nmi_watchdog
mkdir "$output_dir"
cd "$output_dir"
echo "Writing results to $output_dir"
docker ps --no-trunc | tail -n +2 | while read -r line; do
	process_id=$(echo $line | awk '{print $1}')
	name=$(echo $line | awk '{print $3}' | tr -cd '[:alnum:]')
	echo "recording for service $name"
	sudo perf stat -e cycles:u -e cycles:k -e instructions:u -e instructions:k --cgroup=system.slice/docker-"$process_id".scope -a sleep 60 > "$name".data
	sudo perf stat -e resource_stalls.rob:u -e icache.ifetch_stall:u -e icache.misses:u -e cycle_activity.stalls_l1d_pending:u -e cycle_activity.stalls_l2_pending:u --cgroup=system.slice/docker-"$process_id".scope -a sleep 60 >> "$name".data
	sudo perf stat -e resource_stalls.rob:k -e icache.ifetch_stall:k -e icache.misses:k -e cycle_activity.stalls_l1d_pending:k -e cycle_activity.stalls_l2_pending:k --cgroup=system.slice/docker-"$process_id".scope -a sleep 60 >> "$name".data
	sudo perf stat -e itlb_misses.stlb_hit:u -e itlb_misses.miss_causes_a_walk:u -e iTLB-load-misses:u -e dTLB-load-misses:u -e branches:u -e branch-misses:u --cgroup=system.slice/docker-"$process_id".scope -a sleep 60 >> "$name".data
	sudo perf stat -e itlb_misses.stlb_hit:k -e itlb_misses.miss_causes_a_walk:k -e iTLB-load-misses:k -e dTLB-load-misses:k -e branches:k -e branch-misses:k --cgroup=system.slice/docker-"$process_id".scope -a sleep 60 >> "$name".data
done
echo "Finished writing results to $output_dir"
cd ..
