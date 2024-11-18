#!/bin/bash
container=d083bd4510bd
docker exec -d $container ./pin-script.sh
echo "Process started in container $container"
