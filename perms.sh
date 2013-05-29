#!/bin/bash

SEARCH="Satmap"
USER="flatcap"

LINE="$(lsusb | grep "$SEARCH")"

BUS="${LINE:4:3}"
DEV="${LINE:15:3}"

B="${BUS:2:1}"

#echo LINE=$LINE
#echo BUS=$BUS
#echo B=$B
#echo DEV=$DEV

FILE="/dev/usbmon$B"
if [ -c "$FILE" ]; then
	chgrp "$USER" "$FILE"
	chmod g+rw "$FILE"
else
	echo "ERROR: $FILE"
fi

FILE="/dev/bus/usb/$BUS/$DEV"
if [ -c "$FILE" ]; then
	chgrp "$USER" "$FILE"
	chmod g+rw "$FILE"
else
	echo "ERROR: $FILE"
fi

