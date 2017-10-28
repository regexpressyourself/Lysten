all: current-set

current-set: package-static-lab4

sc: /home/zookeeprr/scripts/show-client.sh
	bash /home/zookeeprr/scripts/show-client.sh

ss: /home/zookeeprr/scripts/show-server.sh
	bash /home/zookeeprr/scripts/show-server.sh

compile-static-lab4-test: ./lab4/tpool-test.c
	gcc -Wall -std=gnu99 -otesta ./lab4/tpool-test.c libtpool.a -pthread

compile-shared-lab4-test: ./lab4/tpool-test.c
	gcc -Wall -std=gnu99 -otestso ./lab4/tpool-test.c libtpool.so -pthread


package-static-lab4: ./lab4/tpool.c
	gcc -c -std=gnu99 ./lab4/tpool.c 
	ar -cr libtpool.a tpool.o

package-shared-lab4: ./lab4/tpool.c
	gcc -Wall -std=gnu99 -fpic -c ./lab4/tpool.c 
	gcc -shared -olibtpool.so tpool.o

test-lab4: ./lab4/tpool-test.c
	gcc -Wall -std=gnu99 -D DEBUG -otest ./lab4/tpool-test.c ./lab4/tpool.c -pthread

debug-lab4: ./lab4/tpool.c
	gcc -lrt -Wall -std=gnu99 -pthread -D DEBUG -o "tpool" ./lab4/tpool.c

mvim: ./lab3/lab3-client.c ./lab3/lab3-server.c
	nvim ./lab3/lab3-client.c ./lab3/lab3-server.c

refactor-lab3-client: ./lab3/refactor/lab3-refactor-client.c
	gcc -Wall -lrt -std=gnu99 -pthread -D DEBUG -o "client" ./lab3/refactor/lab3-refactor-client.c

refactor-lab3-server: ./lab3/refactor/lab3-refactor-server.c
	gcc -Wall -lrt -std=gnu99 -pthread -D DEBUG -o "server" ./lab3/refactor/lab3-refactor-server.c



debug-lab3-client: ./lab3/lab3-client.c
	gcc -lrt -Wall -std=gnu99 -pthread -D DEBUG -o "client" ./lab3/lab3-client.c

debug-lab3-server: ./lab3/lab3-server.c
	gcc -lrt -Wall -std=gnu99 -pthread -D DEBUG -o "server" ./lab3/lab3-server.c

debug-carver-lab2-client: ./lab2/carver/lab2-client.c
	gcc -lrt -Wall -std=gnu99 -pthread -D DEBUG -o "client" ./lab2/carver/lab2-client.c

lab3-client: ./lab3/lab3-client.c
	gcc -lrt -std=gnu99 -pthread -o "client" ./lab3/lab3-client.c

lab3-server: ./lab3/lab3-server.c
	gcc -lrt -std=gnu99 -pthread -o "server" ./lab3/lab3-server.c





refactor-lab2-client: ./lab2/lab2-client.c
	gcc -Wall -std=gnu99 -D DEBUG -o "client" ./lab2/refactor/lab2-refactor-client.c

refactor-lab2-server: ./lab2/lab2-server.c
	gcc -Wall -std=gnu99 -D DEBUG -o "server" ./lab2/refactor/lab2-refactor-server.c

debug-lab2-client: ./lab2/lab2-client.c
	gcc -Wall -std=gnu99 -D DEBUG -o "client" ./lab2/lab2-client.c

debug-lab2-server: ./lab2/lab2-server.c
	gcc -wall -std=gnu99 -d debug -o "server" ./lab2/lab2-server.c

lab2-client: ./lab2/lab2-client.c
	gcc -Wall -std=gnu99 -o "client" ./lab2/lab2-client.c

lab2-server: ./lab2/lab2-server.c
	gcc -Wall -std=gnu99 -o "server" ./lab2/lab2-server.c



