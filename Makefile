CFLAGS = -g3 -Wall -Wextra -Wcast-qual -Wcast-align -g
CFLAGS += -Winline -Wfloat-equal -Wnested-externs
CFLAGS += -std=gnu99 -D_GNU_SOURCE -pthread

CC = gcc
EXECS = server client
.PHONY: all clean


all: $(EXECS)

server: server.c comm.c db.c
	$(CC) $(CFLAGS) $(PROMPT) server.c comm.c db.c -o $@

client: client.c 
	$(CC) $(CFLAGS) client.c -o $@

clean:
	rm -f server
	rm -f client

