GITFLAGS = -q  --no-verify --allow-empty
CC=gcc
CFLAGS= -Wall -pedantic -std=c99 -g
LIBS= -lpthread
TARGET=simpletorrent
SOURCES=src/util.c \
        src/bencode.c \
				src/sha1.c \
				src/shutdown.c \
				src/make_tracker_request.c \
				src/parse_announce_url.c \
        src/parsetorretnfile.c \
				src/process_tracker_response.c \
				src/simpletorrent.c \
				src/pwp.c

OBJS=src/util.o \
     src/bencode.o \
		 src/sha1.o\
		 src/shutdown.o \
		 src/make_tracker_request.o \
		 src/parse_announce_url.o \
     src/parsetorrentfile.o \
		 src/process_tracker_response.o \
		 src/simpletorrent.o \
		 src/pwp.o

all: ${TARGET}
	-@git add . --ignore-errors
	-@(echo "> compile" && uname -a) | git commit -F - $(GITFLAGS)
${TARGET}: ${OBJS}
	mkdir bin
	${CC} ${CFLAGS} -o bin/${TARGET} ${LIBS} ${OBJS}

%.o: $.c
	$(CC) -c $(CFLAGS) $@ $<

clean:
	rm -rf src/*.o
	rm -rf bin/${TARGET}
	rm -rf src/*.core
	rm -rf *.o
	rm -rf ${TARGET}
	rm -rf *.core

.PHONY: all clean
