#!/bin/bash

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

# Loop through each service and process its associated container
for service in "${services[@]}"; do
  # Convert the service name to dashed format
  dashed_name=$(echo "$service" | sed 's/\([a-z]\)\([A-Z]\)/\1-\2/g' | tr '[:upper:]' '[:lower:]')
  
  # Define the container name
  container_name="socialnetwork-${dashed_name}-1"
  
  echo "Checking container: $container_name"
  
  # Find the PID of the tcpdump process inside the container
  tcpdump_pid=$(docker exec "$container_name" pgrep tcpdump)
  
  if [ -n "$tcpdump_pid" ]; then
    echo "tcpdump process found in container $container_name (PID: $tcpdump_pid). Killing it..."
    
    # Kill the tcpdump process inside the container
    docker exec "$container_name" kill "$tcpdump_pid"
    
    if [ $? -eq 0 ]; then
      echo "Successfully killed tcpdump process in container $container_name."
    else
      echo "Failed to kill tcpdump process in container $container_name."
    fi
  else
    echo "No tcpdump process found in container $container_name."
  fi
done

echo "Script execution completed!"

