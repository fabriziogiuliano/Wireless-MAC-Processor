#!/bin/bash

cp wmp-engine/broadcom/* /lib/firmware/b43
rmmod b43
modprobe b43 qos=0


pushd wmp-injection/bytecode-manager
make
make metamac
popd
