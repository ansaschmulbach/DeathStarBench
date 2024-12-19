#!/bin/bash
tcpdump -i eth0 &
exec tcpdump -i eth0 & && ComposePostService
