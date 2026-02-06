/**
 * InventixDB Multi-Version Concurrency Control (MVCC)
 * 
 * Features:
 * 1. Transaction isolation levels (READ UNCOMMITTED, READ COMMITTED, 
 *    REPEATABLE READ, SERIALIZABLE)
 * 2. Row versioning with visibility rules
 * 3. Snapshot isolation
 * 4. Deadlock detection
 * 5. Two-Phase Locking (2PL) with deadlock prevention
 * 
 * MVCC allows multiple transactions to read/write without blocking,
 * using version chains to provide consistent snapshots.
 */

#ifndef INVENTIX_MVCC_H
#define INVENTIX_MVCC_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define MAX_ACTIVE_TRANSACTIONS  256
#define VERSION_CHAIN_MAX        16      // Max versions per row
#define LOCK_TABLE_SIZE          4096
#define DEADLOCK_CHECK_INTERVAL  100     // Check every 100ms
#define LOCK_TIMEOUT_MS          5000    // 5 second lock timeout

// -----------------------------------------------------------------------------
// Transaction Isolation Levels
// -----------------------------------------------------------------------------

typedef enum {
    ISOLATION_READ_UNCOMMITTED,  // Can see uncommitted changes
    ISOLATION_READ_COMMITTED,    // Only see committed changes
    ISOLATION_REPEATABLE_READ,   // Snapshot at first read
    ISOLATION_SERIALIZABLE       // Full serializability
} IsolationLevel;

// -----------------------------------------------------------------------------
// Transaction States
// -----------------------------------------------------------------------------

typedef enum {
    TXN_STATE_ACTIVE,       // Currently executing
    TXN_STATE_PREPARING,    // 2PC prepare phase
    TXN_STATE_COMMITTED,    // Successfully committed
    TXN_STATE_ABORTED,      // Rolled back
    TXN_STATE_COMMITTING    // In process of committing
} TransactionState;

// -----------------------------------------------------------------------------
// Transaction ID and Timestamps
// -----------------------------------------------------------------------------

typedef uint64_t TxnId;         // Transaction ID
typedef uint64_t Timestamp;     // Logical timestamp

#define INVALID_TXN_ID      0
#define MIN_TXN_ID          1
#define MAX_TXN_ID          UINT64_MAX

// -----------------------------------------------------------------------------
// Row Version Header
// -----------------------------------------------------------------------------

typedef struct RowVersion {
    TxnId created_by;           // Transaction that created this version
    TxnId deleted_by;           // Transaction that deleted (0 = not deleted)
    Timestamp create_ts;        // Creation timestamp
    Timestamp delete_ts;        // Deletion timestamp (0 = not deleted)
    
    uint32_t data_size;         // Size of row data
    void *data;                 // Actual row data
    
    struct RowVersion *prev;    // Previous version (older)
    struct RowVersion *next;    // Next version (newer)
} RowVersion;

// -----------------------------------------------------------------------------
// MVCC Tuple Header (stored with each row)
// -----------------------------------------------------------------------------

typedef struct {
    TxnId xmin;                 // Creating transaction
    TxnId xmax;                 // Deleting transaction (0 = live)
    uint32_t t_ctid_block;      // Block number of current tuple
    uint16_t t_ctid_offset;     // Offset within block
    uint16_t t_infomask;        // Tuple flags
    
    // Hint bits
    uint8_t hint_committed:1;   // xmin is known committed
    uint8_t hint_aborted:1;     // xmin is known aborted
    uint8_t hint_updated:1;     // Tuple has been updated
    uint8_t hint_deleted:1;     // Tuple has been deleted
} MVCCTupleHeader;

// Tuple flags
#define HEAP_XMIN_COMMITTED     0x0100
#define HEAP_XMIN_ABORTED       0x0200
#define HEAP_XMAX_COMMITTED     0x0400
#define HEAP_XMAX_ABORTED       0x0800
#define HEAP_UPDATED            0x2000
#define HEAP_MOVED              0x4000

// -----------------------------------------------------------------------------
// Transaction Snapshot
// -----------------------------------------------------------------------------

typedef struct {
    TxnId xmin;                 // Oldest active txn at snapshot time
    TxnId xmax;                 // Next txn ID at snapshot time
    
    TxnId *active_txns;         // Array of active txn IDs
    int active_count;           // Number of active txns
    
    Timestamp snapshot_ts;      // When snapshot was taken
} TransactionSnapshot;

// -----------------------------------------------------------------------------
// Lock Types
// -----------------------------------------------------------------------------

