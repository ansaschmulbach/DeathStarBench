#!/bin/bash
image_names=("socialnetwork-social-graph-service" "socialnetwork-unique-id-service" "socialnetwork-url-shorten-service" "socialnetwork-home-timeline-service" "socialnetwork-user-timeline-service" "socialnetwork-user-service" "socialnetwork-user-mention-service" "socialnetwork-media-service" "socialnetwork-compose-post-service" "socialnetwork-text-service" "socialnetwork-post-storage-service")
for image_name in "${image_names[@]}"; do
	container=$(docker ps | grep $image_name | awk '{print $1}')
	docker exec -d $container sh -c "./pin/pin -t /social-network-microservices/pin/source/tools/MyPinTool/obj-intel64/MyPinTool.so -o out -- \$NAME"
	echo "Process started in container $container"
done

sleep 5 
../wrk2/wrk -D exp -t 1 -c 1 -d 1 -L -s ./wrk2/scripts/social-network/compose-post.lua http://localhost:8080/wrk2-api/post/compose -R 1
sleep 5

timestamp=$(date +"%Y-%m-%d_%H-%M-%S")
mkdir -p "output/$timestamp"
cd "output/$timestamp"
for image_name in "${image_names[@]}"; do
	container=$(docker ps | grep $image_name | awk '{print $1}')
	pid=$(docker exec $container sh -c "pgrep -f \$NAME")
	if [ -n "$pid" ]; then
	  # Kill the process
	  docker exec $container kill $pid
	  sleep 5
	  echo "Process $pid killed in container $container"
		mkdir $image_name
		cd $image_name
		docker cp $container:/social-network-microservices/dmem_out .
		docker cp $container:/social-network-microservices/imem_out .
		cd ..
	else
	  echo "No matching process found in container $container"
	fi
done

