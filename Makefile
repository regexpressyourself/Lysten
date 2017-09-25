all: current-set

current-set: lab2-client lab2-server

tc: /home/zookeeprr/scripts/show-client.sh
	bash /home/zookeeprr/scripts/show-client.sh

ts: /home/zookeeprr/scripts/show-server.sh
	bash /home/zookeeprr/scripts/show-server.sh







lab2-client: ./lab2/lab2-client.c
	gcc -Wall -std=gnu99 -o "client" ./lab2/lab2-client.c

lab2-server: ./lab2/lab2-server.c
	gcc -Wall -std=gnu99 -o "server" ./lab2/lab2-server.c



