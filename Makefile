CC = gcc
CFLAGS = -g3 #-Wall -Wextra 

all:
	$(CC) $(CFLAGS) -o server server.c
	g++ $(CFLAGS) -o client client.cpp

sv:
	#head -c 50000 </dev/urandom >myfile
	#head -c 100000000 </dev/urandom >myfile
	./server 5001

cl:
	./client localhost 5001 myfile
	chmod +r *.file
	diff 1.file myfile

loss:
	sudo tc qdisc add dev lo root netem loss 100%

lossdelay:
	sudo tc qdisc add dev lo root netem loss 100% delay 10000ms

deltc:
	sudo tc qdisc del dev lo root

dist: 
	704966744.tar.gz
	sources = Makefile server.c client.c report.pdf README
	704966744.tar.gz: $(sources)
	tar -cvzf $@ $(sources)

clean:
	rm -f 704966744.tar.gz server client *.file