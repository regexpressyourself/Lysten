all: current-set

current-set: refactor-lab3-client refactor-lab3-server

sc: /home/zookeeprr/scripts/show-client.sh
	bash /home/zookeeprr/scripts/show-client.sh

ss: /home/zookeeprr/scripts/show-server.sh
	bash /home/zookeeprr/scripts/show-server.sh



mvim: ./lab3/lab3-client.c ./lab3/lab3-server.c
	nvim ./lab3/lab3-client.c ./lab3/lab3-server.c

refactor-lab3-client: ./lab3/refactor/lab3-refactor-client.c
	gcc -Wall -lrt -std=gnu99 -pthread -D DEBUG -o "client" ./lab3/refactor/lab3-refactor-client.c

refactor-lab3-server: ./lab3/refactor/lab3-refactor-server.c
	gcc -Wall -lrt -std=gnu99 -pthread -D DEBUG -o "server" ./lab3/refactor/lab3-refactor-server.c

debug-lab4-client: ./lab4/lab4-client.c
	gcc -lrt -Wall -std=gnu99 -pthread -D DEBUG -o "client" ./lab4/lab4-client.c

debug-lab4-server: ./lab4/lab4-server.c
	gcc -lrt -Wall -std=gnu99 -pthread -D DEBUG -o "server" ./lab4/lab4-server.c


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



