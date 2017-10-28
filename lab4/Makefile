all: package-static compile-static-test

compile-static-test: tpool-test.c tpool.a
	gcc -Wall -std=gnu99 -otesta tpool-test.c tpool.a -pthread

compile-shared-test: tpool-test.c tpool.so
	gcc -Wall -std=gnu99 -otestso tpool-test.c tpool.so -pthread

package-static: tpool.c 
	gcc -c -std=gnu99 tpool.c 
	ar -cr tpool.a tpool.o

package-shared: tpool.c
	gcc -Wall -std=gnu99 -fpic -c tpool.c 
	gcc -shared -otpool.so tpool.o

test: tpool-test.c
	gcc -Wall -std=gnu99 -D DEBUG -otest tpool-test.c tpool.c -pthread

