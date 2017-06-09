#!/bin/bash

main() {
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

    #######################
    # Select ATTTRA modes #
    #######################
    # !!! ONLY UNCOMMENT ONE OF THE FOLLOWING FUNCTIONS !!!
    start_attrra_slow_loop
    #start_burst_read_every_minute
    #start_radar_every_10_sec
}


###############################
# Starting and default config #
###############################
start_attrra() {
    cd /root/attrrac
    nohup nice -n -20 ./attrracd > out_attrracd.log &
    sleep 2
}

set_attrra_default_config() {
    ./attrrac set_default
    sleep 1
}


################################
# Different modes of operation #
################################

start_attrra_slow_loop() {
    cd /root/attrrac

    start_attrra
    set_attrra_default_config

    ./attrrac set_n_samples 32
    sleep 1
    ./attrrac set_pw 20
    ./attrrac set_adc_delay 0
    sleep 1
    ./attrrac set_loop_freq 20
    sleep 1
    ./attrrac start_slow_loop
}

start_burst_read_every_minute() {
    cd /root/attrrac

    start_attrra
    set_attrra_default_config

    ./attrrac set_n_samples 75000
    sleep 1
    nohup /root/trigger_burst_read_every_minute.sh > /dev/null 2>&1 &
}

start_radar_every_10_sec() {
    cd /root/attrrac

    start_attrra
    set_attrra_default_config
    
    ./attrrac set_n_samples 512
    sleep 1
    nohup /root/trigger_radar_every_10_sec.sh > /dev/null 2>&1 &
}


# Finally, run the whole script...
main
