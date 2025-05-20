#!/bin/bash

# Trap Ctrl+C (SIGINT) and send it to all background processes
trap "echo 'Stopping services...'; pkill -P $$; exit" SIGINT

# Remove dsb-sock-* files
# rm -f dsb-sock-*

# List of services
services=(
    "ComposePostService"
    "HomeTimelineService"
    "MediaService"
    "PostStorageService"
    "SocialGraphService"
    "TextService"
    "UniqueIdService"
    "UrlShortenService"
    "UserMentionService"
    "UserService"
    "UserTimelineService"
)

# Start each service in the background and save PIDs
pids=()
for service in "${services[@]}"; do
    ./cmake-build/src/$service/$service &
    pids+=($!)  # Store the PID of the process
    echo "$service started in the background with PID ${pids[-1]}."
done

# Allow some time for services to initialize
sleep 1

# chmod 777 dsb-sock*

# docker restart socialnetwork-nginx-thrift-1

echo "All services started. Press Ctrl+C to stop."

# Wait for all background processes
for pid in "${pids[@]}"; do
    wait $pid
done

