#!/bin/bash
image_names=("socialnetwork-social-graph-service" "socialnetwork-unique-id-service" "socialnetwork-url-shorten-service" "socialnetwork-home-timeline-service" "socialnetwork-user-timeline-service" "socialnetwork-user-service" "socialnetwork-user-mention-service" "socialnetwork-media-service" "socialnetwork-compose-post-service" "socialnetwork-text-service" "socialnetwork-post-storage-service")

mkdir champsim-traces
cd champsim-traces

for image_name in "${image_names[@]}"; do
	container=$(docker ps | grep $image_name | awk '{print $1}')
	pid=$(docker exec $container sh -c "pgrep -f \$NAME")
	if [ -n "$pid" ]; then
		docker exec $container kill $pid
		echo "Process $pid killed in container $container"
		docker cp $container:/social-network-microservices/champsim.trace ./${image_name}-champsim.trace
	else
		echo "No matching process found in container $container"
	fi
done

