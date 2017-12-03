all: compile-debug

compile-static: src/server.c src/client.c src/lib/libtpool.a
	gcc -Wall -std=gnu99 -o bin/server src/server.c src/lib/libtpool.a -pthread
	gcc -Wall -std=gnu99 -o bin/client src/client.c src/lib/libtpool.a -pthread


compile-shared: src/server.c src/client.c src/lib/libtpool.so
	gcc -Wall -std=gnu99 -o bin/server src/server.c src/lib/libtpool.so -pthread
	gcc -Wall -std=gnu99 -o bin/client src/client.c src/lib/libtpool.so -pthread

compile-debug: src/server.c src/client.c src/lib/libtpool.a
	gcc -Wall -std=gnu99 -D DEBUG -o bin/server src/server.c src/lib/libtpool.a -pthread
	gcc -Wall -std=gnu99 -D DEBUG -o bin/client src/client.c src/lib/libtpool.a -pthread


package-static-lib: src/lib/tpool.c
	gcc -c -std=gnu99 src/lib/tpool.c -osrc/lib/tpool.o
	ar -cr src/lib/libtpool.a src/lib/tpool.o


package-shared-lib: src/lib/tpool.c
	gcc -Wall -std=gnu99 -fpic -c src/lib/tpool.c -osrc/lib/tpool.o
	gcc -shared -osrc/lib/libtpool.so src/lib/tpool.o

test-server: test/testserver.sh 
	./test/testserver.sh 10 10

run-server:
	bin/server

run-client: bin/client
	bin/client
