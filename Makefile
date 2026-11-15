CC = gcc
CFLAGS = -Wall -Wextra -g
LIBS = -pthread -lrt

all: campus

campus: campus.c
	$(CC) $(CFLAGS) -o campus campus.c $(LIBS)

clean:
	rm -f campus campus.log

.PHONY: all clean
