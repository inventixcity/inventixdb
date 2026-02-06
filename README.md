# InventixDB

A distributed database management system implemented in C with Hinglish (Roman-Urdu) query syntax support.

---

## Codebase Statistics

| Metric | Count |
|--------|-------|
| Total Source Files | 73 (.c and .h) |
| Total Lines (with comments and blanks) | 34,981 |
| Lines of Code (without comments/blanks) | 23,878 |
| Comment Lines | 5,041 |
| Blank Lines | 6,062 |
| Language | C (C99 Standard) |
| Platforms | Windows, Linux |

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Features](#features)
4. [Installation](#installation)
5. [Usage](#usage)
6. [Hinglish Query Language](#hinglish-query-language-hql)
7. [Configuration](#configuration)
8. [API Reference](#api-reference)
9. [Module Reference](#module-reference)
10. [Development](#development)
11. [Testing](#testing)
12. [Performance](#performance)
13. [License](#license)

---

## Overview

InventixDB is a database management system built from scratch in C as a university semester project. It implements core database internals including a custom query language, B+ Tree storage, MVCC transactions, a cost-based query optimizer, and a distributed cluster layer with Raft consensus.

The system supports both relational (SQL-style) and document (NoSQL) data models, with a bilingual query interface accepting standard SQL and Hinglish syntax.

### Key Highlights

- Hybrid storage engine: B+ Tree indexing, LSM-Tree write path, columnar store for analytics
- MVCC transactions with four isolation levels and deadlock detection
- Cost-based query optimizer with plan caching and statistics
- Distributed architecture: Raft consensus, hash/range partitioning, sync/async replication
- Bilingual queries: standard SQL and Hinglish (Roman-Urdu)
- RBAC security with PBKDF2 password hashing and session management
- Full JOIN support: INNER, LEFT, RIGHT, FULL, CROSS, NATURAL with multiple algorithms
- Backup and restore in multiple formats (binary, SQL, JSON, CSV)
- NoSQL document store with MongoDB-style aggregation pipeline

---

## Architecture

```
+------------------------------------------------------------------+
|                         CLIENT LAYER                              |
|  +------------------+  +------------------+  +------------------+ |
|  |   CLI Client     |  |   TCP Client     |  |   Binary Proto   | |
|  |  (inventixdb)    |  | (inventix-client)|  |   (libinventix)  | |
|  +------------------+  +------------------+  +------------------+ |
+------------------------------------------------------------------+
                               |
                    Binary Protocol (Length-Prefixed)
                               |
+------------------------------------------------------------------+
|                         SERVER LAYER                              |
|  +------------------+  +------------------+  +------------------+ |
|  | Connection Pool  |  |   IOCP / epoll   |  |  Auth & Session  | |
|  +------------------+  +------------------+  +------------------+ |
+------------------------------------------------------------------+
                               |
+------------------------------------------------------------------+
|                      QUERY PROCESSING                             |
|  +----------+  +----------+  +------------+  +-----------------+ |
|  |  Lexer   |->|  Parser  |->|  Optimizer |->|    Executor     | |
|  | (HQL)    |  |  (AST)   |  | (Cost-Based)|  | (Plan Runner)  | |
|  +----------+  +----------+  +------------+  +-----------------+ |
+------------------------------------------------------------------+
                               |
+------------------------------------------------------------------+
|                    TRANSACTION LAYER                              |
|  +------------------+  +------------------+  +------------------+ |
|  |  MVCC Manager    |  |   Lock Manager   |  |  Deadlock Det.   | |
|  | (Snapshots)      |  | (Row/Page/Table) |  |  (Wait-For Graph)| |
|  +------------------+  +------------------+  +------------------+ |
+------------------------------------------------------------------+
                               |
+------------------------------------------------------------------+
|                      STORAGE LAYER                                |
|  +------------------+  +------------------+  +------------------+ |
|  |   Buffer Pool    |  |    B+ Tree       |  |  Secondary Idx   | |
|  |   (LRU Cache)    |  |  (Primary Store) |  |  (Multi-Column)  | |
|  +------------------+  +------------------+  +------------------+ |
|  +------------------+  +------------------+  +------------------+ |
|  |  Slotted Pages   |  |   LSM Tree       |  |  Column Store    | |
|  | (Variable Rows)  |  | (Write-Optimized)|  |   (Analytics)    | |
|  +------------------+  +------------------+  +------------------+ |
+------------------------------------------------------------------+
                               |
+------------------------------------------------------------------+
|                     CLUSTER LAYER                                 |
|  +------------------+  +------------------+  +------------------+ |
|  | Raft Consensus   |  |    Sharding      |  |   Replication    | |
|  | (Leader Election)|  | (Hash/Range)     |  |  (Sync/Async)    | |
|  +------------------+  +------------------+  +------------------+ |
+------------------------------------------------------------------+
```

### Layer Summary

| Layer | Components | Description |
|-------|------------|-------------|
| Client | CLI, TCP Client, Binary Protocol | Multiple access methods for standalone and networked use |
| Server | Connection Pool, IOCP/epoll, Auth | Async I/O networking with RBAC authentication |
| Query | Lexer, Parser, Optimizer, Executor | Full SQL + Hinglish pipeline with cost-based optimization |
| Transaction | MVCC, Locking, Deadlock Detection | ACID guarantees with four isolation levels |
| Storage | Buffer Pool, B+ Tree, Indexes, LSM, Columnar | Multi-engine persistent data management |
| Cluster | Raft, Sharding, Replication | Distributed operation with consensus |

---

## Features

### Storage Engine

| Feature | Description |
|---------|-------------|
| B+ Tree Index | Primary key indexing with internal/leaf node splitting |
| Buffer Pool | LRU-based page cache with configurable pool size |
| Slotted Pages | Variable-length row storage with slot directory and checksums |
| Page Compression | LZ4-style and RLE compression with configurable thresholds |
| Secondary Indexes | Multi-column B+ Tree indexes with statistics for the optimizer |
| LSM Tree | Write-optimized storage with MemTable, SSTables, and level compaction |
| Column Store | Columnar storage engine for analytics workloads |
| Snapshot Persistence | Binary snapshots and append-only file (AOF) logging |

### Query Processing

| Feature | Description |
|---------|-------------|
| Hinglish Lexer | Tokenizes 150+ keywords in both SQL and Hinglish |
| Recursive Descent Parser | Generates a full AST supporting DDL, DML, transactions, JOINs, subqueries |
| Cost-Based Optimizer | Selectivity estimation, access path selection, join order optimization |
| Plan Caching | LRU cache for optimized query plans |
| Prepared Statements | Parameterized queries with plan reuse and cache management |
| JOIN Operations | Nested Loop, Hash Join, and Merge Join algorithms |
| EXPLAIN / SAMJHAO | Query plan visualization in text, JSON, and XML formats |
| Aggregates | COUNT, SUM, AVG, MIN, MAX with GROUP BY and HAVING |
| ORDER BY / LIMIT | Multi-column sorting (ASC/DESC) with LIMIT and OFFSET |
| Subqueries | Subquery support in WHERE clauses |
| ALTER TABLE | ADD/DROP/RENAME/MODIFY columns, foreign key constraints |
| LIKE / IN / BETWEEN | Pattern matching, set membership, range predicates |
| CASE / WHEN / THEN | Conditional expressions |
| DISTINCT | Duplicate elimination |

### Transaction Management

| Feature | Description |
|---------|-------------|
| ACID Transactions | BEGIN, COMMIT, ROLLBACK with undo logging |
| MVCC | Multi-version concurrency control with row versioning |
| Isolation Levels | READ UNCOMMITTED, READ COMMITTED, REPEATABLE READ, SERIALIZABLE |
| Savepoints | Named savepoints with ROLLBACK TO and RELEASE |
| Deadlock Detection | Wait-for graph cycle detection with configurable timeout |
| Two-Phase Locking | Row, page, and table-level locks |

### Distributed Features

| Feature | Status |
|---------|--------|
| Raft Consensus | Leader election and log replication (Alpha) |
| Hash Partitioning | Consistent hashing for data distribution (Alpha) |
| Range Partitioning | Range-based sharding (Alpha) |
| Sync/Async Replication | Configurable replication mode (Alpha) |
| Cluster Health Monitoring | Heartbeat and failure detection (Alpha) |
| Auto-Rebalancing | Partition redistribution (Planned) |

### Security

| Feature | Description |
|---------|-------------|
| PBKDF2-SHA256 Hashing | Secure password storage with salt |
| Role-Based Access Control | Superadmin, Admin, Developer, Analyst, ReadOnly roles |
| Fine-Grained Permissions | 20 distinct permission flags (CREATE, DROP, SELECT, INSERT, etc.) |
| Session Management | Token-based sessions with configurable timeout and lockout |
| AES-256 Encryption | Data-at-rest encryption support |
| Audit Logging | Security event logging |

### Backup and Restore

| Feature | Description |
|---------|-------------|
| Full Database Backup | BACKUP / SURAKSHA command |
| Point-in-Time Restore | RESTORE / WAPAS_LAO command |
| Table Export | EXPORT to Binary, SQL, JSON, or CSV |
| Table Import | IMPORT from Binary, SQL, JSON, or CSV |
| Compression | Optional gzip compression for backups |

### NoSQL Document Store

| Feature | Description |
|---------|-------------|
| Document CRUD | RAKHO (insert), DHUNDO (find), MANGWAO (get), HATAO (delete) |
| Collections | Create and manage document collections |
| Query Operators | $eq, $gt, $lt, $in, $exists, $regex, $and, $or, $not |
| Aggregation Pipeline | $match, $project, $group, $sort, $limit, $skip, $unwind, $lookup |
| Upsert | Insert-or-update in a single operation |

### Infrastructure

| Feature | Description |
|---------|-------------|
| Safe Memory Allocator | Leak detection, overflow guard bytes, allocation tracking |
| Error Framework | Categorized error codes, error chaining, bilingual messages |
| Structured Logging | Five log levels, file rotation, category filtering, colored output |
| Timeout Configuration | Configurable timeouts for all network and query operations with retry/backoff |
| Memory Arenas | Arena allocator, memory pools, per-query quota enforcement |
| Network Protocol | Length-prefixed binary protocol with compression and keepalive |
| Test Framework | Custom test runner with assertions, timing, and colored output |
| Configuration System | INI-style config file with section-based settings |

### Data Types

| Type | Description | Example |
|------|-------------|---------|
| INT | 64-bit signed integer | `42`, `-100` |
| FLOAT | Double-precision floating point | `3.14159` |
| TEXT / STRING | Variable-length string | `"Hello World"` |
| BOOL | Boolean value | `1` (true), `0` (false) |
| AUTO | Auto-increment integer | `AUTO` |

---

## Installation

### Prerequisites

- GCC 7.0+ or MinGW-w64 (Windows)
- GNU Make
- pthread library
- Windows: Winsock2, advapi32, mswsock (included in Windows SDK)

### Build from Source

```bash
# Clone the repository
git clone https://github.com/yourusername/inventixdb.git
cd inventixdb

# Build all components
make

# Build individual components
make inventixdb        # CLI tool
make inventix-server   # Database server
make inventix-client   # Network client
make test_btree        # B+ Tree tests
```

### Build Output

| Executable | Description |
|------------|-------------|
| `inventixdb` | Standalone CLI database |
| `inventix-server` | Network database server |
| `inventix-client` | TCP client for connecting to the server |
| `test_btree` | B+ Tree unit tests |
| `test_runner` | Full test suite runner |

---

## Usage

### Standalone Mode

```bash
# Start the interactive CLI
./inventixdb

# Execute an SQL file
./inventixdb < script.sql
```

### Client-Server Mode

```bash
# Start the server (default port: 9876)
./inventix-server --port 9876

# Connect with the client
./inventix-client -h 127.0.0.1 -p 9876
```

### Distributed Mode

```bash
# Start master node
./inventix-server --master --port 9876

# Start worker nodes
./inventix-server --worker --port 9877
./inventix-server --worker --port 9878

# Connect to master
./inventix-client -h 127.0.0.1 -p 9876
```

---

## Hinglish Query Language (HQL)

InventixDB supports a bilingual query syntax allowing queries in both standard SQL and Hinglish (Roman-Urdu).

### Keyword Mapping

| SQL Keyword | Hinglish | Description |
|-------------|----------|-------------|
| CREATE TABLE | TABLE BANAO | Create a new table |
| INSERT | INSERT KARO | Insert data |
| SELECT | SELECT | Query data |
| WHERE | JAHAN | Filter condition |
| DELETE | NIKALO | Delete records |
| UPDATE | UPDATE | Update records |
| DROP | GIRAO | Drop table |
| ALTER | BADLO_TABLE | Alter table |
| BEGIN | SHURU | Start transaction |
| COMMIT | PUKKA | Commit transaction |
| ROLLBACK | WAPAS | Rollback transaction |
| SAVEPOINT | NISHAAN | Create savepoint |
| PREPARE | TAYYAR | Prepare statement |
| EXECUTE | CHALAO | Execute statement |
| JOIN | MILAO | Join tables |
| LEFT | BAAYA | Left join |
| RIGHT | DAAYA | Right join |
| FULL | POORA | Full join |
| NATURAL | KUDRATI | Natural join |
| EXPLAIN | SAMJHAO | Explain query plan |
| ORDER | KRAM | Order results |
| ASC | CHADHTE | Ascending |
| DESC | UTARTE | Descending |
| LIMIT | SEEMA | Limit results |
| AND | AUR | Logical AND |
| OR | YA | Logical OR |
| COUNT | GINO | Count aggregate |
| SUM | JODO | Sum aggregate |
| AVG | AUSAAT | Average aggregate |
| MIN | SABSE_CHOTA | Minimum value |
| MAX | SABSE_BADA | Maximum value |
| GROUP BY | SAMOOH DWARA | Group results |
| HAVING | JISME | Filter groups |
| DISTINCT | ALAG | Unique values |
| LIKE | JAISA | Pattern match |
| IN | ANDAR | Set membership |
| BETWEEN | BEECH | Range check |
| EXISTS | MAUJOOD | Existence check |
| CASE | MAAMLA | Conditional |
| WHEN | JAB | Condition branch |
| THEN | PHIR | Result branch |
| ELSE | WARNA | Default branch |
| END | KHATAM | End block |
| BACKUP | SURAKSHA | Backup database |
| RESTORE | WAPAS_LAO | Restore database |
| EXPORT | BHEJO | Export table |
| IMPORT | LAAO | Import table |
| COLLECTION | SANGRAH | NoSQL collection |
| DOCUMENT | DASTAVEZ | NoSQL document |
| FIND | KHOJO | Find documents |
| UPSERT | DAL_YA_BADLO | Insert or update |
| AGGREGATE | IKATHA | Aggregation pipeline |
| HELP | MADAD | Show help |
| QUIT | NIKLO | Exit |
| STATUS | HALAT | Show status |

### Query Examples

#### Table Operations

```sql
-- Create table (SQL)
CREATE TABLE users (
    id INT PRIMARY KEY,
    name TEXT,
    email TEXT,
    is_active BOOL
);

-- Create table (Hinglish)
TABLE BANAO users (
    id INT PRIMARY KEY,
    name TEXT,
    email TEXT,
    is_active BOOL
);

-- Create index
CREATE INDEX ON users (email);

-- Alter table
ALTER TABLE users ADD COLUMN age INT;
```

#### Data Manipulation

```sql
-- Insert data
INSERT KARO users VALUES (1, "Ali Khan", "ali@example.com", 1);
INSERT KARO users VALUES (AUTO, "Sara Ahmed", "sara@example.com", 1);

-- Multi-row insert
INSERT KARO users VALUES (2, "Ahmed", "ahmed@mail.com", 1), (3, "Zara", "zara@mail.com", 1);

-- Query with filter
SELECT name, email FROM users JAHAN is_active = 1;

-- Aggregation
SELECT COUNT(id), AVG(age) FROM users SAMOOH DWARA is_active;

-- Update
UPDATE users SET is_active = 0 JAHAN id = 1;

-- Delete
NIKALO FROM users JAHAN id = 1;
```

#### Transactions

```sql
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;

SAVEPOINT before_update;

INSERT KARO orders VALUES (1, 100, "2024-01-01");

-- Rollback to savepoint if needed
ROLLBACK TO before_update;

COMMIT;
```

#### Joins

```sql
-- Inner join
SELECT users.name, orders.total
FROM users
JOIN orders ON users.id = orders.user_id;

-- Left join (Hinglish)
SELECT users.name, orders.total
FROM users
BAAYA MILAO orders ON users.id = orders.user_id;

-- Explain query plan
SAMJHAO SELECT * FROM users JAHAN id > 100;
```

#### Prepared Statements

```sql
PREPARE get_user AS SELECT * FROM users JAHAN id = ?;
EXECUTE get_user USING (1);
DEALLOCATE get_user;
```

#### Document Storage (NoSQL)

```sql
-- Store JSON document
RAKHO logs "{\"event\": \"login\", \"user\": \"ali\", \"time\": 1234567890}";

-- Retrieve documents
DHUNDO logs;

-- Delete document
HATAO logs;
```

#### Backup and Restore

```sql
BACKUP DATABASE "./backup/inventix_full.bak";
RESTORE DATABASE "./backup/inventix_full.bak";
EXPORT TABLE users "./exports/users.csv" FORMAT CSV;
IMPORT TABLE users "./exports/users.csv" FORMAT CSV;
```

---

## Configuration

InventixDB uses an INI-style configuration file (`inventix.conf`).

### Sections

| Section | Description |
|---------|-------------|
| `[storage]` | Data directory, AOF, snapshots, sync settings |
| `[storage_engine]` | Engine type (row/column/LSM), compression, bloom filters |
| `[buffer_pool]` | Pool size, page size, eviction policy |
| `[network]` | Server port, max connections, buffer sizes, TCP options |
| `[distributed]` | Master/worker config, replication factor, partition strategy |
| `[cluster]` | Raft consensus, node identity, seed nodes, replication mode |
| `[security]` | Authentication, password policy, session timeout, lockout |
| `[logging]` | Log level, file rotation, query logging, slow query threshold |
| `[query]` | Max result rows, query timeout, query cache settings |

### Example Configuration

```ini
[storage]
data_dir = "./data"
enable_aof = true
enable_snapshots = true
sync_on_write = false

[storage_engine]
engine_type = 0          # 0=ROW_STORE, 1=COLUMN_STORE, 2=LSM_TREE
compression_type = 0     # 0=NONE, 1=LZ4, 2=RLE
enable_bloom_filter = 1

[buffer_pool]
pool_size = 1024
page_size = 4096

[network]
server_port = 9876
max_connections = 100

[security]
enable_auth = true
password_min_length = 6
session_timeout_sec = 3600

[logging]
log_level = 1            # 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR
enable_query_logging = 1
slow_query_threshold_ms = 1000
```

---

## API Reference

### Storage Engine API

```c
int storage_engine_init(StorageEngineType type, const char *path);

// Slotted page operations
SlottedPage *slotted_page_create(uint32_t page_id);
int slotted_page_insert(SlottedPage *page, void *record, uint16_t len, uint16_t *slot_id);
void *slotted_page_get(SlottedPage *page, uint16_t slot_id, uint16_t *len);
int slotted_page_delete(SlottedPage *page, uint16_t slot_id);

// Column store
ColumnStore *column_store_create(const char *name, ColumnDef *columns, int count);
int column_store_insert(ColumnStore *store, void **values);
ColumnIterator *column_store_scan(ColumnStore *store, int *columns, int count);

// LSM Tree
LsmTree *lsm_tree_create(const char *name, const char *data_dir);
int lsm_tree_put(LsmTree *tree, const char *key, void *value, size_t size);
void *lsm_tree_get(LsmTree *tree, const char *key, size_t *size);
```

### Transaction API

```c
int mvcc_init(void);
TxnId mvcc_begin_transaction(IsolationLevel isolation, const char *user);
int mvcc_commit_transaction(TxnId txn_id);
int mvcc_abort_transaction(TxnId txn_id);

int mvcc_create_savepoint(TxnId txn_id, const char *name);
int mvcc_rollback_to_savepoint(TxnId txn_id, const char *name);

int mvcc_acquire_lock(TxnId txn_id, const char *resource, LockMode mode, LockGranularity gran);
int mvcc_release_lock(TxnId txn_id, const char *resource);
```

### Query Optimizer API

```c
int optimizer_init(void);
void optimizer_shutdown(void);

OptQueryPlan *optimizer_optimize(ASTNode *ast, const char *query_text);
void optimizer_free_plan(OptQueryPlan *plan);
char *optimizer_plan_explain(OptQueryPlan *plan, bool verbose);

int optimizer_update_stats(const char *table_name);
TableStats *optimizer_get_stats(const char *table_name);
```

### Prepared Statement API

```c
int prepared_init(void);
PreparedStmt *prepared_create(const char *name, const char *query, ASTNode *ast);
int prepared_execute(PreparedStmt *stmt, ParamValue *params, int param_count);
void prepared_free(PreparedStmt *stmt);
```

### Memory Management API

```c
// Safe memory (leak detection, overflow guards)
void *safe_malloc(size_t size, const char *file, int line, const char *func);
void *safe_calloc(size_t n, size_t size, const char *file, int line, const char *func);
void safe_free(void *ptr, const char *file, int line, const char *func);

// Arena allocator
Arena *arena_create(size_t initial_size);
void *arena_alloc(Arena *arena, size_t size);
void arena_destroy(Arena *arena);

// Memory pool
MemPool *mempool_create(size_t object_size, size_t count);
void *mempool_alloc(MemPool *pool);
void mempool_free(MemPool *pool, void *ptr);
```

---

## Module Reference

### Source Files (src/)

| File | Lines | Description |
|------|-------|-------------|
| executor.c | 1,848 | Query execution engine (DDL, DML, transactions, JOINs) |
| network.c | 1,606 | Binary protocol, IOCP, connection pooling |
| cluster.c | 1,594 | Raft consensus, cluster membership, replication |
| parser.c | 1,594 | Recursive descent parser for SQL and Hinglish |
| optimizer.c | 1,310 | Cost-based query optimizer, plan caching |
| storage_engine.c | 1,294 | Slotted pages, columnar store, LSM tree |
| security.c | 1,143 | RBAC, password hashing, session management |
| join.c | 1,128 | Nested loop, hash, and merge join algorithms |
| mvcc.c | 1,080 | Multi-version concurrency control |
| server.c | 1,013 | Server entry point, connection handling |
| index.c | 938 | Secondary B+ Tree indexes with statistics |
| prepared.c | 927 | Prepared statement management and caching |
| memory.c | 871 | Arena allocator, memory pools, quota enforcement |
| query_result.c | 809 | ORDER BY sorting, LIMIT/OFFSET, result caching |
| client.c | 780 | TCP client with binary protocol |
| nosql.c | 730 | NoSQL document store, aggregation pipeline |
| safe_mem.c | 719 | Leak detection, guard bytes, allocation tracking |
| config.c | 710 | Configuration file parsing and management |
| transaction.c | 674 | Transaction undo log, savepoints |
| backup.c | 634 | Backup/restore in binary, SQL, JSON, CSV |
| logger.c | 612 | Structured logging, file rotation |
| storage.c | 579 | Key-value store, hash table, skip list indexes |
| lexer.c | 475 | Tokenizer for SQL and Hinglish keywords |
| test_framework.c | 381 | Custom test framework with assertions |
| timeout_config.c | 362 | Network timeout management with retry/backoff |
| error.c | 358 | Error handling framework with chaining |
| distributed.c | 312 | Master-worker query distribution |
| main.c | 137 | CLI entry point |

### Header Files (include/)

| File | Lines | Description |
|------|-------|-------------|
| cluster.h | 780 | Cluster data structures and Raft protocol |
| network.h | 533 | Network protocol definitions |
| security.h | 514 | Security types, permissions, roles |
| memory.h | 455 | Arena, pool, and quota allocator interfaces |
| storage_engine.h | 413 | Multi-engine storage abstractions |
| index.h | 403 | Secondary index types and statistics |
| mvcc.h | 398 | MVCC row versioning and isolation |
| optimizer.h | 364 | Query plan structures and cost model |
| test_framework.h | 346 | Test macros and runner interface |
| error.h | 336 | Error codes by category |
| transaction.h | 300 | Transaction states and undo log |
| nosql.h | 300 | Document store and aggregation types |
| join.h | 297 | JOIN algorithms and execution state |
| config.h | 291 | Full configuration structure |
| logger.h | 291 | Log levels, categories, file rotation |
| parser.h | 290 | AST node types and structures |
| prepared.h | 280 | Prepared statement cache |
| safe_mem.h | 241 | Safe allocation with tracking |
| timeout_config.h | 228 | Timeout settings per operation type |
| lexer.h | 202 | Token types (150+ keywords) |
| query_result.h | 185 | Result sets, sorting, caching |
| backup.h | 166 | Backup formats and options |

---

## Development

### Project Structure

```
inventixdb/
|-- include/             # Header files (all module interfaces)
|-- src/                 # Implementation files
|-- tests/               # Test suite (test_runner, test_memory, test_network, test_crash_recovery)
|-- docs/                # Documentation
|-- inventix.conf        # Default configuration file
|-- Makefile             # Build configuration
|-- README.md
```

### Coding Standards

- C99 standard compliance
- 4-space indentation
- snake_case for functions and variables
- UPPER_CASE for constants and macros
- Return codes for error handling
- Explicit memory allocation and deallocation

### Build Targets

```bash
make                    # Build all (CLI, server, client, test_btree)
make inventixdb         # CLI tool only
make inventix-server    # Server only
make inventix-client    # Client only
make test_runner        # Test suite runner
make clean              # Remove all build artifacts
```

---

## Testing

```bash
# B+ Tree unit tests
./test_btree

# Full test suite
make test-suite

# Memory management tests
make test-memory

# Network tests
make test-network-suite

# Crash recovery tests
make test-crash

# Run all tests
make test-all

# Cluster testing (manual)
make test-cluster
```

### Test Modules

| Test | Description |
|------|-------------|
| test_btree | B+ Tree insert, search, split, delete |
| test_runner | Comprehensive test suite across modules |
| test_memory | Safe memory allocator, leak detection, overflow guards |
| test_network | Network protocol, timeout handling |
| test_crash_recovery | Crash recovery, WAL, snapshot restore |

---

## Performance

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Primary Key Lookup | O(log n) | B+ Tree traversal |
| Index Scan | O(log n + k) | k = matching rows |
| Sequential Scan | O(n) | Full table scan |
| Insert | O(log n) | Plus index maintenance |
| Delete | O(log n) | Plus index maintenance |
| Nested Loop Join | O(n * m) | n, m = table sizes |
| Hash Join | O(n + m) | With hash table build |
| Merge Join | O(n log n + m log m) | With sorting phase |

---

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/new-feature`)
3. Commit changes with descriptive messages
4. Push to the branch (`git push origin feature/new-feature`)
5. Open a Pull Request

---

## License

This project is developed as part of a university course (Semester 5).

---

## References

- Database Systems: The Complete Book (Garcia-Molina, Ullman, Widom)
- Architecture of a Database System (Hellerstein, Stonebraker, Hamilton)
- The Design and Implementation of Modern Column-Oriented Database Systems

---

## Contact

For questions or feedback, please open an issue on the repository.
