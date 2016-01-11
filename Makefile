CC = gcc

CFLAGS = -o
OFLAGS = -Wall -g -c

OFILES = PQS.o
PROJECT = PQS

all: $(OFILES)
		$(CC) $(CFLAGS) $(PROJECT) $(OFILES) -pthread

PQS.o: PQS.c PQS.h
		$(CC) $(OFLAGS) PQS.c

clean:
		rm -f $(OFILES) $(PROJECT)
