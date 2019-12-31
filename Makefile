CXX=g++ -Wall -Wextra -std=c++17 -g -ggdb
LOADLIBES=-lcupsimage -lcups

all: rastertoadafruitmini ppd/mini.ppd

rastertoadafruitmini:
ppd/mini.ppd: mini.drv
	ppdc mini.drv

clean:
	rm -fr ppd rastertoadafruitmini *.o

install: all
	./install.sh
