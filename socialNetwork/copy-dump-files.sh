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

cd tcpdump_out
# Loop through services and perform the Docker copy
for service in "${services[@]}"; do
  # Convert the service name to dashed format
  dashed_name=$(echo "$service" | sed 's/\([a-z]\)\([A-Z]\)/\1-\2/g' | tr '[:upper:]' '[:lower:]')
  
  # Define the container name
  container_name="socialnetwork-${dashed_name}-1"
  
  # Perform the Docker copy
  echo "Copying capture.pcap from container ${container_name} to ${dashed_name}.pcap..."
  docker cp "${container_name}:/social-network-microservices/capture.pcap" "${dashed_name}.pcap"
  
  if [ $? -eq 0 ]; then
    echo "Successfully copied to ${dashed_name}.pcap"
  else
    echo "Failed to copy from container ${container_name}. Check the container or file path."
  fi
done

echo "All Docker copy operations completed!"

