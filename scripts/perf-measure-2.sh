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
	sudo perf stat -e cycles:u,cycles:k,instructions:u,instructions:k,resource_stalls.rob,icache.ifetch_stall,icache.misses,cycle_activity.stalls_l1d_pending,cycle_activity.stalls_l2_pending -M L1MPKI,L2MPKI --cgroup=system.slice/docker-"$process_id".scope -a sleep 60 > "$name"-1.data
	sudo perf stat -e itlb_misses.stlb_hit,itlb_misses.miss_causes_a_walk,iTLB-load-misses,dTLB-load-misses,branch-misses --cgroup=system.slice/docker-"$process_id".scope -a sleep 60 > "$name"-2.data
done
cd ..
