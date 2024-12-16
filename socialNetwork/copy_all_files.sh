#!/bin/bash

# Get the list of all running Docker containers
containers=$(docker ps --format '{{.Names}}')

# Loop through each container
for container in $containers; do
  # Create a new folder with the name of the container
  mkdir -p "$container"
  
  # Get the list of files ending with trace_out or trace in the container
  files=$(docker exec "$container" sh -c 'ls /social-network-microservices/*trace_out /social-network-microservices/*trace 2>/dev/null')
  
  # Loop through each file and copy it to the new folder
  for file in $files; do
    docker cp "$container":"$file" "$container"/
  done
done
