#!/bin/bash
container=d083bd4510bd
pid=$(docker exec $container pgrep -f pin-script.sh)
  
mkdir output
cd output
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
