#!/bin/bash
image_names=("socialnetwork-social-graph-service" "socialnetwork-unique-id-service" "socialnetwork-url-shorten-service" "socialnetwork-home-timeline-service" "socialnetwork-user-timeline-service" "socialnetwork-user-service" "socialnetwork-user-mention-service" "socialnetwork-media-service" "socialnetwork-compose-post-service" "socialnetwork-text-service" "socialnetwork-post-storage-service")
for image_name in "${image_names[@]}"; do
	container=$(docker ps | grep $image_name | awk '{print $1}')
	docker exec -d $container sh -c "\$NAME"
	echo "Process started in container $container"
done

