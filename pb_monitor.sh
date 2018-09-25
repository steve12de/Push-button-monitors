#!/bin/bash
#######################################################################################################################
#
#   File:           pb_monitor.sh
#
#   Summary:        Push-button functionality for 'factory reset', 'reboot' and 'shutdown''
#
#   Element:        IESv06
#
#   Platform:       Linux
#
#   Description:  Interfaces to Gateworks System Controller using I2C POLL.
#                 First, disables hardware reset on pushbutton (pb).
#                 Two modes of Operation:
#                 AT START-UP
#                 - Checks file (/opt/monior/fc_set) on start-up to see if pb pressed while in use.
#                 - Yes - LED Flashes red while factory reset occurs.
#                 - No  - A 10 second period where LED is solid red, allows factory reset
#                   If pushed, factory-reset occurs. Otherwise allows period to call it.
#
#                 IN-USE Power Switch MONITOR
#                 - Press push-button for less than 5 seconds will reboot
#
#                 - Press push-button for 5 to 10 seconds, LED solid red, release button and
#                   will factory reset after next reboot (writes above File).
#
#                 - Press push-button for 10 to 15 seconds, LED flashes red, release button and
#                   board will shut-down for period in file: /opt/wakeup. If file not present
#                   (or value zero), will reboot immediatly.
#
#                 - Press push-button for 15+ seconds, LED flashes green, and cancels press.
#
#  Operation: Power On-off HW reset is disabled. GSC Interrupt status reg polled via I2C reads. 
#
#                Register Details
#                GSC_CTR_0            (R0)  PB_HARD_RESET(R0.1) bit at 1 
#                GSC_INTERRUPT_ENABLE (R11) IRQ_PB(R0.1) bit at 1 
#                            IRQ_GPIO_CHANGE(R0.4) bit at 1 
#                GSC_INTERRUPT_STATUS (R10) IRQ_GPIO_CHANGE(R0.4) Read bit - 0 is set
#                Write '1' to this register to clear interrupt
#
#######################################################################################################################

# Constants
# Note SLEEP and INTERVAL set for 200ms polling until a push,
# then poll every 50ms till release
# interval               50ms  100ms 200ms 
SLEEP_INTERVAL=0.20      # 0.05  0.10  0.20
SLEEP_PUSH_INTERVAL=0.05
INTERVAL_PERSEC=5        #  20   10    5
INTERVAL_PUSH_PERSEC=20
COUNT_5_SEC=100          # 100   50    25 
COUNT_10_SEC=200         # 200   100   50 
COUNT_15_SEC=300

# Variables
ncountDelay=0 # start-up delay count
nstate=0	  # not pressed=0; pressed= 1
ncount=0	  # count intervals (50ms) for press
ncountMax=0   # max number of count intervals for press
sleep_set=$SLEEP_INTERVAL

fcset=0       # in-use factory reset detected by presence of file on next reboot ?

#################
## Initialise  ##
#################
echo "Start-Up Power-Switch Monitor"

# GSC_CTRL_0 (R0) Disable pushbutton hard reset
# NOTE: To Re-enable PB_HARD_RESET (R0.1) bit  - R0=$((R0 | 0x01))
R0=$(i2cget -f -y 0 0x20 0) && \
  R0=$((R0 & ~0x01)) && \
  i2cset -f -y 0 0x20 0 $R0 || echo i2c error

mkdir -p /opt/monitors/

# clear Status register
i2cset -f -y 0 0x20 10 $((R10 & ~0x11))

# export gpio240 to userspace - only for debug
#echo 448 > /sys/class/gpio/export

###################################
## START-UP Power Switch MONITOR ##
###################################
# LED: Disable heartbeat and turn bi-color LED Red 
echo none > /sys/class/leds/user1/trigger
echo 255 > /sys/class/leds/user2/brightness