typedef enum {
    MVCC_LOCK_NONE = 0,
    MVCC_LOCK_SHARED,            // Read lock (multiple allowed)
    MVCC_LOCK_EXCLUSIVE,         // Write lock (single holder)
    MVCC_LOCK_UPDATE,            // Update intent lock
    MVCC_LOCK_SHARE_ROW_EXCLUSIVE,
    MVCC_LOCK_ACCESS_EXCLUSIVE   // DDL lock
} LockMode;

typedef enum {
    MVCC_LOCK_GRANULARITY_ROW,
    MVCC_LOCK_GRANULARITY_PAGE,
    MVCC_LOCK_GRANULARITY_TABLE,
    MVCC_LOCK_GRANULARITY_DATABASE
} LockGranularity;

// -----------------------------------------------------------------------------
// Lock Entry
// -----------------------------------------------------------------------------

typedef struct LockEntry {
    char *resource_id;          // What is locked (table.rowid)
    LockGranularity granularity;
    LockMode mode;
    TxnId holder;               // Transaction holding the lock
    
    struct LockEntry *next;     // Next lock on same resource (queue)
    
    // For wait-for graph
    TxnId waiting_for;          // Txn we're waiting for (deadlock detection)
} LockEntry;

typedef struct {
    LockEntry *entries[LOCK_TABLE_SIZE];
    pthread_mutex_t mutex;
    int total_locks;
} LockTable;

// -----------------------------------------------------------------------------
// Transaction Descriptor
// -----------------------------------------------------------------------------

typedef struct {
    TxnId txn_id;
    TransactionState state;
    IsolationLevel isolation;
    
    // Timing
    Timestamp start_ts;
    Timestamp commit_ts;
    time_t start_time;          // Wall clock start
    
    // Snapshot for REPEATABLE READ and SERIALIZABLE
    TransactionSnapshot *snapshot;
    
    // Write set (for conflict detection)
    struct {
        char *table_name;
        uint64_t row_id;
        void *old_version;
        void *new_version;
    } *write_set;
    int write_set_count;
    int write_set_capacity;
    
    // Read set (for SERIALIZABLE)
    struct {
        char *table_name;
        uint64_t row_id;
    } *read_set;
    int read_set_count;
    int read_set_capacity;
    
    // Locks held
    LockEntry **held_locks;
    int lock_count;
    int lock_capacity;
    
    // Savepoints
    struct {
        char *name;
        int write_set_mark;
        int read_set_mark;
    } *savepoints;
    int savepoint_count;
    
    // For distributed transactions
    bool is_distributed;
    char *coordinator_id;
    
    // User context
    char *user_name;
    char *database_name;
    
} TransactionDescriptor;

// -----------------------------------------------------------------------------
// MVCC Manager
// -----------------------------------------------------------------------------

typedef struct {
    bool initialized;
    
    // Transaction ID generator
    TxnId next_txn_id;
    pthread_mutex_t txn_id_lock;
    
    // Timestamp generator
    Timestamp current_timestamp;
    pthread_mutex_t ts_lock;
    
    // Active transactions
    TransactionDescriptor *active_txns[MAX_ACTIVE_TRANSACTIONS];
    int active_count;
    pthread_rwlock_t active_lock;
    
    // Committed transaction log (for visibility)
    struct {
        TxnId txn_id;
        Timestamp commit_ts;
    } *commit_log;
    int commit_log_size;
    int commit_log_capacity;
    pthread_mutex_t commit_log_lock;
    
    // Lock manager
    LockTable lock_table;
    
    // Deadlock detector thread
    pthread_t deadlock_detector;
    bool detector_running;
    
    // Statistics
    uint64_t total_commits;
    uint64_t total_aborts;
    uint64_t total_deadlocks;
    uint64_t lock_waits;
    
} MVCCManager;

// -----------------------------------------------------------------------------
// MVCC API
// -----------------------------------------------------------------------------

// Initialization
int mvcc_init(void);
void mvcc_shutdown(void);

// Transaction management
TxnId mvcc_begin_transaction(IsolationLevel isolation, const char *user);
int mvcc_commit_transaction(TxnId txn_id);
int mvcc_abort_transaction(TxnId txn_id);
int mvcc_rollback_to_savepoint(TxnId txn_id, const char *savepoint);
int mvcc_create_savepoint(TxnId txn_id, const char *name);
int mvcc_release_savepoint(TxnId txn_id, const char *name);

// Transaction info
TransactionDescriptor *mvcc_get_transaction(TxnId txn_id);
TransactionState mvcc_get_state(TxnId txn_id);
IsolationLevel mvcc_get_isolation(TxnId txn_id);
bool mvcc_is_active(TxnId txn_id);

// Snapshot management
TransactionSnapshot *mvcc_take_snapshot(TxnId txn_id);
void mvcc_free_snapshot(TransactionSnapshot *snapshot);
bool mvcc_is_visible(TxnId txn_id, TransactionSnapshot *snapshot);

