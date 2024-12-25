#!/bin/bash
exec UrlShortenService &
sleep 120 
tcpdump -i eth0 -w data.pcap &
TCPDUMP_PID=$!
sleep 60
kill $TCPDUMP_PID
wait
