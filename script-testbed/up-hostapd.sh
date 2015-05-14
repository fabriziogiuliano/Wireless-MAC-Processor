#!/bin/bash

if [ $# -lt 2 ]; then
	echo "usage $0 <hostapd.conf> <ip_addr>"
	exit
fi

hostapd_config=$1
ip_addr=$2
dev="wlan0"

echo rmmod b43
rmmod b43
echo sleep 1
sleep 1
echo modprobe b43 qos=0
modprobe b43 qos=0
echo sleep 1
sleep 1

echo ifconfig wlan0 $ip_addr netmask 255.255.255.0
ifconfig $dev $ip_addr netmask 255.255.255.0

echo hostapd $hostapd_config
hostapd $hostapd_config


