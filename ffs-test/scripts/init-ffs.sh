#!/bin/bash
PWD=`pwd`

modprobe libcomposite
cd /sys/kernel/config/usb_gadget
mkdir g1
cd g1
echo 0x1a0a > idVendor
echo 0xbadd > idProduct
mkdir strings/0x409
echo 12345 > strings/0x409/serialnumber
echo "usb-perf" > strings/0x409/manufacturer
echo "ffs-test" > strings/0x409/product
mkdir configs/c.1
mkdir configs/c.1/strings/0x409
echo "config1" > configs/c.1/strings/0x409/configuration
mkdir functions/ffs.usb0
ln -s functions/ffs.usb0 configs/c.1/
cd $PWD
mkdir -p ffs
mount usb0 ffs -t functionfs
