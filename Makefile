CXX=g++ -Wall -Wextra -std=c++17 -D_GLIBCXX_DEBUG -fsanitize=address -g -ggdb
LOADLIBES=-lcupsimage

rastertoadafruitmini:
ppd/mini.ppd:
	ppdc mini.drv


install: rastertoadafruitmini ppd/mini.ppd
	./install
