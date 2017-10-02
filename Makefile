all: current-set

current-set: refactor-lab2-client refactor-lab2-server

tc: /home/zookeeprr/scripts/show-client.sh
	bash /home/zookeeprr/scripts/show-client.sh

ts: /home/zookeeprr/scripts/show-server.sh
	bash /home/zookeeprr/scripts/show-server.sh





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



