# gs -dNOPAUSE -dBATCH -sDEVICE=cups -sOutputFile=- "$@"
DPI=203.2

gs -dPARANOIDSAFER -dNOPAUSE -dBATCH -sstdout=%stderr -sOutputFile=%stdout \
-sDEVICE=cups -sMediaClass=PwgRaster -sOutputType=Automatic -r${DPI}x${DPI} \
-dDEVICEWIDTH=384 -dDEVICEHEIGHT=384 -dcupsBitsPerColor=8 -dcupsColorOrder=0 \
-dcupsColorSpace=0 -dcupsBorderlessScalingFactor=0.0000 -dcupsInteger1=1 \
-dcupsInteger2=1 -scupsPageSizeName=na_letter_8.5x11in -I/usr/share/cups/fonts \
"$@"

