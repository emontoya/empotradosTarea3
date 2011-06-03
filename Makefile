.PHONY: all clean run pkill
PID=$(shell ps | sed -n 's/^[[:space:]]*\([0-9]*\).*nweb*/\1/p')

all: nweb client

nweb: nweb.c
	gcc -O -DLINUX nweb.c -o nweb

client: client.c
	gcc -O -DLINUX client.c -o client

clean:
	rm -f *.o nweb client

run:
	./nweb 8181 $(CURDIR)

pkill:
	kill $(PID)
