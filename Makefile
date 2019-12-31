CXX=g++ -Wall -Wextra -std=c++17 -D_GLIBCXX_DEBUG -fsanitize=address -g -ggdb
LOADLIBES=-lcupsimage -lcups

all: rastertoadafruitmini ppd/mini.ppd

rastertoadafruitmini:
ppd/mini.ppd: mini.drv
	ppdc mini.drv


install: all
	./install
