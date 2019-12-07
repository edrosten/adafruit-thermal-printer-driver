BEGIN{
	for(i=0; i < 255; i++){

		l=16*1.0
		h=16*7
		
		j = ((i/255)^2.0) * (h-l) + l

		printf("%c7%c%c%c", 27, 7, int(j), 2)
		printf("\x1dv0\0%c\0\x01\0", 40)
		for(j=0; j < 40; j++)
			printf("\xff")
	}
	print "\n\n\n\n"
}

