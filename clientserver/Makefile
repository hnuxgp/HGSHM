CC=/usr/bin/gcc

all: tcp
tcp: tcpserver tcpclient

tcpclient:%:%.c
	${CC} -g -Wall -o $@ $^ -lpthread
tcpserver:%:%.c
	${CC} -g -Wall -o $@ $^ -lpthread -lm

clean:
	rm -f tcpserver tcpclient *.o *.gch
