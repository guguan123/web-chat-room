CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lsqlite3

# If cJSON.c is in the same directory
CJSON_SRC = cJSON.c

all: get_messages.cgi post_message.cgi

get_messages.cgi: get_messages.c $(CJSON_SRC)
	$(CC) $(CFLAGS) $< $(CJSON_SRC) -o $@ $(LDFLAGS)

post_message.cgi: post_message.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f *.cgi *.o
