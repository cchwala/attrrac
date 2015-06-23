#!/bin/sh

while test 1 -eq 1 
do
	sleep 5
	./attrrac get_reset_count
	sleep 5
	./attrrac get_case_temp
	sleep 50
	./attrrac radar
done
