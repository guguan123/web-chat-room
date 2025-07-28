CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lsqlite3

# If cJSON.c is in the same directory
CJSON_SRC = cJSON.c

all: chat_handler.cgi

chat_handler.cgi: chat_handler.c $(CJSON_SRC)
	$(CC) $(CFLAGS) $< $(CJSON_SRC) -o $@ $(LDFLAGS)

clean:
	rm *.cgi *.o
