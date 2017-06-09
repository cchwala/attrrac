#!/bin/bash

cd /root/attrrac

while true;
do
	# trigger one radar measurement
	./attrrac radar
	
	sleep 10
done
	
