#!/bin/bash
set -x

cp wmp-engine/broadcom-metaMAC/* /lib/firmware/b43
rmmod b43
sleep 1
modprobe b43 qos=0
sleep 1


pushd wmp-injection/bytecode-manager
make
make metamac
popd
