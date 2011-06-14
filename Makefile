.PHONY: all clean run pkill

all: nweb client

nweb: nweb.c
	gcc -O -DLINUX -lpthread nweb.c -o nweb

client: client.c
	gcc -O -DLINUX client.c -o client

clean:
	rm -f *.o nweb client

run:
	./nweb -t 10 8181 $(CURDIR) 

pkill:
	kill $(shell ps | sed -n 's/^[[:space:]]*\([0-9]*\).*nweb*/\1/p')
