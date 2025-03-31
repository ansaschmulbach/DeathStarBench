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

mkdir entrypoints
cd entrypoints
# Loop through services and create scripts
for service in "${services[@]}"; do
  # Convert the service name to dashed format
  dashed_name=$(echo "$service" | sed 's/\([a-z]\)\([A-Z]\)/\1-\2/g' | tr '[:upper:]' '[:lower:]')
  
  # Create the entrypoint script
  script_name="${dashed_name}-entrypoint.sh"
  
  echo "Creating ${script_name}..."
  
  # Write the content to the script
  cat <<EOF > "$script_name"
#!/bin/bash
tcpdump -i eth0 -w capture.pcap &
$service
EOF
  
  # Make the script executable
  chmod +x "$script_name"
done

echo "All entrypoint scripts created successfully!"

