#!/bin/bash
cd /social-network-microservices

./pin/pin -t /social-network-microservices/pin/source/tools/MyPinTool/obj-intel64/MyPinTool.so -o out -- $NAME 

