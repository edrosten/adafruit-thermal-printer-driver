# Driver for the Adafruit thermal printers

## Installing

```
make
sudo make install
```

You need a C++17 compiler and I've only tested this on ubuntu 18.04 so far.

## Features

I implemented a number of features for dealing with the realities of roll
media, whether you want to print out a paginated document or make use of the
roll-like nature of the roll:
* Several predefined virtual page lengths (50, 100, 200mm)
* Custom page lengths
* Optional marking of page boundaries
* Optional remval of whitespace at beginning and end of page
* Option to not eject the job after it's finished

In addition:
* Plain text filter (print text using the printer's text mode)
* Detection of paper out/door open

And the flagship feature:
* Enhanced resolution mode which uses the printer's ability to vary the power
  draw in order to get a bit of greyscale output to improve the image
  quality at the expense of much slower printouts


## About the printer

I've tested with [this model](https://www.adafruit.com/product/597). It's
stringly inspired by the the [Adafruit ZJ-58
driver](https://github.com/adafruit/zj-58). It uses the [Epson
ESC/POS](https://reference.epson-biz.com/modules/ref_escpos/index.php?content_id=72)
command set with some extensions.

## Documentation

It was quite an adventure writing this code. If you want to know how it works
and why I did the things I did, then read the series of blog posts starting
[here](https://deathandthepenguinblog.wordpress.com/2019/12/08/adafruit-mini-thermal-printer-part-1-getting-better-pictures/).