// Visibility checks
bool mvcc_tuple_visible(MVCCTupleHeader *tuple, TxnId reader_txn);
bool mvcc_version_visible(RowVersion *version, TxnId reader_txn);

// Row version management
RowVersion *mvcc_create_version(TxnId txn_id, void *data, uint32_t size);
int mvcc_mark_deleted(RowVersion *version, TxnId txn_id);
RowVersion *mvcc_get_visible_version(RowVersion *chain, TxnId reader_txn);
void mvcc_free_version(RowVersion *version);

// Lock management
int mvcc_acquire_lock(TxnId txn_id, const char *resource, 
                       LockMode mode, LockGranularity gran);
int mvcc_release_lock(TxnId txn_id, const char *resource);
int mvcc_release_all_locks(TxnId txn_id);
bool mvcc_has_lock(TxnId txn_id, const char *resource, LockMode mode);
int mvcc_upgrade_lock(TxnId txn_id, const char *resource, LockMode new_mode);

// Deadlock detection
bool mvcc_check_deadlock(TxnId txn_id);
TxnId mvcc_choose_victim(TxnId *cycle, int cycle_len);

// Write/Read set tracking
int mvcc_track_write(TxnId txn_id, const char *table, uint64_t row_id,
                      void *old_ver, void *new_ver);
int mvcc_track_read(TxnId txn_id, const char *table, uint64_t row_id);

// Conflict detection (for SERIALIZABLE)
bool mvcc_check_write_conflict(TxnId txn_id);
bool mvcc_check_read_conflict(TxnId txn_id);

// Garbage collection (vacuum)
int mvcc_vacuum_table(const char *table_name);
int mvcc_vacuum_all(void);
int mvcc_get_oldest_active_txn(void);

// Statistics
void mvcc_get_stats(uint64_t *commits, uint64_t *aborts, 
                     uint64_t *deadlocks, uint64_t *lock_waits);

// -----------------------------------------------------------------------------
// Convenience Macros
// -----------------------------------------------------------------------------

#define TXN_BEGIN(iso) mvcc_begin_transaction(iso, NULL)
#define TXN_COMMIT(txn) mvcc_commit_transaction(txn)
#define TXN_ABORT(txn) mvcc_abort_transaction(txn)
#define TXN_SAVEPOINT(txn, name) mvcc_create_savepoint(txn, name)
#define TXN_ROLLBACK_TO(txn, name) mvcc_rollback_to_savepoint(txn, name)

// Lock convenience
#define ROW_LOCK_SHARED(txn, res) \
    mvcc_acquire_lock(txn, res, LOCK_SHARED, LOCK_GRANULARITY_ROW)
#define ROW_LOCK_EXCLUSIVE(txn, res) \
    mvcc_acquire_lock(txn, res, LOCK_EXCLUSIVE, LOCK_GRANULARITY_ROW)
#define TABLE_LOCK_SHARED(txn, res) \
    mvcc_acquire_lock(txn, res, LOCK_SHARED, LOCK_GRANULARITY_TABLE)
#define TABLE_LOCK_EXCLUSIVE(txn, res) \
    mvcc_acquire_lock(txn, res, LOCK_EXCLUSIVE, LOCK_GRANULARITY_TABLE)

// -----------------------------------------------------------------------------
// Two-Phase Commit (2PC) for Distributed Transactions
// -----------------------------------------------------------------------------

typedef enum {
    TWOPHASE_INIT,
    TWOPHASE_PREPARING,
    TWOPHASE_PREPARED,
    TWOPHASE_COMMITTING,
    TWOPHASE_COMMITTED,
    TWOPHASE_ABORTING,
    TWOPHASE_ABORTED
} TwoPhaseState;

typedef struct {
    TxnId global_txn_id;
    char *coordinator_id;
    TwoPhaseState state;
    
    // Participants
    char **participant_ids;
    int participant_count;
    bool *participant_votes;  // true = prepared, false = abort
    
    // Timing
    time_t prepare_time;
    time_t decision_time;
    
} TwoPhaseTransaction;

// 2PC API
TxnId mvcc_2pc_begin(const char *coordinator);
int mvcc_2pc_prepare(TxnId txn_id);
int mvcc_2pc_commit(TxnId txn_id);
int mvcc_2pc_abort(TxnId txn_id);
int mvcc_2pc_add_participant(TxnId txn_id, const char *participant);
int mvcc_2pc_vote(TxnId txn_id, const char *participant, bool vote);

// Recovery
int mvcc_recover_2pc(void);
TwoPhaseTransaction *mvcc_get_prepared_transactions(int *count);

#endif // INVENTIX_MVCC_H
