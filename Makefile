CC = gcc
CFLAGS = -Wall -Wextra -Iinclude
LDFLAGS = -lws2_32 -lpthread -ladvapi32 -lmswsock

# ============================================================================
# MODULES
# ============================================================================
# Safe Memory Module (leak detection, overflow protection)
SRC_SAFE_MEM = src/safe_mem.c
# Error Handling Module (structured errors, chaining)
SRC_ERROR = src/error.c
# Timeout Configuration Module (configurable timeouts)
SRC_TIMEOUT = src/timeout_config.c
# Test Framework Module
SRC_TEST_FRAMEWORK = src/test_framework.c

# ============================================================================
# CORE MODULES
# ============================================================================
# Common components (Old Engine + Config + Logger + Transaction)
SRC_COMMON = src/lexer.c src/parser.c src/storage.c src/executor.c src/row.c src/auth.c src/system.c src/config.c src/logger.c src/transaction.c
# Storage Engine (New B+ Tree + Advanced Storage Engine)
SRC_STORAGE_BTREE = src/btree.c src/buffer_pool.c src/pager.c src/storage_engine.c
# Cluster Management (Distributed System)
SRC_CLUSTER = src/cluster.c
# Security Module
SRC_SECURITY = src/security.c
# Memory Management Module
SRC_MEMORY = src/memory.c
# Network Protocol Module (Binary protocol, IOCP, connection pooling)
SRC_NETWORK = src/network.c
# Prepared Statements Module
SRC_PREPARED = src/prepared.c
# Secondary Index Module
SRC_INDEX = src/index.c
# Query Optimizer Module
SRC_OPTIMIZER = src/optimizer.c
# JOIN Operations Module
SRC_JOIN = src/join.c
# MVCC (Multi-Version Concurrency Control) Module
SRC_MVCC = src/mvcc.c
# Query Result Processing Module (ORDER BY, LIMIT, Caching)
SRC_QUERY_RESULT = src/query_result.c
# Backup/Restore Module
SRC_BACKUP = src/backup.c
# NoSQL Document Store Module
SRC_NOSQL = src/nosql.c

SRC_CLI = src/main.c
SRC_SERVER = src/server.c src/distributed.c
SRC_CLIENT = src/client.c
SRC_TEST_BTREE = src/test_btree.c

# Module objects
OBJ_SAFE_MEM = $(SRC_SAFE_MEM:.c=.o)
OBJ_ERROR = $(SRC_ERROR:.c=.o)
OBJ_TIMEOUT = $(SRC_TIMEOUT:.c=.o)
OBJ_TEST_FRAMEWORK = $(SRC_TEST_FRAMEWORK:.c=.o)

OBJ_COMMON = $(SRC_COMMON:.c=.o)
OBJ_STORAGE_BTREE = $(SRC_STORAGE_BTREE:.c=.o)
OBJ_CLUSTER = $(SRC_CLUSTER:.c=.o)
OBJ_SECURITY = $(SRC_SECURITY:.c=.o)
OBJ_MEMORY = $(SRC_MEMORY:.c=.o)
OBJ_NETWORK = $(SRC_NETWORK:.c=.o)
OBJ_PREPARED = $(SRC_PREPARED:.c=.o)
OBJ_INDEX = $(SRC_INDEX:.c=.o)
OBJ_OPTIMIZER = $(SRC_OPTIMIZER:.c=.o)
OBJ_JOIN = $(SRC_JOIN:.c=.o)
OBJ_MVCC = $(SRC_MVCC:.c=.o)
OBJ_QUERY_RESULT = $(SRC_QUERY_RESULT:.c=.o)
OBJ_BACKUP = $(SRC_BACKUP:.c=.o)
OBJ_NOSQL = $(SRC_NOSQL:.c=.o)
OBJ_CLI = $(SRC_CLI:.c=.o)
OBJ_SERVER = $(SRC_SERVER:.c=.o)
OBJ_CLIENT = $(SRC_CLIENT:.c=.o)
OBJ_TEST_BTREE = $(SRC_TEST_BTREE:.c=.o)

TARGET_CLI = inventixdb
TARGET_SERVER = inventix-server
TARGET_CLIENT = inventix-client
TARGET_TEST_BTREE = test_btree
TARGET_TEST_RUNNER = test_runner

# ============================================================================
# BUILD TARGETS
# ============================================================================

all: $(TARGET_CLI) $(TARGET_SERVER) $(TARGET_CLIENT) $(TARGET_TEST_BTREE)

