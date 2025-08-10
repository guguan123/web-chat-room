CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lsqlite3 -lcjson

all: chat_handler.cgi

chat_handler.cgi: chat_handler.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm *.cgi *.o
