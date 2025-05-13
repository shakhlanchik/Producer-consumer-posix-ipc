# makefile
CC = gcc
CFLAGS = -W -Wall -Wextra -std=c11
.PHONY: clean

all:  main
main: main.c makefile
	$(CC) $(CFLAGS) main.c -o main
clean:
	rm -rf main
