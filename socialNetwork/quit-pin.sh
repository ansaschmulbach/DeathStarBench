#!/bin/bash
image_names=("socialnetwork-social-graph-service" "socialnetwork-unique-id-service" "socialnetwork-url-shorten-service" "socialnetwork-home-timeline-service" "socialnetwork-user-timeline-service" "socialnetwork-user-service" "socialnetwork-user-mention-service" "socialnetwork-media-service" "socialnetwork-compose-post-service" "socialnetwork-text-service" "socialnetwork-post-storage-service")
mkdir output
cd output

for image_name in "${image_names[@]}"; do
	container=$(docker ps | grep $image_name | awk '{print $1}')
	pid=$(docker exec $container pgrep -f pin-script.sh)
  
	if [ -n "$pid" ]; then
	  # Kill the process
	  docker exec $container kill $pid
	  echo "Process $pid killed in container $container"
		mkdir $container
		cd $container
		docker cp $container:/social-network-microservices/dmem_out .
		docker cp $container:/social-network-microservices/imem_out .
		cd ..
	else
	  echo "No matching process found in container $container"
	fi
done
