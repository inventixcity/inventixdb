CC = gcc
CFLAGS = -Wall -Wextra -Iinclude
LDFLAGS = -lws2_32 -lpthread

SRC_COMMON = src/lexer.c src/parser.c src/storage.c src/executor.c
SRC_CLI = src/main.c
SRC_SERVER = src/server.c src/distributed.c

OBJ_COMMON = $(SRC_COMMON:.c=.o)
OBJ_CLI = $(SRC_CLI:.c=.o)
OBJ_SERVER = $(SRC_SERVER:.c=.o)

TARGET_CLI = inventixdb
TARGET_SERVER = inventix-server

all: $(TARGET_CLI) $(TARGET_SERVER)

$(TARGET_CLI): $(OBJ_COMMON) $(OBJ_CLI)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_SERVER): $(OBJ_COMMON) $(OBJ_SERVER)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f src/*.o $(TARGET_CLI) $(TARGET_SERVER)
