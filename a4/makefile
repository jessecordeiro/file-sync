PORT=57021
FLAGS = -DPORT=$(PORT) -g -Wall -std=gnu99

all: rcopy_client rcopy_server

rcopy_client: rcopy_client.o
	gcc ${FLAGS} -o $@ rcopy_client.o

rcopy_server: rcopy_server.o
	gcc ${FLAGS} -o $@ rcopy_server.o

clean:
	rm *.o rcopy_client rcopy_server