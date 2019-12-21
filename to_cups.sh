# gs -dNOPAUSE -dBATCH -sDEVICE=cups -sOutputFile=- "$@"
DPI=203.2

gs -dPARANOIDSAFER -dNOPAUSE -dBATCH -sstdout=%stderr -sOutputFile=%stdout \
-sDEVICE=cups -sOutputType=Automatic -r${DPI}x${DPI} \
-dDEVICEWIDTH=384 -dDEVICEHEIGHT=384 -dcupsBitsPerColor=8 -dcupsColorOrder=0 \
-dcupsColorSpace=0 \
	-dcupsInteger0=0 \
	-dcupsInteger1=0 \
	-dcupsInteger2=0 \
	-dcupsInteger3=1 \
	-dcupsInteger4=0 \
	-I/usr/share/cups/fonts \ "$@"

