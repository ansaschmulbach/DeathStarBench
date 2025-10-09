#!/bin/bash
# List of services to launch
services=(
  ComposePostService
  HomeTimelineService
  MediaService
  PostStorageService
  SocialGraphService
  TextService
  UniqueIdService
  UrlShortenService
  UserMentionService
  UserService
  UserTimelineService
)

pids=()

# Handle Ctrl+C by killing all background processes
cleanup() {
  echo "Caught SIGINT. Terminating all services..."
  for pid in "${pids[@]}"; do
    kill "$pid" 2>/dev/null
  done
  wait
  mkdir -p "logs_$(date +%Y%m%d_%H%M%S)"
  mv *.out pinlog-* "logs_$(date +%Y%m%d_%H%M%S)"
  exit 0
}

trap cleanup SIGINT

# Launch each service with its own PIN log file
for SERVICE_NAME in "${services[@]}"; do
  echo "Launching $SERVICE_NAME"
  LOG_FILE="pinlog-${SERVICE_NAME}.txt"

  /data/sanchez/users/ansa/pin/pin -t /data/sanchez/users/ansa/hierarchical-pref-2/obj-intel64/HierarchicalPrefetchPintool.so -f /data/sanchez/users/ansa/hp-soft/out/$SERVICE_NAME.out -o $SERVICE_NAME -- \
    ./cmake-build/src/"$SERVICE_NAME"/"$SERVICE_NAME" \
    > "$LOG_FILE" 2>&1 &

  pids+=($!)
done

# Wait for all background services
wait

