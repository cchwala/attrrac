#!/bin/bash

DATA_DIR="/root/data_to_send"

SERVER="gimpel"
USER="chwala-c"
SERVER_DIR="data_inbox/attrra"

cd $DATA_DIR

while true;
do
	# do nothing if no file is found
	shopt -s nullglob
	
	for i in *
	do
		scp $i $USER@$SERVER:$SERVER_DIR
		rm $i
	done
	sleep 10;
done


