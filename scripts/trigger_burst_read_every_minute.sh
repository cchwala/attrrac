#!/bin/bash

cd /root/attrrac

while true;
do
	# trigger one burst measurement
	./attrrac start

	#
	# THIS DID NOT WORK. STILL REBOOTS EVERY 2 HOURS OR SO
	#	
	# wait 30 sec and restart attrracd. This way system frezzes will
	# hopefully be reduced
	#
	#sleep 20
	#./attrrac quit
	#sleep 8
	#nohup nice -n -20 ./attrracd > out_attrracd.log &
	#sleep 1
	#./attrrac set_default
	#sleep 1
	#./attrrac set_n_samples 75000
	#
	# wait another 30 seconds and loop
	#sleep 30;
	
	sleep 60
done
