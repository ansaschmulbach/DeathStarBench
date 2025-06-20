#!/bin/bash

set -euo pipefail  # exit on error, treat unset vars as error, and fail on pipe errors

cleanup() {
    echo "🧹 Cleaning up..."
    # Kill background services
    pkill -P $$ || true
    # Kill sudo keeper if running
    [[ -n "$sudo_keeper_pid" ]] && kill "$sudo_keeper_pid" 2>/dev/null || true
    exit
}

# Register trap on exit, interrupt, error
trap cleanup EXIT INT ERR


# Ensure sudo credentials are active before any background sudo commands
sudo -v

# (Optional) Keep sudo session alive for long runs
( while true; do sudo -v; sleep 60; done ) &
sudo_keeper_pid=$!

# trap "echo 'Stopping...'; kill $sudo_keeper_pid 2>/dev/null; pkill -P $$; exit" SIGINT EXIT
# Cleanup function called on any exit

# Remove dsb-sock-* files
# rm -f dsb-sock-*

# List of services
services=(
    "ComposePostService"
    "HomeTimelineService"
    "MediaService"
    "PostStorageService"
    "SocialGraphService"
#    "TextService"
    "UniqueIdService"
    "UrlShortenService"
    "UserMentionService"
    "UserService"
    "UserTimelineService"
)

# Start each service in the background and save PIDs
# pids=()
# for service in "${services[@]}"; do
#     ./cmake-build/src/$service/$service &
#     pids+=($!)  # Store the PID of the process
#     echo "$service started in the background with PID ${pids[-1]}."
# done
# 
# # Allow some time for services to initialize
# sleep 1

# chmod 777 dsb-sock*

# docker restart socialnetwork-nginx-thrift-1

# echo "All services started. Press Ctrl+C to stop."

# Event groups for perf
event_groups=(
    "cycles:u,instructions:u,branches:u,branch-misses:u,context-switches"
    "cycles:u,instructions:u,frontend_retired.itlb_miss,br_misp_retired.all_branches:u,frontend_retired.l1i_miss:u"
    "cycles:u,instructions:u,mem_load_retired.l2_miss:u,offcore_requests.demand_rfo:u"
    "cycles:u,instructions:u,dtlb_store_misses.stlb_hit:u,dtlb_load_misses.miss_causes_a_walk:u"
    "cycles:u,instructions:u,dtlb_load_misses.stlb_hit:u,dtlb_store_misses.miss_causes_a_walk:u"
    "cycles:u,instructions:u,cycle_activity.cycles_l3_miss:u,cycle_activity.cycles_l2_miss:u,cycle_activity.cycles_l1d_miss:u"
)

# Create timestamped result directory
timestamp=$(date "+%Y-%m-%d_%H-%M-%S")
base_log_dir="run_logs/run_${timestamp}"
mkdir -p "$base_log_dir"

restart_services() {
    echo "🔁 Restarting services..."

    # Kill any previously running child processes
    pkill -P $$ || true
    sleep 1

    pids=()
    for service in "${services[@]}"; do
        ./cmake-build/src/$service/$service 2>&1 | tee -a "$group_dir/services.log" &
        pids+=($!)
        echo "  $service started with PID ${pids[-1]}"
    done
    sleep 2
}


# Run each group
i=1
for events in "${event_groups[@]}"; do
    unit_name="TextServicePerfGroup$i"
    echo -e "\n▶️  Running perf group #$i with events:\n$events"

    group_dir="$base_log_dir/group$i"
    mkdir -p "$group_dir"

    restart_services

    group_dir="$base_log_dir/group$i"
    mkdir -p "$group_dir"

    unit_name="TextServicePerfGroup$i"

    # Create a log for capturing launch errors (from sudo/systemd-run)
    launch_err_log="$group_dir/launch_error.log"

    # Stop/reset the unit if it previously existed
    sudo systemctl stop "${unit_name}.scope" 2>/dev/null || true
    sudo systemctl reset-failed "${unit_name}.scope" 2>/dev/null || true

    # Run systemd-run in the background and capture errors to log
    sudo systemd-run --unit="$unit_name" --scope -p Slice=workload.slice nice -n -20 \
        perf stat -e "$events" \
        ./cmake-build/src/TextService/TextService \
        > "$group_dir/textservice_output.log" \
        2> "$group_dir/textservice_perf.log" \
        2>> "$launch_err_log" &
    
    # Give systemd a moment to create the scope
    sleep 1
    
    # Check if the TextService systemd unit actually started
    if ! systemctl is-active --quiet "${unit_name}.scope"; then
        echo "❌ Failed to launch systemd-run for $unit_name"
        echo "🔎 Launch error (from sudo/systemd-run):"
        echo "----------------------------------------"
        cat "$launch_err_log"
        echo "----------------------------------------"
        exit 1
    fi


    # Run TextService with perf under systemd-run, capturing stderr from systemd-run
    # if ! sudo systemd-run --unit="$unit_name" --scope -p Slice=workload.slice nice -n -20 \
    #     perf stat -e "$events" \
    #     ./cmake-build/src/TextService/TextService \
    #     > "$group_dir/textservice_output.log" \
    #     2> "$group_dir/textservice_perf.log" 2>> "$launch_err_log" & ; then
    #     echo "❌ Failed to launch systemd-run for $unit_name"
    #     echo "🔎 Launch error (from sudo/systemd-run):"
    #     echo "----------------------------------------"
    #     cat "$launch_err_log"
    #     echo "----------------------------------------"
    #     exit 1
    # fi


    # Stop and clean up previous unit if it exists
    # sudo systemctl stop "${unit_name}.scope" 2>/dev/null || true
    # sudo systemctl reset-failed "${unit_name}.scope" 2>/dev/null || true

    # if ! sudo systemd-run --unit="$unit_name" --scope -p Slice=workload.slice nice -n -20 \
    #     perf stat -e "$events" \
    #     ./cmake-build/src/TextService/TextService \
    #     > "$group_dir/textservice_output.log" \
    #     2> "$group_dir/textservice_perf.log" & then
    #     echo "❌ Failed to launch systemd-run for $unit_name"
    #     exit 1
    # fi

    # sleep 1  # Give the unit a moment to start

    echo "🚀 Running workload generator..."
    python3 gen-random-workload.py 100

    echo "⏳ Waiting for $unit_name to finish..."
    while systemctl is-active --quiet "${unit_name}.scope"; do
        sleep 1
    done
    pkill -P $$

    echo "✅ Group #$i complete. Logs written to:"
    echo "    textservice_output_group${i}.log"
    echo "    textservice_perf_group${i}.log"

    ((i++))
done

echo -e "\n🏁 All perf groups complete. Press Ctrl+C to stop remaining services."

# Keep services alive until user exits
# for pid in "${pids[@]}"; do
#     wait $pid
# done

# sudo systemd-run --scope -p Slice=workload.slice nice -n -20 \
#     perf stat -e cycles:u \
#               -e instructions:u \
#               -e branches:u \
#               -e branch-misses:u \
#               -e context-switches \
#               -e inst_retired.any:u \
#               ./cmake-build/src/TextService/TextService \
#     > textservice_output.log 2> textservice_perf.log &
# 
# textservice_pid=$!
# echo "TextService started with PID $textservice_pid"
# 
# # Run the workload generator
# echo "Running workload generator..."
# python3 gen-random-workload.py 100
# 
# # Wait for TextService to finish
# wait $textservice_pid
# 
# # Wait for all background processes
# # for pid in "${pids[@]}"; do
# #     wait $pid
# # done
# 
