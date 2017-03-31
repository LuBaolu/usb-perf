#!/bin/bash
WHERE=`pwd`

cd ffs
nice -n -20 ../../ffs-test &
sleep 2
cd /sys/kernel/config/usb_gadget/g1/
echo "dwc3.0.auto" > UDC