$(TARGET_CLI): $(OBJ_COMMON) $(OBJ_CLI) $(OBJ_STORAGE_BTREE) $(OBJ_SECURITY) $(OBJ_PREPARED) $(OBJ_INDEX) $(OBJ_OPTIMIZER) $(OBJ_JOIN) $(OBJ_MVCC) $(OBJ_QUERY_RESULT) $(OBJ_BACKUP) $(OBJ_NOSQL) $(OBJ_SAFE_MEM) $(OBJ_ERROR) $(OBJ_TIMEOUT)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_SERVER): $(OBJ_COMMON) $(OBJ_SERVER) $(OBJ_STORAGE_BTREE) $(OBJ_CLUSTER) $(OBJ_SECURITY) $(OBJ_MEMORY) $(OBJ_NETWORK) $(OBJ_PREPARED) $(OBJ_INDEX) $(OBJ_OPTIMIZER) $(OBJ_JOIN) $(OBJ_MVCC) $(OBJ_QUERY_RESULT) $(OBJ_BACKUP) $(OBJ_NOSQL) $(OBJ_SAFE_MEM) $(OBJ_ERROR) $(OBJ_TIMEOUT)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_CLIENT): $(OBJ_CLIENT) $(OBJ_NETWORK) src/logger.o src/config.o $(OBJ_SAFE_MEM) $(OBJ_ERROR) $(OBJ_TIMEOUT)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_TEST_BTREE): $(OBJ_TEST_BTREE) $(OBJ_STORAGE_BTREE) src/logger.o src/config.o $(OBJ_SAFE_MEM) $(OBJ_ERROR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ============================================================================
# TEST RUNNER
# ============================================================================
$(TARGET_TEST_RUNNER): tests/test_runner.o $(OBJ_TEST_FRAMEWORK) $(OBJ_SAFE_MEM) $(OBJ_ERROR) $(OBJ_TIMEOUT) src/logger.o src/config.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/test_runner.o: tests/test_runner.c
	$(CC) $(CFLAGS) -DSAFE_MEM_ENABLED -c $< -o $@

# Individual test executables
test_memory: tests/test_memory.o $(OBJ_TEST_FRAMEWORK) $(OBJ_SAFE_MEM) $(OBJ_ERROR) src/logger.o src/config.o
	$(CC) $(CFLAGS) -DSAFE_MEM_ENABLED -o $@ $^ $(LDFLAGS)

test_network: tests/test_network.o $(OBJ_TEST_FRAMEWORK) $(OBJ_TIMEOUT) $(OBJ_SAFE_MEM) $(OBJ_ERROR) src/logger.o src/config.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_crash_recovery: tests/test_crash_recovery.o $(OBJ_TEST_FRAMEWORK) $(OBJ_STORAGE_BTREE) $(OBJ_SAFE_MEM) $(OBJ_ERROR) src/logger.o src/config.o src/transaction.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/test_memory.o: tests/test_memory.c
	$(CC) $(CFLAGS) -DSAFE_MEM_ENABLED -c $< -o $@

tests/test_network.o: tests/test_network.c
	$(CC) $(CFLAGS) -c $< -o $@

tests/test_crash_recovery.o: tests/test_crash_recovery.c
	$(CC) $(CFLAGS) -c $< -o $@

ifeq ($(OS),Windows_NT)
    RM = del /q /f
    RMOBJ = if exist src\*.o del /q /f src\*.o
    RMTESTOBJ = if exist tests\*.o del /q /f tests\*.o
    RMEXE = if exist *.exe del /q /f *.exe
else
    RM = rm -f
    RMOBJ = rm -f src/*.o
    RMTESTOBJ = rm -f tests/*.o
    RMEXE = rm -f *.exe
endif

clean:
	$(RMOBJ)
	$(RMTESTOBJ)
	$(RMEXE)

# ============================================================================
# TEST TARGETS
# ============================================================================
ifeq ($(OS),Windows_NT)
    EXE_EXT = .exe
    RUN_PREFIX =
else
    EXE_EXT =
    RUN_PREFIX = ./
endif

test: $(TARGET_TEST_BTREE) $(TARGET_CLI)
	@echo "--- Testing B+ Tree Storage Engine ---"
	$(RUN_PREFIX)$(TARGET_TEST_BTREE)$(EXE_EXT)
	@echo "--- Testing Parser/Executor (Hinglish Support) ---"
	$(RUN_PREFIX)$(TARGET_CLI)$(EXE_EXT) < test_hinglish_oneline.sql

# Run test suite
test-suite: $(TARGET_TEST_RUNNER)
	@echo ""
	@echo "=========================================="
	@echo "  Running Test Suite"
	@echo "=========================================="
	@echo ""
	$(RUN_PREFIX)$(TARGET_TEST_RUNNER)$(EXE_EXT) -v

# Run individual test suites
test-memory: test_memory
	$(RUN_PREFIX)test_memory$(EXE_EXT)

test-network-suite: test_network
	$(RUN_PREFIX)test_network$(EXE_EXT)

test-crash: test_crash_recovery
	$(RUN_PREFIX)test_crash_recovery$(EXE_EXT)

# Run all tests
test-all: test test-suite
	@echo ""
	@echo "=========================================="
	@echo "  All Tests Completed"
	@echo "=========================================="

# Cluster testing
test-cluster: $(TARGET_SERVER) $(TARGET_CLIENT)
	@echo "--- Starting cluster nodes ---"
	@echo "Run: $(RUN_PREFIX)$(TARGET_SERVER)$(EXE_EXT) --master --port 9876"
	@echo "Run: $(RUN_PREFIX)$(TARGET_SERVER)$(EXE_EXT) --worker --port 9877"
	@echo "Run: $(RUN_PREFIX)$(TARGET_CLIENT)$(EXE_EXT) -h 127.0.0.1 -p 9876"
