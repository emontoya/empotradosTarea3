.PHONY: all clean

all: nweb client

nweb: nweb.c
	gcc -O -DLINUX nweb.c -o nweb

client: client.c
	gcc -O -DLINUX client.c -o client

clean:
	rm -f *.o nweb client
