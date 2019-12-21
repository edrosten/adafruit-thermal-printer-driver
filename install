#!/bin/bash
#Tested on Ubuntu 18.04
/etc/init.d/cups stop

mkdir -p /usr/share/cups/model/adafruit
install rastertoadafruitmini  /usr/lib/cups/filter/rastertoadafruitmini
install ppd/mini.ppd /usr/share/cups/model/adafruit/mini.ppd

/etc/init.d/cups start