# Loop for Startup period of 10 seconds
while [ $ncountDelay -lt $((INTERVAL_PERSEC*10)) ]
do
    #######################################################
    ## START-UP Check Factory Reset request when running ##
    #######################################################
    if [ -f /opt/monitors/fc-set ]; then
        echo "Factory Reset Requested in run-time"
        rm /opt/monitors/fc-set
        fcset=1
        break
    fi
    # Check interrupt
    R10=$(i2cget -f -y 0 0x20 10)
    if (($R10 & 0x01)); then
        fcset=1
        # clear register
        i2cset -f -y 0 0x20 10 $((R10 & ~0x11))
        break
    fi
    sleep $SLEEP_INTERVAL
    ncountDelay=$((ncountDelay+1))
done
# clear Status register
i2cset -f -y 0 0x20 10 $((R10 & ~0x11))

# call check-factory-reset with reset parameter
# call required if set or not
/usr/local/bin/check-factory-reset.sh $fcset

echo "End Start-Up Power-Switch Monitor"

# LED Re-enable heartbeat
echo 0 > /sys/class/leds/user2/brightness	
echo heartbeat > /sys/class/leds/user1/trigger

#################################
## IN-USE Power Switch MONITOR ##
#################################

#echo "R0=$R0" # read interrupt status register

# Run time Power On-Off Monitor forever
while [ 1 ]; do
#  cat /sys/class/gpio/gpio448/value  # Debug - PB state 0 or 1
  # Check interrupt
  R10=$(i2cget -f -y 0 0x20 10)
# echo "R10=$R10"
    
  # if we got an IRQ_GPIO_CHANGE - GPIO Interrupt (0x10) process
  if (($R10 & 0x10)); then
      if ((($nstate) == "0")); then
#        echo "------set"
         nstate=1
         sleep_set=$SLEEP_PUSH_INTERVAL
      else
#        echo "------reset"
         # LED Re-enable heartbeat
         echo 0 > /sys/class/leds/user2/brightness	
         echo heartbeat > /sys/class/leds/user1/trigger
         nstate=0
         ncountMax=$ncount
         ncount=0
         sleep_set=$SLEEP_INTERVAL         
      fi      
      # clear register
      i2cset -f -y 0 0x20 10 $((R10 & ~0x11))
  fi
  # end interrupt
  
  # Process counts
  if ((($nstate) == "1")); then
       ncount=$((ncount+1))
#      echo "ncount=$ncount"
      if ((($ncount) == "$COUNT_5_SEC")); then
          # LED: Disable heartbeat and turn bi-color LED Red
          echo none > /sys/class/leds/user1/trigger
          echo 255 > /sys/class/leds/user2/brightness
      fi
      if ((($ncount) == "$COUNT_10_SEC")); then
          # LED: Re-enable heartbeat to Flash LED Red
          echo heartbeat > /sys/class/leds/user1/trigger
          echo 255 > /sys/class/leds/user2/brightness
      fi
       # Missed Interrupt Handler - could occur on frequent press so in wrong
       # state (unlikely since needs many consecutive presses).
       # restore state. Also allows cancel of any press.
      if ((($ncount) == "$COUNT_15_SEC")); then
  	      echo "Reset pb_monitor"
          # LED: Re-Enable heartbeat
          nstate=0
          ncountMax=0
          ncount=0
          sleep_set=$SLEEP_INTERVAL
          echo heartbeat > /sys/class/leds/user1/trigger
          echo 0 > /sys/class/leds/user2/brightness
      fi
  fi

  # Act on interval of press
  if [[ $ncountMax -gt "0" ]]; then
       if [[ $ncountMax -gt $((INTERVAL_PUSH_PERSEC*10)) ]]; then
           echo "Long Press (10+sec) - shutdown"
       shutdown -h now
       elif [[ $ncountMax -gt $((INTERVAL_PUSH_PERSEC*5)) ]]; then
         echo "Long Press (5+sec) - Enter factory reset on next reboot"
         touch /opt/monitors/fc-set
       else
         echo "Short Press (<5sec) - reboot"
       reboot
       fi
#       echo "ncountMax=$ncountMax"
       ncountMax=0
       nstate=0
       # clear register
  fi
  sleep $sleep_set
done
