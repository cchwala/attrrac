#!/bin/bash

STX=$(printf "\x2")
ETX=$(printf "\x3")
DATA_DIR="/root/data_to_send"
TEMP_DIR="/root/tmp_data"

# COM2 is file descriptor 3
3<>/dev/ttyS1


# set port to raw mode
stty -F /dev/ttyS1 raw

while true;
do
	# read character as long as STX is read (should be the first)
	read -u 3 -r -s -n 1 CHAR
	if [ "$CHAR" == "$STX" ]
	then
		#echo "found stx"
		DATA=""
		
		# build timestamp filename
		filename=${TEMP_DIR}/distro_`date '+%Y%m%d_%H%M_%S'`.dat
		
		# read rest of serial stream to DATA till  ETX is read
		while true;
		do
			read -u 3 -r -s -n 1 CHAR
			if [ "$CHAR" == "$ETX" ]
			then
				#echo "found etx"
				break
			fi
			# append char from stream to data string 
			DATA=${DATA}$CHAR
		done
		
		# write data string to file
		echo $DATA >> "$filename"
		gzip "$filename"
		mv "$filename.gz" "$DATA_DIR"
	fi
done


