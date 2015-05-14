#!/bin/bash

if [ $# -lt 1 ]; then
	echo "usage $0 <essid> <ip-addr>"
	exit
fi


essid=$1
ip=$2
dev="wlan0"

set -x

rmmod b43
sleep 1
modprobe b43 qos=0
sleep 1
ifconfig $dev $ip netmask 255.255.255.0

iwconfig $dev essid $essid
sleep 1

iwconfig $dev rate 12M fixed
iwconfig $dev txpower 15dBm

set +x
