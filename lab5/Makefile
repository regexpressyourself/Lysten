all: compile-debug


compile-debug: lab5-server.c lab5-client.c tpool.so
	gcc -Wall -D DEBUG -std=gnu99 -o server lab5-server.c tpool.so -pthread
	gcc -Wall -D DEBUG -std=gnu99 -o client lab5-client.c tpool.so -pthread

package-shared: tpool.c
	gcc -Wall -std=gnu99 -fpic -c tpool.c 
	gcc -shared -otpool.so tpool.o

