# InventixDB System Architecture Diagrams

> PlantUML source code for generating all architecture and flow diagrams.
> Render these using [plantuml.com](https://www.plantuml.com/plantuml/uml/) or any PlantUML-compatible tool.

---

## Table of Contents

1. [High-Level System Architecture](#1-high-level-system-architecture)
2. [Query Processing Pipeline](#2-query-processing-pipeline)
3. [Storage Engine Architecture](#3-storage-engine-architecture)
4. [MVCC Transaction Flow](#4-mvcc-transaction-flow)
5. [Distributed Cluster Architecture](#5-distributed-cluster-architecture)
6. [Network Protocol Flow](#6-network-protocol-flow)
7. [Security Module Architecture](#7-security-module-architecture)
8. [Backup and Restore Flow](#8-backup-and-restore-flow)
9. [Memory Management Architecture](#9-memory-management-architecture)
10. [Component Dependency Map](#10-component-dependency-map)

---

## 1. High-Level System Architecture

Complete layered view of InventixDB showing all six architectural layers and their internal components.

```plantuml
@startuml InventixDB_System_Architecture
!theme plain
skinparam linetype ortho
skinparam ranksep 40
skinparam nodesep 30
skinparam padding 6
skinparam defaultFontName "Segoe UI"
skinparam defaultFontSize 11
skinparam roundCorner 8
skinparam shadowing false

skinparam rectangle {
    BorderColor #37474F
    FontColor #212121
}

' ============================================================
' COLOR PALETTE
' ============================================================
!$CLIENT_BG    = "#E3F2FD"
!$CLIENT_BD    = "#1565C0"
!$SERVER_BG    = "#E8F5E9"
!$SERVER_BD    = "#2E7D32"
!$QUERY_BG     = "#FFF3E0"
!$QUERY_BD     = "#E65100"
!$TXN_BG       = "#F3E5F5"
!$TXN_BD       = "#6A1B9A"
!$STORAGE_BG   = "#ECEFF1"
!$STORAGE_BD   = "#37474F"
!$CLUSTER_BG   = "#FCE4EC"
!$CLUSTER_BD   = "#B71C1C"
!$INFRA_BG     = "#E0F7FA"
!$INFRA_BD     = "#00695C"

title **InventixDB -- System Architecture Overview**\n73 source files | 34,981 total lines | 23,878 lines of code\n

' ============================================================
' LAYER 1 -- CLIENT LAYER
' ============================================================
rectangle "CLIENT LAYER" as CLIENT_LAYER $CLIENT_BD {
    skinparam rectangle {
        BackgroundColor $CLIENT_BG
        BorderColor $CLIENT_BD
    }
    rectangle "**CLI Client**\n(inventixdb)\n---\nInteractive REPL\nSQL file execution\nHinglish support" as CLI
    rectangle "**TCP Client**\n(inventix-client)\n---\nBinary protocol\nAuth handshake\nResult streaming" as TCP_CLIENT
    rectangle "**Binary Protocol Lib**\n(libinventix)\n---\nLength-prefixed frames\nCompression support\nKeepalive probes" as PROTO_LIB
}

' ============================================================
' LAYER 2 -- SERVER LAYER
' ============================================================
rectangle "SERVER LAYER" as SERVER_LAYER $SERVER_BD {
    skinparam rectangle {
        BackgroundColor $SERVER_BG
        BorderColor $SERVER_BD
    }
    rectangle "**Connection Manager**\n---\nAccept/close sockets\nConnection pooling\nMax 100 concurrent" as CONN_MGR
    rectangle "**Async I/O Engine**\n---\nIOCP (Windows)\nepoll (Linux)\nNon-blocking recv/send" as ASYNC_IO
    rectangle "**Auth & Session**\n---\nPBKDF2-SHA256 login\nToken-based sessions\nRole verification" as AUTH_SESSION
}

' ============================================================
' LAYER 3 -- QUERY PROCESSING LAYER
' ============================================================
rectangle "QUERY PROCESSING LAYER" as QUERY_LAYER $QUERY_BD {
    skinparam rectangle {
        BackgroundColor $QUERY_BG
        BorderColor $QUERY_BD
    }
    rectangle "**Lexer**\n(lexer.c -- 475 lines)\n---\nTokenizes input\n150+ SQL & Hinglish\nkeywords" as LEXER
    rectangle "**Parser**\n(parser.c -- 1,594 lines)\n---\nRecursive descent\nAST generation\nDDL/DML/Txn nodes" as PARSER
    rectangle "**Optimizer**\n(optimizer.c -- 1,310 lines)\n---\nCost-based planning\nSelectivity estimation\nPlan cache (LRU)" as OPTIMIZER
    rectangle "**Executor**\n(executor.c -- 1,848 lines)\n---\nPlan runner\nJOIN execution\nAggregate processing" as EXECUTOR
}

' ============================================================
' LAYER 4 -- TRANSACTION LAYER
' ============================================================
rectangle "TRANSACTION LAYER" as TXN_LAYER $TXN_BD {
    skinparam rectangle {
        BackgroundColor $TXN_BG
        BorderColor $TXN_BD
    }
    rectangle "**MVCC Manager**\n(mvcc.c -- 1,080 lines)\n---\nRow versioning\nSnapshot isolation\n4 isolation levels" as MVCC
    rectangle "**Lock Manager**\n---\nRow/Page/Table locks\nShared & Exclusive\nLock escalation" as LOCK_MGR
    rectangle "**Deadlock Detector**\n---\nWait-for graph\nCycle detection\nVictim selection" as DEADLOCK
    rectangle "**Undo Log**\n(transaction.c -- 674 lines)\n---\nOperation logging\nSavepoints\nRollback support" as UNDO_LOG
}

' ============================================================
' LAYER 5 -- STORAGE LAYER
' ============================================================
rectangle "STORAGE LAYER" as STORAGE_LAYER $STORAGE_BD {
    skinparam rectangle {
        BackgroundColor $STORAGE_BG
        BorderColor $STORAGE_BD
    }
    rectangle "**Buffer Pool**\n(buffer_pool.c)\n---\nLRU page cache\nDirty page tracking\nConfigurable size" as BUFFER_POOL
    rectangle "**B+ Tree**\n(btree.c)\n---\nPrimary key index\nLeaf/Internal nodes\nNode splitting" as BTREE
    rectangle "**Secondary Indexes**\n(index.c -- 938 lines)\n---\nMulti-column B+ Tree\nIndex statistics\nASC/DESC ordering" as SEC_INDEX
    rectangle "**Slotted Pages**\n(storage_engine.c)\n---\nVariable-length rows\nSlot directory\nPage checksums" as SLOTTED
    rectangle "**LSM Tree**\n(storage_engine.c)\n---\nMemTable (sorted)\nSSTables (levels)\nCompaction" as LSM
    rectangle "**Column Store**\n(storage_engine.c)\n---\nColumnar chunks\nRLE compression\nAnalytics scans" as COL_STORE
}

' ============================================================
' LAYER 6 -- CLUSTER LAYER
' ============================================================
rectangle "CLUSTER LAYER" as CLUSTER_LAYER $CLUSTER_BD {
    skinparam rectangle {
        BackgroundColor $CLUSTER_BG
        BorderColor $CLUSTER_BD
    }
    rectangle "**Raft Consensus**\n(cluster.c -- 1,594 lines)\n---\nLeader election\nLog replication\nTerm management" as RAFT
    rectangle "**Partitioning**\n---\nHash partitioning\nRange partitioning\nPartition map" as PARTITIONS
    rectangle "**Replication**\n---\nSync / Async / SyncAll\nReplica health\nData forwarding" as REPLICATION
}

' ============================================================
' INFRASTRUCTURE (cross-cutting)
' ============================================================
rectangle "INFRASTRUCTURE (cross-cutting)" as INFRA $INFRA_BD {
    skinparam rectangle {
        BackgroundColor $INFRA_BG
        BorderColor $INFRA_BD
    }
    rectangle "**Logger**\n5 levels\nFile rotation\nColored output" as LOGGER
    rectangle "**Safe Memory**\nLeak detection\nGuard bytes\nTracking" as SAFE_MEM
    rectangle "**Error Framework**\n12 categories\nChaining\nBilingual msgs" as ERR_FW
    rectangle "**Config System**\nINI parser\n9 sections\nHot reload" as CONFIG
    rectangle "**Timeout Mgr**\nPer-op timeouts\nRetry/backoff\nStatistics" as TIMEOUT
}

' ============================================================
' VERTICAL FLOW (layer connections)
' ============================================================
CLI        -[#1565C0,bold]down-> CONN_MGR
TCP_CLIENT -[#1565C0,bold]down-> CONN_MGR
PROTO_LIB  -[#1565C0,bold]down-> ASYNC_IO

CONN_MGR    -[#2E7D32,bold]down-> LEXER
AUTH_SESSION -[#2E7D32,bold]down-> LEXER
ASYNC_IO    -[#2E7D32,bold]down-> LEXER

LEXER     -[#E65100,bold]right-> PARSER
PARSER    -[#E65100,bold]right-> OPTIMIZER
OPTIMIZER -[#E65100,bold]right-> EXECUTOR

EXECUTOR -[#6A1B9A,bold]down-> MVCC
EXECUTOR -[#6A1B9A,bold]down-> LOCK_MGR

MVCC      -[#6A1B9A,bold]right-> DEADLOCK
LOCK_MGR  -[#6A1B9A,bold]right-> DEADLOCK
MVCC      -[#6A1B9A,bold]down-> UNDO_LOG

MVCC       -[#37474F,bold]down-> BUFFER_POOL
UNDO_LOG   -[#37474F,bold]down-> BUFFER_POOL
BUFFER_POOL -[#37474F,bold]right-> BTREE
BTREE       -[#37474F,bold]right-> SEC_INDEX
BUFFER_POOL -[#37474F,bold]down-> SLOTTED
SLOTTED     -[#37474F,bold]right-> LSM
LSM         -[#37474F,bold]right-> COL_STORE

BTREE       -[#B71C1C,bold]down-> RAFT
SEC_INDEX   -[#B71C1C,bold]down-> PARTITIONS
SLOTTED     -[#B71C1C,bold]down-> REPLICATION

@enduml
```

---

## 2. Query Processing Pipeline

Detailed flow from raw SQL/Hinglish text through tokenization, parsing, optimization, and execution.

```plantuml
@startuml InventixDB_Query_Pipeline
!theme plain
skinparam linetype ortho
skinparam ranksep 30
skinparam nodesep 25
skinparam defaultFontName "Segoe UI"
skinparam defaultFontSize 11
skinparam shadowing false
skinparam ActivityBackgroundColor #FFFFFF
skinparam ActivityBorderColor #37474F

title **InventixDB -- Query Processing Pipeline**\nFrom raw text to result set\n

|#E3F2FD| **Input** |
start
:User submits query string;
note right
  Supports both syntaxes:
  **SQL**: SELECT * FROM users WHERE id = 1;
  **HQL**: SELECT * FROM users JAHAN id = 1;
end note

|#FFF8E1| **Lexer** (lexer.c -- 475 lines) |
:Tokenize input string;
note right
  **150+ token types** recognized:
  -- SQL keywords (SELECT, INSERT, JOIN...)
  -- Hinglish keywords (BANAO, JAHAN, KARO...)
  -- Literals (INT, FLOAT, STRING)
  -- Operators (=, >, <, AND, OR)
  -- Symbols ( (, ), ;, *, {, } )
  Each token stores: type, value, line number
end note
:Produce TokenList;

|#FFF3E0| **Parser** (parser.c -- 1,594 lines) |
:Parse token stream (recursive descent);
note right
  **AST Node Types Generated:**
  -- DDL: CREATE TABLE, DROP, ALTER
  -- DML: SELECT, INSERT, UPDATE, DELETE
  -- Transaction: BEGIN, COMMIT, ROLLBACK
  -- JOIN: INNER, LEFT, RIGHT, FULL, CROSS
  -- Prepared: PREPARE, EXECUTE, DEALLOCATE
  -- NoSQL: RAKHO, DHUNDO, HATAO
  -- Backup: BACKUP, RESTORE, EXPORT, IMPORT
  -- Expressions: Binary, Logical, Subquery
end note
:Generate Abstract Syntax Tree (AST);

|#F3E5F5| **Optimizer** (optimizer.c -- 1,310 lines) |
if (Plan in cache?) then (yes -- cache hit)
    :Return cached OptQueryPlan;
else (no -- cache miss)
    :Collect table statistics;
    note right
      **Statistics used:**
      -- Row count per table
      -- Distinct values per column
      -- Min/Max values
      -- Null fraction
      -- Average row width
    end note
    :Estimate selectivity of predicates;
    :Evaluate access paths;
    note right
      **Access Path Selection:**
      -- Sequential Scan (cost = 4.0x)
      -- Index Scan (cost = 1.0x)
      -- Index-Only Scan
      -- Bitmap Scan
    end note
    :Optimize join order (for multi-table);
    note right
      **Join Algorithms Considered:**
      -- Nested Loop Join: O(n*m)
      -- Hash Join: O(n+m)
      -- Merge Join: O(n log n + m log m)
      Best algorithm chosen by cost estimate
    end note
    :Build OptQueryPlan with cost;
    :Store plan in LRU cache (256 slots);
endif

|#E8F5E9| **Executor** (executor.c -- 1,848 lines) |
:Execute OptQueryPlan;
note right
  **Execution steps:**
  1. Acquire locks via MVCC Lock Manager
  2. Open table/index scans
  3. Apply WHERE predicates row-by-row
  4. Execute JOIN using selected algorithm
  5. Process GROUP BY aggregates
  6. Apply HAVING filter
  7. Sort results (ORDER BY)
  8. Apply LIMIT / OFFSET
  9. Format output rows
end note

|#ECEFF1| **Result** |
:Build ResultSet;
note right
  **ResultSet contains:**
  -- Column names and types
  -- Row data (linked list)
  -- Execution time (ms)
  -- Rows scanned vs returned
  -- Memory used
end note
:Return to client;
stop

@enduml
```

---

## 3. Storage Engine Architecture

Three storage engines (Row Store, Column Store, LSM Tree) with buffer pool and page management.

```plantuml
@startuml InventixDB_Storage_Engine
!theme plain
skinparam linetype ortho
skinparam ranksep 30
skinparam nodesep 20
skinparam defaultFontName "Segoe UI"
skinparam defaultFontSize 11
skinparam shadowing false
skinparam roundCorner 8

skinparam package {
    BackgroundColor #FFFFFF
    BorderColor #37474F
    FontColor #212121
}

title **InventixDB -- Storage Engine Architecture**\nThree storage engines with unified buffer pool\n

package "Buffer Pool (LRU Cache)" as BP #E3F2FD {
    rectangle "**Page Cache**\n---\nConfigurable pool_size\nLRU eviction policy\nDirty page tracking\nPin count per page" as PAGE_CACHE #BBDEFB
    rectangle "**Page Descriptor**\n---\npage_id: uint32\nis_dirty: bool\npin_count: int\nlast_access: timestamp" as PAGE_DESC #BBDEFB
    rectangle "**Pager**\n(pager.c)\n---\nDisk I/O layer\nPage read/write\nFile management" as PAGER #BBDEFB
}

package "ENGINE 1: Row Store (B+ Tree)" as E1 #E8F5E9 {
    rectangle "**B+ Tree Index**\n(btree.c -- 143 lines)\n---\nLeaf nodes: key + row data\nInternal nodes: key + child ptr\nMax cells per leaf: ~7\nNode splitting on overflow" as BTREE_IDX #C8E6C9
    rectangle "**Slotted Pages**\n---\nVariable-length records\nSlot directory (offset+length)\nFree space management\nCRC32 checksums" as SLOT_PAGE #C8E6C9
    rectangle "**Secondary Indexes**\n(index.c -- 938 lines)\n---\nMulti-column composite keys\nASC/DESC per column\nIndex statistics for optimizer\nMax 16 indexes per table" as SEC_IDX #C8E6C9
}

package "ENGINE 2: Column Store" as E2 #FFF3E0 {
    rectangle "**Column Chunks**\n---\nOne file per column\nHeader: name, type, row_count\nMin/Max statistics\nNull bitmap" as COL_CHUNK #FFE0B2
    rectangle "**RLE Compression**\n---\nRun-length encoding\nfor repeated values\n(analytics workloads)" as COL_RLE #FFE0B2
    rectangle "**Column Iterator**\n---\nSelective column scan\nProjection pushdown\nBatch processing" as COL_ITER #FFE0B2
}

package "ENGINE 3: LSM Tree" as E3 #F3E5F5 {
    rectangle "**MemTable**\n(in-memory, sorted)\n---\nBinary search tree\n1 MB capacity\nWrites go here first" as MEMTABLE #E1BEE7
    rectangle "**Write-Ahead Log**\n---\nAppend-only durability\nReplay on crash recovery\nFsync per write (optional)" as WAL #E1BEE7
    rectangle "**SSTables**\n(on-disk, immutable)\n---\nLevel 0: unsorted, overlapping\nLevel 1-3: sorted, non-overlapping\n4 MB max per SSTable" as SSTABLE #E1BEE7
    rectangle "**Compaction**\n---\nMerge overlapping SSTables\nRemove tombstones\nLevel-by-level promotion" as COMPACT #E1BEE7
}

package "Compression Module" as COMP #ECEFF1 {
    rectangle "**LZ4 Compression**\n---\nFast compression\nfor row-store pages\nThreshold: 512 bytes" as LZ4 #CFD8DC
    rectangle "**RLE Compression**\n---\nRun-length encoding\nfor columnar data\nHigh compression ratio" as RLE #CFD8DC
}

package "Persistence Layer" as PERSIST #FCE4EC {
    rectangle "**Snapshot**\n(Binary dump)\n---\nFull KV store dump\nAtomic via tmp+rename\nRecovery on startup" as SNAPSHOT #FFCDD2
    rectangle "**AOF Log**\n(Append-Only File)\n---\nOperation replay log\nConfigurable fsync\nLog truncation" as AOF #FFCDD2
}

' ============================================================
' CONNECTIONS
' ============================================================
PAGE_CACHE -[#1565C0,bold]down-> PAGE_DESC
PAGE_DESC  -[#1565C0,bold]down-> PAGER

PAGER -[#2E7D32,bold]down-> BTREE_IDX
PAGER -[#E65100,bold]down-> COL_CHUNK
PAGER -[#6A1B9A,bold]down-> MEMTABLE

BTREE_IDX -[#2E7D32]right-> SLOT_PAGE
SLOT_PAGE -[#2E7D32]right-> SEC_IDX

COL_CHUNK -[#E65100]right-> COL_RLE
COL_RLE   -[#E65100]right-> COL_ITER

MEMTABLE -[#6A1B9A]right-> WAL
MEMTABLE -[#6A1B9A]down-> SSTABLE
SSTABLE  -[#6A1B9A]right-> COMPACT

SLOT_PAGE -[#546E7A]down-> LZ4
COL_RLE   -[#546E7A]down-> RLE

PAGER    -[#B71C1C]down-> SNAPSHOT
WAL      -[#B71C1C]down-> AOF

@enduml
```

---

## 4. MVCC Transaction Flow

Detailed transaction lifecycle showing isolation levels, locking, versioning, and deadlock detection.

```plantuml
@startuml InventixDB_MVCC_Flow
!theme plain
skinparam linetype ortho
skinparam ranksep 25
skinparam nodesep 20
skinparam defaultFontName "Segoe UI"
skinparam defaultFontSize 11
skinparam shadowing false

title **InventixDB -- MVCC Transaction Lifecycle**\nMulti-Version Concurrency Control with Deadlock Detection\n

|#E3F2FD| **Client** |
start
:BEGIN TRANSACTION\nISOLATION LEVEL = ?;
note right
  **4 Isolation Levels:**
  READ_UNCOMMITTED -- see uncommitted
  READ_COMMITTED -- only committed
  REPEATABLE_READ -- snapshot at first read
  SERIALIZABLE -- full serializability
end note

|#E8F5E9| **MVCC Manager** (mvcc.c) |
:Assign TxnId (monotonic uint64);
:Record start timestamp;
:Set state = TXN_STATE_ACTIVE;

if (Isolation >= REPEATABLE_READ?) then (yes)
    :Take TransactionSnapshot;
    note right
      **Snapshot contains:**
      xmin: oldest active txn
      xmax: next txn id
      active_txns[]: all running txn ids
      snapshot_ts: current timestamp
    end note
else (no)
    :No snapshot needed\n(visibility checked per-statement);
endif

|#FFF3E0| **Read Path** |
:SELECT query arrives;
:Scan row version chain;
note right
  **Row Version Chain:**
  Each row has: created_by, deleted_by
  Traverse: newest --> oldest
  
  **Visibility Rules:**
  xmin committed AND xmax not set --> VISIBLE
  xmin = my_txn AND xmax not set --> VISIBLE
  xmin committed AND xmax = my_txn --> INVISIBLE
  xmin not committed --> INVISIBLE (unless READ_UNCOMMITTED)
end note

if (Row visible?) then (yes)
    :Return row to executor;
    if (SERIALIZABLE?) then (yes)
        :Add to read_set[] for conflict check;
    endif
else (no)
    :Skip row (invisible);
endif

|#F3E5F5| **Write Path** |
:INSERT / UPDATE / DELETE arrives;

:Request lock from Lock Manager;
note right
  **Lock Granularity:**
  ROW -- finest, most concurrent
  PAGE -- medium
  TABLE -- coarsest, least concurrent
  
  **Lock Modes:**
  SHARED -- multiple readers
  EXCLUSIVE -- single writer
  UPDATE -- intent to write
end note

|#FCE4EC| **Lock Manager** |
if (Lock available?) then (yes)
    :Grant lock;
    :Add to held_locks[];
else (no -- conflict)
    :Add to wait queue;
    :Trigger deadlock check;

    |#FFEBEE| **Deadlock Detector** |
    :Build wait-for graph;
    note right
      **Wait-For Graph:**
      Node = Transaction
      Edge = Txn A waits for Txn B
      Cycle = Deadlock detected
      
      Check interval: 100ms
      Lock timeout: 5000ms
    end note

    if (Cycle found?) then (yes)
        :Select victim (youngest txn);
        :Abort victim transaction;
        :Release victim locks;
    else (no)
        :Wait until lock freed or timeout;
    endif
endif

|#F3E5F5| **Write Path** |
:Create new RowVersion;
note right
  **Version record:**
  created_by = my TxnId
  deleted_by = 0
  create_ts = current timestamp
  data = new row data
  prev = pointer to old version
end note
:Log operation in Undo Log;
:Add to write_set[];

|#FFF8E1| **Savepoint (optional)** |
if (SAVEPOINT issued?) then (yes)
    :Record write_set mark;
    :Push savepoint on stack;
endif

if (ROLLBACK TO savepoint?) then (yes)
    :Undo operations back to mark;
    :Restore write_set to mark;
    :Release locks acquired after mark;
endif

|#E8F5E9| **Commit / Abort** |
if (COMMIT?) then (yes)
    if (SERIALIZABLE?) then (yes)
        :Validate read_set -- no conflicts;
        if (Conflict found?) then (yes)
            :ABORT (serialization failure);
        else (no)
            :Proceed with commit;
        endif
    endif
    :Assign commit timestamp;
    :Set state = TXN_STATE_COMMITTED;
    :Mark all RowVersions as committed;
    :Release all locks;
    :Write commit record to log;
else (ROLLBACK)
    :Walk Undo Log in reverse;
    :Restore old RowVersions;
    :Set state = TXN_STATE_ABORTED;
    :Release all locks;
endif

|#E3F2FD| **Client** |
:Return result to client;
stop

@enduml
```

---

## 5. Distributed Cluster Architecture

Raft consensus, partitioning, replication, and cluster communication.

```plantuml
@startuml InventixDB_Cluster_Architecture
!theme plain
skinparam linetype ortho
skinparam ranksep 35
skinparam nodesep 30
skinparam defaultFontName "Segoe UI"
skinparam defaultFontSize 11
skinparam shadowing false
skinparam roundCorner 8

title **InventixDB -- Distributed Cluster Architecture**\nRaft Consensus | Hash/Range Partitioning | Sync/Async Replication\n

' ============================================================
' CLIENT
' ============================================================
rectangle "**Client Application**\n(inventix-client)" as CLIENT #E3F2FD {
}

' ============================================================
' LEADER NODE
' ============================================================
package "LEADER NODE (Raft Leader)" as LEADER_PKG #E8F5E9 {
    rectangle "**Raft State Machine**\n---\nRole: LEADER\nCurrent term: N\nHeartbeat every 1000ms\nLog replication to followers" as RAFT_LEADER #C8E6C9
    rectangle "**Partition Router**\n---\nMaintains partition map\nHash: key % partition_count\nRange: key boundaries\nRoutes queries to correct shard" as ROUTER #C8E6C9
    rectangle "**Replication Manager**\n---\nMode: SYNC / ASYNC / SYNC_ALL\nACK tracking per replica\nTimeout: 5000ms\nAuto-retry on failure" as REPL_MGR #C8E6C9
    rectangle "**Query Executor**\n---\nLocal execution\nor forward to worker\nScatter-gather for\nmulti-partition queries" as LEADER_EXEC #C8E6C9
}

' ============================================================
' FOLLOWER NODE 1
' ============================================================
package "FOLLOWER NODE 1" as FOLLOWER1_PKG #FFF3E0 {
    rectangle "**Raft State Machine**\n---\nRole: FOLLOWER\nVotes for leader\nAppends log entries\nApplies committed entries" as RAFT_F1 #FFE0B2
    rectangle "**Local Storage**\n---\nPartition shard data\nB+ Tree indexes\nSnapshot persistence" as STORE_F1 #FFE0B2
    rectangle "**Health Monitor**\n---\nReports heartbeat\nLoad: CPU + connections\nStatus: HEALTHY / DEGRADED" as HEALTH_F1 #FFE0B2
}

' ============================================================
' FOLLOWER NODE 2
' ============================================================
package "FOLLOWER NODE 2" as FOLLOWER2_PKG #F3E5F5 {
    rectangle "**Raft State Machine**\n---\nRole: FOLLOWER\nVotes for leader\nAppends log entries\nApplies committed entries" as RAFT_F2 #E1BEE7
    rectangle "**Local Storage**\n---\nPartition shard data\nB+ Tree indexes\nSnapshot persistence" as STORE_F2 #E1BEE7
    rectangle "**Health Monitor**\n---\nReports heartbeat\nLoad: CPU + connections\nStatus: HEALTHY / DEGRADED" as HEALTH_F2 #E1BEE7
}

' ============================================================
' CLUSTER COMMUNICATION
' ============================================================
package "Cluster Communication Protocol" as COMM #ECEFF1 {
    rectangle "**Message Types**\n---\nRAFT_VOTE_REQ (0x20)\nRAFT_VOTE_RESP (0x21)\nRAFT_APPEND (0x22)\nCLUSTER_HEARTBEAT (0x15)\nCLUSTER_FORWARD (0x13)\nCLUSTER_SYNC (0x12)" as MSG_TYPES #CFD8DC
    rectangle "**Wire Format**\n---\n[4B magic: INVX]\n[2B version]\n[1B type]\n[1B flags]\n[4B sequence]\n[4B payload_len]\n[payload...]" as WIRE #CFD8DC
}

' ============================================================
' CONNECTIONS
' ============================================================
CLIENT       -[#1565C0,bold]down-> RAFT_LEADER : Queries
RAFT_LEADER  -[#2E7D32,bold]down-> ROUTER
ROUTER       -[#2E7D32,bold]down-> LEADER_EXEC
ROUTER       -[#2E7D32,bold]down-> REPL_MGR

REPL_MGR -[#E65100,bold]right-> RAFT_F1 : AppendEntries\n(log replication)
REPL_MGR -[#6A1B9A,bold]right-> RAFT_F2 : AppendEntries\n(log replication)

RAFT_F1 -[#E65100]down-> STORE_F1
RAFT_F1 -[#E65100]down-> HEALTH_F1
RAFT_F2 -[#6A1B9A]down-> STORE_F2
RAFT_F2 -[#6A1B9A]down-> HEALTH_F2

HEALTH_F1 -[#546E7A,dashed]left-> RAFT_LEADER : Heartbeat ACK
HEALTH_F2 -[#546E7A,dashed]left-> RAFT_LEADER : Heartbeat ACK

LEADER_EXEC -[#37474F]down-> MSG_TYPES
RAFT_F1     -[#37474F]down-> WIRE
RAFT_F2     -[#37474F]down-> WIRE

note bottom of COMM
  **Raft Election Flow:**
  1. Leader heartbeat timeout expires (5000ms)
  2. Follower becomes CANDIDATE
  3. Increments term, votes for self
  4. Sends RAFT_VOTE_REQ to all nodes
  5. Majority votes received --> becomes LEADER
  6. Begins sending AppendEntries heartbeats
  
  **Max 32 nodes per cluster**
  **Max 256 partitions**
end note

@enduml
```

---

## 6. Network Protocol Flow

Client-server communication sequence showing the binary protocol handshake, authentication, and query execution.

```plantuml
@startuml InventixDB_Network_Protocol
!theme plain
skinparam linetype ortho
skinparam defaultFontName "Segoe UI"
skinparam defaultFontSize 11
skinparam shadowing false
skinparam sequenceArrowThickness 2
skinparam sequenceParticipantBackgroundColor #FFFFFF
skinparam sequenceParticipantBorderColor #37474F
skinparam sequenceLifeLineBorderColor #90A4AE

skinparam sequence {
    ArrowColor #37474F
    DividerBackgroundColor #ECEFF1
    GroupBackgroundColor #F5F5F5
}

title **InventixDB -- Network Protocol Sequence**\nBinary Protocol with Authentication and Query Flow\n

participant "**Client**\n(inventix-client)" as C #E3F2FD
participant "**Connection\nManager**" as CM #E8F5E9
participant "**Auth &\nSession**" as AUTH #FFF3E0
participant "**Query\nProcessor**" as QP #F3E5F5
participant "**Storage\nEngine**" as SE #ECEFF1

== TCP Connection Establishment ==

C -> CM : TCP connect (port 9876)
activate CM #E8F5E9
CM -> CM : Accept socket\nAllocate connection slot
CM --> C : Connection accepted
note right of CM
  **Connection State: CONNECTING**
  Max connections: 100
  IOCP/epoll async I/O
end note

== Authentication Handshake ==

C -> CM : MSG_TYPE_AUTH_REQUEST (0x04)\n[magic:INVX][ver:1.0][type:0x04]\n[username_len][password_len]\n[username][password]
CM -> AUTH : Forward auth request
activate AUTH #FFF3E0

AUTH -> AUTH : Lookup user in store
AUTH -> AUTH : PBKDF2-SHA256 hash\nverify against stored hash
note right of AUTH
  **Security checks:**
  - Max login attempts: configurable
  - Lockout after failures
  - Password min length: 6
  - Salt: 16 bytes random
end note

alt Authentication Success
    AUTH -> AUTH : Create session token\nAssign session_id\nRecord role & permissions
    AUTH --> CM : MSG_TYPE_AUTH_RESPONSE (0x05)\n[success:1][session_id][token]
    CM --> C : Forward auth response
    note right of CM
      **Connection State: AUTHENTICATED**
      Session timeout: 3600s
      Role-based permissions active
    end note
else Authentication Failed
    AUTH --> CM : MSG_TYPE_AUTH_RESPONSE\n[success:0][error:"Invalid credentials"]
    CM --> C : Forward error
    note right of CM
      **Connection State: ERROR**
      Increment failed attempts
      Lockout if max exceeded
    end note
end
deactivate AUTH

== Query Execution ==

C -> CM : MSG_TYPE_QUERY (0x01)\n[query_id][query_len][timeout_ms]\n[query: "SELECT * FROM users"]
CM -> CM : Verify session is valid\nCheck PERM_SELECT permission
CM -> QP : Forward query string
activate QP #F3E5F5

QP -> QP : Lexer: tokenize
QP -> QP : Parser: build AST
QP -> QP : Optimizer: plan query
QP -> SE : Execute plan (scan/index)
activate SE #ECEFF1
SE -> SE : Buffer pool lookup\nB+ Tree traversal\nRow filtering
SE --> QP : ResultSet (rows)
deactivate SE

QP --> CM : MSG_TYPE_QUERY_RESULT (0x02)\n[query_id][status:0][rows_affected]\n[result_data]
deactivate QP
CM --> C : Forward result

== Keepalive ==

C -> CM : MSG_TYPE_PING (0x06)\n[timestamp]
CM --> C : MSG_TYPE_PONG (0x07)\n[timestamp]
note right of CM
  **Keepalive interval: 30s**
  **Read timeout: 60s**
  **Idle timeout: 300s**
end note

== Connection Close ==

C -> CM : MSG_TYPE_CLOSE (0x08)
CM -> AUTH : Destroy session
CM -> CM : Release connection slot\nClose socket
CM --> C : Connection closed
deactivate CM

@enduml
```

---

## 7. Security Module Architecture

RBAC, password hashing, session management, and permission checking.

```plantuml
@startuml InventixDB_Security_Module
!theme plain
skinparam linetype ortho
skinparam ranksep 30
skinparam nodesep 20
skinparam defaultFontName "Segoe UI"
skinparam defaultFontSize 11
skinparam shadowing false
skinparam roundCorner 8

title **InventixDB -- Security Module Architecture**\nRBAC | PBKDF2-SHA256 | Session Management | AES-256 Encryption\n

package "Authentication Layer" as AUTH_LAYER #E3F2FD {
    rectangle "**PBKDF2-SHA256 Hasher**\n---\nSalt: 16 bytes random\nHash: 64 bytes output\nCost: 2^12 iterations\nConstant-time comparison" as HASHER #BBDEFB
    rectangle "**Login Handler**\n---\nValidate credentials\nCheck lockout status\nTrack failed attempts\nMax attempts: configurable" as LOGIN #BBDEFB
    rectangle "**User Store**\n---\nMax 256 users\nStored in KV store\nKey: SYS:USER:<name>\nFields: hash, salt, role" as USER_STORE #BBDEFB
}

package "Authorization Layer (RBAC)" as AUTHZ_LAYER #E8F5E9 {
    rectangle "**Role Registry**\n---\n5 predefined roles:\n  superadmin (all perms)\n  admin (manage + ops)\n  developer (DDL + DML)\n  analyst (SELECT only)\n  readonly (SELECT only)\nMax 32 custom roles" as ROLES #C8E6C9
    rectangle "**Permission Engine**\n---\n20 permission flags (bitmask):\n  CREATE_DB, DROP_DB, USE_DB\n  CREATE_TABLE, DROP_TABLE, ALTER_TABLE\n  SELECT, INSERT, UPDATE, DELETE\n  CREATE_USER, DROP_USER\n  GRANT, REVOKE\n  SHUTDOWN, RELOAD_CONFIG\n  VIEW_STATS, BACKUP, RESTORE\n  TRANSACTION" as PERMS #C8E6C9
    rectangle "**Permission Checker**\n---\nInput: user_role + operation\nOutput: ALLOW / DENY\nBitwise AND check\nPERM_ALL = 0xFFFFFFFF" as CHECKER #C8E6C9
}

package "Session Management" as SESSION_LAYER #FFF3E0 {
    rectangle "**Session Store**\n---\nMax 1024 active sessions\nToken: 64-byte random\nExpiry: configurable timeout\nLinked to user + role" as SESSIONS #FFE0B2
    rectangle "**Token Generator**\n---\nCryptographic random bytes\n64-byte session tokens\nUnique per session\nInvalidated on logout" as TOKEN_GEN #FFE0B2
    rectangle "**Session Validator**\n---\nCheck token validity\nCheck expiry time\nRefresh on activity\nAuto-expire idle sessions" as VALIDATOR #FFE0B2
}

package "Encryption Layer" as ENCRYPT_LAYER #F3E5F5 {
    rectangle "**AES-256 Engine**\n---\nKey: 32 bytes\nIV: 16 bytes random\nData-at-rest encryption\nPage-level encryption" as AES #E1BEE7
    rectangle "**Audit Logger**\n---\nLogs security events:\n  LOGIN_SUCCESS\n  LOGIN_FAILURE\n  PERMISSION_DENIED\n  USER_CREATED\n  ROLE_CHANGED\nTimestamp + user + IP" as AUDIT #E1BEE7
}

' ============================================================
' CONNECTIONS
' ============================================================
LOGIN     -[#1565C0,bold]right-> HASHER : Verify password
LOGIN     -[#1565C0,bold]down-> USER_STORE : Lookup user
HASHER    -[#1565C0]down-> USER_STORE : Read stored hash + salt

LOGIN     -[#2E7D32,bold]down-> SESSIONS : Create session on success
SESSIONS  -[#2E7D32]right-> TOKEN_GEN : Generate token
SESSIONS  -[#2E7D32]right-> VALIDATOR : Validate on each request

LOGIN     -[#E65100,bold]down-> ROLES : Fetch user role
ROLES     -[#E65100]right-> PERMS : Load permission bitmask
PERMS     -[#E65100]right-> CHECKER : Check per-query

LOGIN     -[#6A1B9A,dashed]down-> AUDIT : Log login event
CHECKER   -[#6A1B9A,dashed]down-> AUDIT : Log permission denied

AES       -[#37474F,dashed]left-> USER_STORE : Encrypt stored passwords

@enduml
```

---

## 8. Backup and Restore Flow

Complete backup/restore lifecycle with format options and compression.

```plantuml
@startuml InventixDB_Backup_Restore
!theme plain
skinparam linetype ortho
skinparam ranksep 25
skinparam nodesep 20
skinparam defaultFontName "Segoe UI"
skinparam defaultFontSize 11
skinparam shadowing false

title **InventixDB -- Backup & Restore Flow**\nMulti-format backup with compression support\n

|#E3F2FD| **User Command** |
start
if (Operation type?) then (BACKUP / EXPORT)

    |#E8F5E9| **Backup Engine** (backup.c -- 634 lines) |
    :Parse backup options;
    note right
      **BackupOptions:**
      format: BINARY / SQL / JSON / CSV
      compression: NONE / GZIP
      include_schema: bool
      include_data: bool
      include_indexes: bool
      specific_db: NULL = all
      specific_table: NULL = all
    end note

    :Write BackupHeader;
    note right
      **Header (fixed 64+ bytes):**
      magic: "INVXBKP\0"
      version: uint32
      format: uint32
      compression: uint32
      timestamp: uint64
      db_count: uint64
      table_count: uint64
      row_count: uint64
      data_size: uint64
      checksum: SHA-256 (64 chars)
    end note

    if (Format?) then (BINARY)
        :Serialize KV store\nas raw binary pages;
    elseif (SQL) then
        :Generate CREATE TABLE stmts;
        :Generate INSERT stmts\nfor each row;
    elseif (JSON) then
        :Export as JSON objects\nper table;
    else (CSV)
        :Write column headers;
        :Write comma-separated rows;
    endif

    if (Compression enabled?) then (yes)
        :Apply GZIP compression;
    endif

    :Compute SHA-256 checksum;
    :Write file to disk;

    |#ECEFF1| **Result** |
    :Return BackupResult;
    note right
      **BackupResult:**
      success: bool
      tables_backed: uint64
      rows_backed: uint64
      bytes_written: uint64
      elapsed_seconds: double
    end note

else (RESTORE / IMPORT)

    |#FFF3E0| **Restore Engine** |
    :Read and validate header;
    :Verify magic bytes ("INVXBKP");
    :Verify SHA-256 checksum;

    if (Compression?) then (yes)
        :Decompress GZIP;
    endif

    :Parse RestoreOptions;
    note right
      **RestoreOptions:**
      drop_existing: bool
      ignore_errors: bool
      restore_users: bool
      target_db: override name
    end note

    if (drop_existing?) then (yes)
        :DROP existing tables;
    endif

    if (Format?) then (BINARY)
        :Deserialize raw pages\ninto KV store;
    elseif (SQL) then
        :Execute CREATE TABLE stmts;
        :Execute INSERT stmts;
    elseif (JSON) then
        :Parse JSON objects;
        :Insert into tables;
    else (CSV)
        :Parse headers;
        :Insert rows;
    endif

    |#ECEFF1| **Result** |
    :Return RestoreResult;
    note right
      **RestoreResult:**
      success: bool
      tables_restored: uint64
      rows_restored: uint64
      bytes_read: uint64
      errors_count: int
    end note
endif

stop

@enduml
```

---

## 9. Memory Management Architecture

Three-tier memory system: Safe Allocator, Arena Allocator, and Memory Pool.

```plantuml
@startuml InventixDB_Memory_Management
!theme plain
skinparam linetype ortho
skinparam ranksep 30
skinparam nodesep 20
skinparam defaultFontName "Segoe UI"
skinparam defaultFontSize 11
skinparam shadowing false
skinparam roundCorner 8

title **InventixDB -- Memory Management Architecture**\nThree-tier allocation system with leak detection\n

package "Tier 1: Safe Memory Allocator (safe_mem.c -- 719 lines)" as T1 #FCE4EC {
    rectangle "**SAFE_MALLOC / SAFE_CALLOC**\n---\nWraps system malloc/calloc\nNULL check after every allocation\nLogs file:line:function on failure\nReturns NULL (never crashes)" as SAFE_ALLOC #FFCDD2
    rectangle "**Guard Bytes**\n---\n8 bytes BEFORE allocation (0xDE)\n8 bytes AFTER allocation (0xDE)\nChecked on every SAFE_FREE\nDetects buffer overflows" as GUARD #FFCDD2
    rectangle "**Allocation Tracker**\n---\nTracks up to 65,536 allocations\nRecords: ptr, size, file, line, func\nTimestamp per allocation\nUnique alloc_id per allocation" as TRACKER #FFCDD2
    rectangle "**Leak Reporter**\n---\nOn shutdown: scan all records\nReport unfreed allocations\nShow file:line where allocated\nCompute leaked_bytes + count" as LEAK_RPT #FFCDD2
    rectangle "**Statistics**\n---\ntotal_allocations\ntotal_frees\ncurrent_bytes\npeak_bytes\nfailed_allocations" as MEM_STATS #FFCDD2
}

package "Tier 2: Arena Allocator (memory.c -- 871 lines)" as T2 #E3F2FD {
    rectangle "**Arena (Region Allocator)**\n---\nBump-pointer allocation\nAll memory freed at once\nDefault block: 64 KB\nMax block: 16 MB\nPer-query scoped lifetime" as ARENA #BBDEFB
    rectangle "**Arena Checkpoint**\n---\nSave current position\nRestore to checkpoint\nNested scope support\nUsed for savepoints" as CHECKPOINT #BBDEFB
    rectangle "**Quota Enforcement**\n---\nMax memory per query: 32 MB\nReject allocation if exceeded\nPrevents runaway queries\nConfigurable per session" as QUOTA #BBDEFB
}

package "Tier 3: Memory Pool (memory.c)" as T3 #E8F5E9 {
    rectangle "**Fixed-Size Pool**\n---\nPre-allocated object blocks\nO(1) alloc and free\nFree list based recycling\nMax block size: 4096 bytes\nDefault: 256 objects" as POOL #C8E6C9
    rectangle "**Pool Statistics**\n---\nalloc_count\nfree_count\npeak_usage\nfree_list_length" as POOL_STATS #C8E6C9
}

package "System (OS)" as OS #ECEFF1 {
    rectangle "**malloc / calloc / realloc / free**\n(C standard library)" as LIBC #CFD8DC
}

' ============================================================
' CONNECTIONS
' ============================================================
SAFE_ALLOC -[#B71C1C,bold]down-> GUARD : Add guard bytes
GUARD      -[#B71C1C,bold]down-> TRACKER : Record allocation
SAFE_ALLOC -[#B71C1C]right-> MEM_STATS : Update counters
TRACKER    -[#B71C1C]right-> LEAK_RPT : Scan on shutdown

ARENA      -[#1565C0,bold]right-> CHECKPOINT : Save/Restore
ARENA      -[#1565C0,bold]down-> QUOTA : Check limit

POOL       -[#2E7D32]right-> POOL_STATS : Track usage

SAFE_ALLOC -[#37474F,bold]down-> LIBC : Calls malloc()
ARENA      -[#37474F,bold]down-> LIBC : Allocates blocks
POOL       -[#37474F,bold]down-> LIBC : Pre-allocates slab

note bottom of OS
  **Usage patterns in InventixDB:**
  -- **Safe Allocator**: General-purpose allocations (default for all modules)
  -- **Arena**: Per-query allocations (parser AST, executor temp data)
  -- **Pool**: Fixed-size objects (lock entries, row buffers, connections)
end note

@enduml
```

---

## 10. Component Dependency Map

Shows which source modules depend on which others (compile-time dependencies).

```plantuml
@startuml InventixDB_Dependency_Map
!theme plain
skinparam linetype ortho
skinparam ranksep 25
skinparam nodesep 15
skinparam defaultFontName "Segoe UI"
skinparam defaultFontSize 10
skinparam shadowing false
skinparam roundCorner 6

skinparam component {
    BackgroundColor #FFFFFF
    BorderColor #37474F
    FontColor #212121
}

title **InventixDB -- Module Dependency Map**\nCompile-time dependencies between source modules\n

' ============================================================
' ENTRY POINTS (top)
' ============================================================
component "**main.c**\nCLI Entry" as MAIN #E3F2FD
component "**server.c**\nServer Entry" as SERVER #E3F2FD
component "**client.c**\nClient Entry" as CLIENT #E3F2FD

' ============================================================
' QUERY LAYER
' ============================================================
component "**lexer.c**\nTokenizer" as LEXER #FFF3E0
component "**parser.c**\nAST Builder" as PARSER #FFF3E0
component "**optimizer.c**\nQuery Planner" as OPTIMIZER #FFF3E0
component "**executor.c**\nPlan Runner" as EXECUTOR #FFF3E0
component "**prepared.c**\nStmt Cache" as PREPARED #FFF3E0
component "**join.c**\nJOIN Algos" as JOIN #FFF3E0
component "**query_result.c**\nSort/Limit" as QR #FFF3E0
component "**nosql.c**\nDocument Store" as NOSQL #FFF3E0

' ============================================================
' TRANSACTION LAYER
' ============================================================
component "**mvcc.c**\nVersioning" as MVCC #F3E5F5
component "**transaction.c**\nUndo Log" as TXN #F3E5F5

' ============================================================
' STORAGE LAYER
' ============================================================
component "**storage.c**\nKV Store" as STORAGE #ECEFF1
component "**btree.c**\nB+ Tree" as BTREE #ECEFF1
component "**buffer_pool.c**\nPage Cache" as BUFPOOL #ECEFF1
component "**pager.c**\nDisk I/O" as PAGER #ECEFF1
component "**storage_engine.c**\nMulti-Engine" as SENGINE #ECEFF1
component "**index.c**\nSecondary Idx" as INDEX #ECEFF1
component "**backup.c**\nBackup/Restore" as BACKUP #ECEFF1

' ============================================================
' NETWORK & CLUSTER
' ============================================================
component "**network.c**\nBinary Protocol" as NETWORK #FCE4EC
component "**distributed.c**\nMaster-Worker" as DIST #FCE4EC
component "**cluster.c**\nRaft Consensus" as CLUSTER #FCE4EC

' ============================================================
' SECURITY
' ============================================================
component "**security.c**\nRBAC/Crypto" as SECURITY #E8F5E9
component "**auth.c**\nLogin Handler" as AUTH #E8F5E9

' ============================================================
' INFRASTRUCTURE
' ============================================================
component "**config.c**\nINI Parser" as CONFIG #E0F7FA
component "**logger.c**\nLogging" as LOGGER #E0F7FA
component "**safe_mem.c**\nLeak Detection" as SAFEMEM #E0F7FA
component "**error.c**\nError Framework" as ERROR #E0F7FA
component "**memory.c**\nArena/Pool" as MEMORY #E0F7FA
component "**timeout_config.c**\nTimeouts" as TIMEOUT #E0F7FA

' ============================================================
' DEPENDENCIES
' ============================================================
' Entry points
MAIN    -[#1565C0]down-> LEXER
MAIN    -[#1565C0]down-> EXECUTOR
MAIN    -[#1565C0]down-> STORAGE
SERVER  -[#1565C0]down-> NETWORK
SERVER  -[#1565C0]down-> SECURITY
SERVER  -[#1565C0]down-> CLUSTER
CLIENT  -[#1565C0]down-> NETWORK

' Query pipeline
LEXER     -[#E65100]right-> PARSER
PARSER    -[#E65100]right-> OPTIMIZER
OPTIMIZER -[#E65100]right-> EXECUTOR
EXECUTOR  -[#E65100]down-> JOIN
EXECUTOR  -[#E65100]down-> QR
EXECUTOR  -[#E65100]down-> PREPARED
EXECUTOR  -[#E65100]down-> NOSQL
EXECUTOR  -[#E65100]down-> BACKUP

' Transaction
EXECUTOR -[#6A1B9A]down-> MVCC
EXECUTOR -[#6A1B9A]down-> TXN
MVCC     -[#6A1B9A]down-> STORAGE

' Storage
STORAGE  -[#37474F]down-> BTREE
BTREE    -[#37474F]down-> BUFPOOL
BUFPOOL  -[#37474F]down-> PAGER
STORAGE  -[#37474F]right-> INDEX
STORAGE  -[#37474F]right-> SENGINE

' Network/Cluster
NETWORK  -[#B71C1C]down-> TIMEOUT
DIST     -[#B71C1C]down-> NETWORK
CLUSTER  -[#B71C1C]down-> NETWORK

' Security
AUTH     -[#2E7D32]down-> STORAGE
SECURITY -[#2E7D32]down-> AUTH

' Infrastructure (everything depends on these)
SAFEMEM ..[#00695C]down.> ERROR : uses
LOGGER  ..[#00695C]down.> CONFIG : reads config
MEMORY  ..[#00695C]down.> SAFEMEM : wraps

@enduml
```

---

## Rendering Instructions

### Online (Quickest)

1. Go to [plantuml.com/plantuml/uml](https://www.plantuml.com/plantuml/uml/)
2. Copy any diagram block above (between the triple-backtick fences, starting from `@startuml` to `@enduml`)
3. Paste and click **Submit**

### VS Code

1. Install the **PlantUML** extension (`jebbs.plantuml`)
2. Open this file
3. Place cursor inside any `plantuml` code block
4. Press `Alt+D` to preview

### Command Line

```bash
# Install PlantUML
# (requires Java runtime)
java -jar plantuml.jar diagrams.md -o output/

# Or with Docker
docker run -v $(pwd):/data plantuml/plantuml diagrams.md
```

### Export Formats

PlantUML supports PNG, SVG, PDF, and EPS output. For high-resolution documentation, use SVG:

```bash
java -jar plantuml.jar -tsvg diagrams.md -o output/
```
