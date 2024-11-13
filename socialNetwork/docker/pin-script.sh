#!/bin/bash
cd /social-network-microservices

./pin/pin -t /social-network-microservices/sst-elements/src/sst/elements/prospero/tracetool/sstmemtrace.so -- $NAME &

PID=$!
sleep 10
kill -SIGABRT $PID
