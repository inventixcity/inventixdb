CC = gcc
CFLAGS = -Wall -Wextra -Iinclude
LDFLAGS = -lws2_32 -lpthread

# Common components (Old Engine)
SRC_COMMON = src/lexer.c src/parser.c src/storage.c src/executor.c src/row.c src/auth.c src/system.c
# Storage Engine (New B+ Tree)
SRC_STORAGE_BTREE = src/btree.c src/buffer_pool.c src/pager.c

SRC_CLI = src/main.c
SRC_SERVER = src/server.c src/distributed.c
SRC_TEST_BTREE = src/test_btree.c

OBJ_COMMON = $(SRC_COMMON:.c=.o)
OBJ_STORAGE_BTREE = $(SRC_STORAGE_BTREE:.c=.o)
OBJ_CLI = $(SRC_CLI:.c=.o)
OBJ_SERVER = $(SRC_SERVER:.c=.o)
OBJ_TEST_BTREE = $(SRC_TEST_BTREE:.c=.o)

TARGET_CLI = inventixdb
TARGET_SERVER = inventix-server
TARGET_TEST_BTREE = test_btree

all: $(TARGET_CLI) $(TARGET_SERVER) $(TARGET_TEST_BTREE)

$(TARGET_CLI): $(OBJ_COMMON) $(OBJ_CLI) $(OBJ_STORAGE_BTREE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_SERVER): $(OBJ_COMMON) $(OBJ_SERVER) $(OBJ_STORAGE_BTREE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_TEST_BTREE): $(OBJ_TEST_BTREE) $(OBJ_STORAGE_BTREE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f src/*.o $(TARGET_CLI) $(TARGET_SERVER) $(TARGET_TEST_BTREE) *.exe

test: $(TARGET_TEST_BTREE) $(TARGET_CLI)
	@echo "--- Testing B+ Tree Storage Engine ---"
	./$(TARGET_TEST_BTREE)
	@echo "--- Testing Parser/Executor (Hinglish Support) ---"
	./$(TARGET_CLI) < test_hinglish_oneline.sql
