CC = gcc
CFLAGS = -Wall -pedantic-errors -std=gnu99
LIBS = -lm -lrt -pthread
DEPS = client.h server.h auxiliary.h
CLIENT_OBJ = client.o auxiliary.o
SERVER_OBJ = server.o auxiliary.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS) $(LIBS)

all: client server

client: $(CLIENT_OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

server: $(SERVER_OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

clean:
	rm -f *.o client server