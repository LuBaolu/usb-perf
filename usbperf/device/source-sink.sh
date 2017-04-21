#!/bin/bash
TRANSFER_LENGTH=1024
BULK_QLEN=32
ISOC_INTERVAL=1
ISOC_MAXBURST=0
ISOC_MULT=0
ISOC_MAXPACKET=1024
ISOC_QLEN=2
PATTERN=2

modprobe libcomposite
cd /sys/kernel/config/usb_gadget
mkdir g_zero
cd g_zero
echo 0x0525 > idVendor
echo 0xa4a0 > idProduct
mkdir strings/0x409
echo 00001 > strings/0x409/serialnumber
echo "usbperf" > strings/0x409/manufacturer
echo "g_zero" > strings/0x409/product
mkdir configs/c.1
mkdir configs/c.1/strings/0x409
echo "SourceSink" > configs/c.1/strings/0x409/configuration
mkdir functions/SourceSink.usb0

echo $TRANSFER_LENGTH > functions/SourceSink.usb0/bulk_buflen
echo $BULK_QLEN > functions/SourceSink.usb0/bulk_qlen
echo $ISOC_INTERVAL > functions/SourceSink.usb0/isoc_interval
echo $ISOC_MAXBURST > functions/SourceSink.usb0/isoc_maxburst
echo $ISOC_MAXPACKET > functions/SourceSink.usb0/isoc_maxpacket
echo $ISOC_MULT > functions/SourceSink.usb0/isoc_mult
echo $ISOC_QLEN > functions/SourceSink.usb0/iso_qlen
echo $PATTERN > functions/SourceSink.usb0/pattern

ln -s functions/SourceSink.usb0 configs/c.1/
sleep 2
echo `ls /sys/class/udc` > UDC
