#!/bin/bash

# Get time from remote server (imk-ifu-gipel, linux workstation)
date -s "$(ssh chwala-c@gimpel date -u +%m%d%H%M%Y.%S)"
hwclock --systohc

watchdog set 20
watchdog enable

##########################################################
# !!! DO NEVER COMMENT OUT THIS WATCHDOG FEED SCRIPT !!! #
##########################################################
cd /root
nohup ./feed_watchdog_loop.sh > /dev/null 2>&1 &

####################################################
# Scripts for file transfer and DAQ of disdrometer #
####################################################
#nohup ./read_distrometer.sh > /dev/null 2>&1 &
nohup ./move_data_to_server.sh > /dev/null 2>&1 &

#################
# Start ATTRRAC #
#################
cd /root/attrrac
nohup nice -n -20 ./attrracd > out_attrracd.log &
sleep 2
./attrrac set_default
sleep 1

# Select ATTTRAC modes #
#
############################
# slow loop
############################
./attrrac set_n_samples 32
sleep 1
./attrrac set_pw 20
./attrrac set_adc_delay 0
sleep 1
./attrrac set_loop_freq 20
sleep 1
./attrrac start_slow_loop
###########################

##############################
# burst read every minute
##############################
#./attrrac set_n_samples 75000
#sleep 1
#nohup /root/trigger_burst_read_every_minute.sh > /dev/null 2>&1 &
#############################

#########################################
# radar measurement every 10 seconds
#########################################
#./attrrac set_n_samples 512
#sleep 1
#nohup /root/trigger_radar_every_10_sec.sh > /dev/null 2>&1 &

