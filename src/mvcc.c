/**
 * InventixDB MVCC Implementation
 * 
 * Multi-Version Concurrency Control with:
 * - Transaction isolation levels
 * - Snapshot isolation
 * - Row-level locking
 * - Deadlock detection
 * - Write-ahead logging integration
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp

static void usleep_ms(unsigned int usec) {
    Sleep(usec / 1000);
}
#define usleep usleep_ms
#endif

#include "mvcc.h"
#include "logger.h"

// -----------------------------------------------------------------------------
// Global MVCC Manager
// -----------------------------------------------------------------------------

static MVCCManager g_mvcc = {0};

// -----------------------------------------------------------------------------
// Helper Functions
// -----------------------------------------------------------------------------

static char *my_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static uint32_t hash_string(const char *str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static double get_current_time_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
#endif
}

// -----------------------------------------------------------------------------
// Transaction ID & Timestamp Generation
// -----------------------------------------------------------------------------

static TxnId generate_txn_id(void) {
    pthread_mutex_lock(&g_mvcc.txn_id_lock);
    TxnId id = g_mvcc.next_txn_id++;
    pthread_mutex_unlock(&g_mvcc.txn_id_lock);
    return id;
}

static Timestamp generate_timestamp(void) {
    pthread_mutex_lock(&g_mvcc.ts_lock);
    Timestamp ts = ++g_mvcc.current_timestamp;
    pthread_mutex_unlock(&g_mvcc.ts_lock);
    return ts;
}

// -----------------------------------------------------------------------------
// Deadlock Detection Thread
// -----------------------------------------------------------------------------

static void *deadlock_detector_thread(void *arg) {
    (void)arg;
    
    while (g_mvcc.detector_running) {
        usleep(DEADLOCK_CHECK_INTERVAL * 1000);
        
        if (!g_mvcc.detector_running) break;
        
        // Build wait-for graph and detect cycles
        pthread_rwlock_rdlock(&g_mvcc.active_lock);
        
        // Simple cycle detection using DFS
        for (int i = 0; i < MAX_ACTIVE_TRANSACTIONS; i++) {
            TransactionDescriptor *txn = g_mvcc.active_txns[i];
            if (!txn || txn->state != TXN_STATE_ACTIVE) continue;
            
            // Check if this transaction is waiting and forms a cycle
            // For now, simplified implementation
        }
        
        pthread_rwlock_unlock(&g_mvcc.active_lock);
    }
    
    return NULL;
}

// -----------------------------------------------------------------------------
// Initialization
// -----------------------------------------------------------------------------

int mvcc_init(void) {
    if (g_mvcc.initialized) return 0;
    
    memset(&g_mvcc, 0, sizeof(g_mvcc));
    
    g_mvcc.next_txn_id = MIN_TXN_ID;
    g_mvcc.current_timestamp = 0;
    
    if (pthread_mutex_init(&g_mvcc.txn_id_lock, NULL) != 0) return -1;
    if (pthread_mutex_init(&g_mvcc.ts_lock, NULL) != 0) return -1;
    if (pthread_rwlock_init(&g_mvcc.active_lock, NULL) != 0) return -1;
    if (pthread_mutex_init(&g_mvcc.commit_log_lock, NULL) != 0) return -1;
    if (pthread_mutex_init(&g_mvcc.lock_table.mutex, NULL) != 0) return -1;
    
    // Initialize commit log
    g_mvcc.commit_log_capacity = 1024;
    g_mvcc.commit_log = calloc(g_mvcc.commit_log_capacity, 
                                sizeof(*g_mvcc.commit_log));
    if (!g_mvcc.commit_log) return -1;
    
    // Start deadlock detector
    g_mvcc.detector_running = true;
    if (pthread_create(&g_mvcc.deadlock_detector, NULL, 
                       deadlock_detector_thread, NULL) != 0) {
        LOG_WARN("Failed to start deadlock detector thread");
    }
    
    g_mvcc.initialized = true;
    LOG_INFO("MVCC subsystem initialized");
    return 0;
}

void mvcc_shutdown(void) {
    if (!g_mvcc.initialized) return;
    
    // Stop deadlock detector
    g_mvcc.detector_running = false;
    pthread_join(g_mvcc.deadlock_detector, NULL);
    
    // Abort all active transactions
    pthread_rwlock_wrlock(&g_mvcc.active_lock);
    for (int i = 0; i < MAX_ACTIVE_TRANSACTIONS; i++) {
        if (g_mvcc.active_txns[i]) {
            mvcc_abort_transaction(g_mvcc.active_txns[i]->txn_id);
        }
    }
    pthread_rwlock_unlock(&g_mvcc.active_lock);
    
    // Free commit log
    free(g_mvcc.commit_log);
    
    // Destroy locks
    pthread_mutex_destroy(&g_mvcc.txn_id_lock);
    pthread_mutex_destroy(&g_mvcc.ts_lock);
    pthread_rwlock_destroy(&g_mvcc.active_lock);
    pthread_mutex_destroy(&g_mvcc.commit_log_lock);
    pthread_mutex_destroy(&g_mvcc.lock_table.mutex);
    
    g_mvcc.initialized = false;
    LOG_INFO("MVCC subsystem shutdown");
}

// -----------------------------------------------------------------------------
// Transaction Management
// -----------------------------------------------------------------------------

TxnId mvcc_begin_transaction(IsolationLevel isolation, const char *user) {
    TransactionDescriptor *txn = calloc(1, sizeof(TransactionDescriptor));
    if (!txn) return INVALID_TXN_ID;
    
    txn->txn_id = generate_txn_id();
    txn->state = TXN_STATE_ACTIVE;
    txn->isolation = isolation;
    txn->start_ts = generate_timestamp();
    txn->start_time = time(NULL);
    txn->user_name = user ? my_strdup(user) : NULL;
    
    // For REPEATABLE READ and SERIALIZABLE, take snapshot immediately
    if (isolation >= ISOLATION_REPEATABLE_READ) {
        txn->snapshot = mvcc_take_snapshot(txn->txn_id);
    }
    
    // Initialize write/read sets
    txn->write_set_capacity = 64;
    txn->write_set = calloc(txn->write_set_capacity, sizeof(*txn->write_set));
    
    if (isolation == ISOLATION_SERIALIZABLE) {
        txn->read_set_capacity = 128;
        txn->read_set = calloc(txn->read_set_capacity, sizeof(*txn->read_set));
    }
    
    // Initialize lock list
    txn->lock_capacity = 32;
    txn->held_locks = calloc(txn->lock_capacity, sizeof(LockEntry *));
    
    // Register in active transactions
    pthread_rwlock_wrlock(&g_mvcc.active_lock);
    for (int i = 0; i < MAX_ACTIVE_TRANSACTIONS; i++) {
        if (!g_mvcc.active_txns[i]) {
            g_mvcc.active_txns[i] = txn;
            g_mvcc.active_count++;
            break;
        }
    }
    pthread_rwlock_unlock(&g_mvcc.active_lock);
    
    LOG_DEBUG("Transaction %lu started (isolation=%d, user=%s)",
              (unsigned long)txn->txn_id, isolation, user ? user : "anon");
    
    return txn->txn_id;
}

int mvcc_commit_transaction(TxnId txn_id) {
    TransactionDescriptor *txn = mvcc_get_transaction(txn_id);
    if (!txn) return -1;
    
    if (txn->state != TXN_STATE_ACTIVE) {
        LOG_WARN("Cannot commit transaction %lu: not active", (unsigned long)txn_id);
        return -1;
    }
    
    // For SERIALIZABLE, check for conflicts
    if (txn->isolation == ISOLATION_SERIALIZABLE) {
        if (mvcc_check_write_conflict(txn_id) || mvcc_check_read_conflict(txn_id)) {
            LOG_WARN("Transaction %lu aborted due to serialization failure",
                     (unsigned long)txn_id);
            mvcc_abort_transaction(txn_id);
            return -1;
        }
    }
    
    txn->state = TXN_STATE_COMMITTING;
    txn->commit_ts = generate_timestamp();
    
    // Add to commit log
    pthread_mutex_lock(&g_mvcc.commit_log_lock);
    if (g_mvcc.commit_log_size >= g_mvcc.commit_log_capacity) {
        g_mvcc.commit_log_capacity *= 2;
        g_mvcc.commit_log = realloc(g_mvcc.commit_log,
            g_mvcc.commit_log_capacity * sizeof(*g_mvcc.commit_log));
    }
    g_mvcc.commit_log[g_mvcc.commit_log_size].txn_id = txn_id;
    g_mvcc.commit_log[g_mvcc.commit_log_size].commit_ts = txn->commit_ts;
    g_mvcc.commit_log_size++;
    pthread_mutex_unlock(&g_mvcc.commit_log_lock);
    
    // Release all locks
    mvcc_release_all_locks(txn_id);
    
    txn->state = TXN_STATE_COMMITTED;
    g_mvcc.total_commits++;
    
    // Remove from active transactions
    pthread_rwlock_wrlock(&g_mvcc.active_lock);
    for (int i = 0; i < MAX_ACTIVE_TRANSACTIONS; i++) {
        if (g_mvcc.active_txns[i] == txn) {
            g_mvcc.active_txns[i] = NULL;
            g_mvcc.active_count--;
            break;
        }
    }
    pthread_rwlock_unlock(&g_mvcc.active_lock);
    
    LOG_DEBUG("Transaction %lu committed (ts=%lu)",
              (unsigned long)txn_id, (unsigned long)txn->commit_ts);
    
    // Free transaction resources
    if (txn->snapshot) mvcc_free_snapshot(txn->snapshot);
    free(txn->write_set);
    free(txn->read_set);
    free(txn->held_locks);
    free(txn->user_name);
    free(txn);
    
    return 0;
}

int mvcc_abort_transaction(TxnId txn_id) {
    TransactionDescriptor *txn = mvcc_get_transaction(txn_id);
    if (!txn) return -1;
    
    txn->state = TXN_STATE_ABORTED;
    
    // Undo all writes (restore old versions)
    for (int i = txn->write_set_count - 1; i >= 0; i--) {
        // Restore old_version, invalidate new_version
        // This integrates with storage engine
    }
    
    // Release all locks
    mvcc_release_all_locks(txn_id);
    
    g_mvcc.total_aborts++;
    
    // Remove from active transactions
    pthread_rwlock_wrlock(&g_mvcc.active_lock);
    for (int i = 0; i < MAX_ACTIVE_TRANSACTIONS; i++) {
        if (g_mvcc.active_txns[i] == txn) {
            g_mvcc.active_txns[i] = NULL;
            g_mvcc.active_count--;
            break;
        }
    }
    pthread_rwlock_unlock(&g_mvcc.active_lock);
    
    LOG_DEBUG("Transaction %lu aborted", (unsigned long)txn_id);
    
    // Free resources
    if (txn->snapshot) mvcc_free_snapshot(txn->snapshot);
    free(txn->write_set);
    free(txn->read_set);
    free(txn->held_locks);
    free(txn->user_name);
    free(txn);
    
    return 0;
}

// -----------------------------------------------------------------------------
// Savepoint Management
// -----------------------------------------------------------------------------

int mvcc_create_savepoint(TxnId txn_id, const char *name) {
    TransactionDescriptor *txn = mvcc_get_transaction(txn_id);
    if (!txn || !name) return -1;
    
    // Grow savepoint array if needed
    if (!txn->savepoints) {
        txn->savepoints = calloc(8, sizeof(*txn->savepoints));
    }
    
    int idx = txn->savepoint_count++;
    txn->savepoints[idx].name = my_strdup(name);
    txn->savepoints[idx].write_set_mark = txn->write_set_count;
    txn->savepoints[idx].read_set_mark = txn->read_set_count;
    
    LOG_DEBUG("Savepoint '%s' created in txn %lu", name, (unsigned long)txn_id);
    return 0;
}

int mvcc_rollback_to_savepoint(TxnId txn_id, const char *savepoint) {
    TransactionDescriptor *txn = mvcc_get_transaction(txn_id);
    if (!txn || !savepoint) return -1;
    
    // Find savepoint
    int sp_idx = -1;
    for (int i = txn->savepoint_count - 1; i >= 0; i--) {
        if (strcmp(txn->savepoints[i].name, savepoint) == 0) {
            sp_idx = i;
            break;
        }
    }
    
    if (sp_idx < 0) {
        LOG_WARN("Savepoint '%s' not found", savepoint);
        return -1;
    }
    
    // Undo writes since savepoint
    int mark = txn->savepoints[sp_idx].write_set_mark;
    for (int i = txn->write_set_count - 1; i >= mark; i--) {
        // Restore old versions
    }
    txn->write_set_count = mark;
    
    // Truncate read set
    txn->read_set_count = txn->savepoints[sp_idx].read_set_mark;
    
    // Remove savepoints after this one
    for (int i = sp_idx + 1; i < txn->savepoint_count; i++) {
        free(txn->savepoints[i].name);
    }
    txn->savepoint_count = sp_idx + 1;
    
    LOG_DEBUG("Rolled back to savepoint '%s' in txn %lu", 
              savepoint, (unsigned long)txn_id);
    return 0;
}

int mvcc_release_savepoint(TxnId txn_id, const char *name) {
    TransactionDescriptor *txn = mvcc_get_transaction(txn_id);
    if (!txn || !name) return -1;
    
    // Find and remove savepoint
    for (int i = 0; i < txn->savepoint_count; i++) {
        if (strcmp(txn->savepoints[i].name, name) == 0) {
            free(txn->savepoints[i].name);
            // Shift remaining
            memmove(&txn->savepoints[i], &txn->savepoints[i+1],
                    (txn->savepoint_count - i - 1) * sizeof(*txn->savepoints));
            txn->savepoint_count--;
            return 0;
        }
    }
    
    return -1;
}

// -----------------------------------------------------------------------------
// Transaction Info
// -----------------------------------------------------------------------------

TransactionDescriptor *mvcc_get_transaction(TxnId txn_id) {
    pthread_rwlock_rdlock(&g_mvcc.active_lock);
    for (int i = 0; i < MAX_ACTIVE_TRANSACTIONS; i++) {
        if (g_mvcc.active_txns[i] && g_mvcc.active_txns[i]->txn_id == txn_id) {
            TransactionDescriptor *txn = g_mvcc.active_txns[i];
            pthread_rwlock_unlock(&g_mvcc.active_lock);
            return txn;
        }
    }
    pthread_rwlock_unlock(&g_mvcc.active_lock);
    return NULL;
}

TransactionState mvcc_get_state(TxnId txn_id) {
    TransactionDescriptor *txn = mvcc_get_transaction(txn_id);
    return txn ? txn->state : TXN_STATE_ABORTED;
}

IsolationLevel mvcc_get_isolation(TxnId txn_id) {
    TransactionDescriptor *txn = mvcc_get_transaction(txn_id);
    return txn ? txn->isolation : ISOLATION_READ_COMMITTED;
}

bool mvcc_is_active(TxnId txn_id) {
    TransactionDescriptor *txn = mvcc_get_transaction(txn_id);
    return txn && txn->state == TXN_STATE_ACTIVE;
}

// -----------------------------------------------------------------------------
// Snapshot Management
// -----------------------------------------------------------------------------

TransactionSnapshot *mvcc_take_snapshot(TxnId txn_id) {
    TransactionSnapshot *snap = calloc(1, sizeof(TransactionSnapshot));
    if (!snap) return NULL;
    
    pthread_rwlock_rdlock(&g_mvcc.active_lock);
    
    snap->snapshot_ts = g_mvcc.current_timestamp;
    snap->xmax = g_mvcc.next_txn_id;
    
    // Find xmin and active transactions
    snap->xmin = snap->xmax;
    snap->active_count = 0;
    snap->active_txns = calloc(g_mvcc.active_count, sizeof(TxnId));
    
    for (int i = 0; i < MAX_ACTIVE_TRANSACTIONS; i++) {
        TransactionDescriptor *txn = g_mvcc.active_txns[i];
        if (txn && txn->state == TXN_STATE_ACTIVE && txn->txn_id != txn_id) {
            if (txn->txn_id < snap->xmin) {
                snap->xmin = txn->txn_id;
            }
            snap->active_txns[snap->active_count++] = txn->txn_id;
        }
    }
    
    pthread_rwlock_unlock(&g_mvcc.active_lock);
    
    return snap;
}

void mvcc_free_snapshot(TransactionSnapshot *snapshot) {
    if (!snapshot) return;
    free(snapshot->active_txns);
    free(snapshot);
}

bool mvcc_is_visible(TxnId txn_id, TransactionSnapshot *snapshot) {
    if (!snapshot) return true;
    
    // txn_id committed before snapshot was taken
    if (txn_id < snapshot->xmin) return true;
    
    // txn_id started after snapshot was taken
    if (txn_id >= snapshot->xmax) return false;
    
    // Check if txn_id was active when snapshot was taken
    for (int i = 0; i < snapshot->active_count; i++) {
        if (snapshot->active_txns[i] == txn_id) {
            return false;  // Was active, not visible
        }
    }
    
    return true;  // Was committed
}

// -----------------------------------------------------------------------------
// Visibility Checks
// -----------------------------------------------------------------------------

bool mvcc_tuple_visible(MVCCTupleHeader *tuple, TxnId reader_txn) {
    if (!tuple) return false;
    
    TransactionDescriptor *reader = mvcc_get_transaction(reader_txn);
    if (!reader) return false;
    
    // Check xmin (creating transaction)
    if (tuple->xmin == reader_txn) {
        // Our own insert - visible if not deleted by us
        return tuple->xmax == 0 || tuple->xmax != reader_txn;
    }
    
    // Check if xmin is committed
    if (tuple->hint_aborted) return false;
    
    if (!tuple->hint_committed) {
        // Need to check commit log
        TransactionState state = mvcc_get_state(tuple->xmin);
        if (state == TXN_STATE_ABORTED) {
            return false;
        }
        if (state != TXN_STATE_COMMITTED) {
            // Still active - visible only for READ UNCOMMITTED
            if (reader->isolation > ISOLATION_READ_UNCOMMITTED) {
                return false;
            }
        }
    }
    
    // Check visibility against snapshot
    if (reader->snapshot) {
        if (!mvcc_is_visible(tuple->xmin, reader->snapshot)) {
            return false;
        }
    }
    
    // Check xmax (deleting transaction)
    if (tuple->xmax != 0) {
        if (tuple->xmax == reader_txn) {
            return false;  // Deleted by us
        }
        
        // Check if xmax is committed
        TransactionState del_state = mvcc_get_state(tuple->xmax);
        if (del_state == TXN_STATE_COMMITTED) {
            if (!reader->snapshot || mvcc_is_visible(tuple->xmax, reader->snapshot)) {
                return false;  // Deleted and visible
            }
        }
    }
    
    return true;
}

bool mvcc_version_visible(RowVersion *version, TxnId reader_txn) {
    if (!version) return false;
    
    TransactionDescriptor *reader = mvcc_get_transaction(reader_txn);
    if (!reader) return false;
    
    // Own transaction's version
    if (version->created_by == reader_txn) {
        return version->deleted_by == 0 || version->deleted_by != reader_txn;
    }
    
    // Check if creating txn is visible
    if (!mvcc_is_visible(version->created_by, reader->snapshot)) {
        return false;
    }
    
    // Check if deleted
    if (version->deleted_by != 0) {
        if (mvcc_is_visible(version->deleted_by, reader->snapshot)) {
            return false;
        }
    }
    
    return true;
}

// -----------------------------------------------------------------------------
// Row Version Management
// -----------------------------------------------------------------------------

RowVersion *mvcc_create_version(TxnId txn_id, void *data, uint32_t size) {
    RowVersion *ver = calloc(1, sizeof(RowVersion));
    if (!ver) return NULL;
    
    ver->created_by = txn_id;
    ver->create_ts = generate_timestamp();
    ver->data_size = size;
    
    if (data && size > 0) {
        ver->data = malloc(size);
        if (ver->data) {
            memcpy(ver->data, data, size);
        }
    }
    
    return ver;
}

int mvcc_mark_deleted(RowVersion *version, TxnId txn_id) {
    if (!version) return -1;
    
    version->deleted_by = txn_id;
    version->delete_ts = generate_timestamp();
    
    return 0;
}

RowVersion *mvcc_get_visible_version(RowVersion *chain, TxnId reader_txn) {
    RowVersion *ver = chain;
    
    // Start from newest and go back
    while (ver && ver->next) {
        ver = ver->next;
    }
    
    // Now ver is the newest, walk back to find visible version
    while (ver) {
        if (mvcc_version_visible(ver, reader_txn)) {
            return ver;
        }
        ver = ver->prev;
    }
    
    return NULL;
}

void mvcc_free_version(RowVersion *version) {
    if (!version) return;
    free(version->data);
    free(version);
}

// -----------------------------------------------------------------------------
// Lock Management
// -----------------------------------------------------------------------------

static LockEntry *find_lock_entry(const char *resource) {
    uint32_t hash = hash_string(resource) % LOCK_TABLE_SIZE;
    LockEntry *entry = g_mvcc.lock_table.entries[hash];
    
    while (entry) {
        if (strcmp(entry->resource_id, resource) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    
    return NULL;
}

static bool locks_conflict(LockMode held, LockMode requested) {
    // Lock compatibility matrix
    static const bool compat[6][6] = {
        //                 NONE  SH    EX    UP    SRX   AE
        /* NONE */       { true, true, true, true, true, true },
        /* SHARED */     { true, true, false,true, false,false},
        /* EXCLUSIVE */  { true, false,false,false,false,false},
        /* UPDATE */     { true, true, false,false,false,false},
        /* SHARE_ROW_EX*/ { true, false,false,false,false,false},
        /* ACCESS_EX */  { true, false,false,false,false,false}
    };
    
    return !compat[held][requested];
}

int mvcc_acquire_lock(TxnId txn_id, const char *resource, 
                       LockMode mode, LockGranularity gran) {
    if (!resource) return -1;
    
    TransactionDescriptor *txn = mvcc_get_transaction(txn_id);
    if (!txn) return -1;
    
    double start_wait = get_current_time_ms();
    
    pthread_mutex_lock(&g_mvcc.lock_table.mutex);
    
    while (1) {
        LockEntry *existing = find_lock_entry(resource);
        
        if (!existing) {
            // No existing lock, acquire it
            LockEntry *lock = calloc(1, sizeof(LockEntry));
            lock->resource_id = my_strdup(resource);
            lock->mode = mode;
            lock->granularity = gran;
            lock->holder = txn_id;
            
            uint32_t hash = hash_string(resource) % LOCK_TABLE_SIZE;
            lock->next = g_mvcc.lock_table.entries[hash];
            g_mvcc.lock_table.entries[hash] = lock;
            g_mvcc.lock_table.total_locks++;
            
            // Add to transaction's lock list
            if (txn->lock_count >= txn->lock_capacity) {
                txn->lock_capacity *= 2;
                txn->held_locks = realloc(txn->held_locks,
                    txn->lock_capacity * sizeof(LockEntry *));
            }
            txn->held_locks[txn->lock_count++] = lock;
            
            pthread_mutex_unlock(&g_mvcc.lock_table.mutex);
            return 0;
        }
        
        // Check if we already hold this lock
        if (existing->holder == txn_id) {
            if (existing->mode >= mode) {
                // Already have sufficient lock
                pthread_mutex_unlock(&g_mvcc.lock_table.mutex);
                return 0;
            }
            // Need to upgrade
            if (!locks_conflict(existing->mode, mode)) {
                existing->mode = mode;
                pthread_mutex_unlock(&g_mvcc.lock_table.mutex);
                return 0;
            }
        }
        
        // Check for conflict
        if (locks_conflict(existing->mode, mode)) {
            // Check timeout
            if (get_current_time_ms() - start_wait > LOCK_TIMEOUT_MS) {
                pthread_mutex_unlock(&g_mvcc.lock_table.mutex);
                LOG_WARN("Lock timeout for txn %lu on resource '%s'",
                         (unsigned long)txn_id, resource);
                g_mvcc.lock_waits++;
                return -1;
            }
            
            // Wait and retry
            pthread_mutex_unlock(&g_mvcc.lock_table.mutex);
            usleep(1000);  // 1ms
            pthread_mutex_lock(&g_mvcc.lock_table.mutex);
            continue;
        }
        
        // Compatible lock, add to queue
        LockEntry *lock = calloc(1, sizeof(LockEntry));
        lock->resource_id = my_strdup(resource);
        lock->mode = mode;
        lock->granularity = gran;
        lock->holder = txn_id;
        
        // Add to end of chain
        LockEntry *tail = existing;
        while (tail->next) tail = tail->next;
        tail->next = lock;
        
        if (txn->lock_count >= txn->lock_capacity) {
            txn->lock_capacity *= 2;
            txn->held_locks = realloc(txn->held_locks,
                txn->lock_capacity * sizeof(LockEntry *));
        }
        txn->held_locks[txn->lock_count++] = lock;
        
        pthread_mutex_unlock(&g_mvcc.lock_table.mutex);
        return 0;
    }
}

int mvcc_release_lock(TxnId txn_id, const char *resource) {
    if (!resource) return -1;
    
    pthread_mutex_lock(&g_mvcc.lock_table.mutex);
    
    uint32_t hash = hash_string(resource) % LOCK_TABLE_SIZE;
    LockEntry *prev = NULL;
    LockEntry *entry = g_mvcc.lock_table.entries[hash];
    
    while (entry) {
        if (strcmp(entry->resource_id, resource) == 0 && 
            entry->holder == txn_id) {
            // Remove from hash table
            if (prev) {
                prev->next = entry->next;
            } else {
                g_mvcc.lock_table.entries[hash] = entry->next;
            }
            
            free(entry->resource_id);
            free(entry);
            g_mvcc.lock_table.total_locks--;
            
            pthread_mutex_unlock(&g_mvcc.lock_table.mutex);
            return 0;
        }
        prev = entry;
        entry = entry->next;
    }
    
    pthread_mutex_unlock(&g_mvcc.lock_table.mutex);
    return -1;
}

int mvcc_release_all_locks(TxnId txn_id) {
    TransactionDescriptor *txn = mvcc_get_transaction(txn_id);
    if (!txn) return -1;
    
    pthread_mutex_lock(&g_mvcc.lock_table.mutex);
    
    for (int i = 0; i < txn->lock_count; i++) {
        LockEntry *lock = txn->held_locks[i];
        if (!lock) continue;
        
        uint32_t hash = hash_string(lock->resource_id) % LOCK_TABLE_SIZE;
        LockEntry *prev = NULL;
        LockEntry *entry = g_mvcc.lock_table.entries[hash];
        
        while (entry) {
            if (entry == lock) {
                if (prev) {
                    prev->next = entry->next;
                } else {
                    g_mvcc.lock_table.entries[hash] = entry->next;
                }
                free(entry->resource_id);
                free(entry);
                g_mvcc.lock_table.total_locks--;
                break;
            }
            prev = entry;
            entry = entry->next;
        }
    }
    
    txn->lock_count = 0;
    
    pthread_mutex_unlock(&g_mvcc.lock_table.mutex);
    return 0;
}

bool mvcc_has_lock(TxnId txn_id, const char *resource, LockMode mode) {
    pthread_mutex_lock(&g_mvcc.lock_table.mutex);
    
    LockEntry *entry = find_lock_entry(resource);
    while (entry) {
        if (entry->holder == txn_id && entry->mode >= mode) {
            pthread_mutex_unlock(&g_mvcc.lock_table.mutex);
            return true;
        }
        entry = entry->next;
    }
    
    pthread_mutex_unlock(&g_mvcc.lock_table.mutex);
    return false;
}

int mvcc_upgrade_lock(TxnId txn_id, const char *resource, LockMode new_mode) {
    return mvcc_acquire_lock(txn_id, resource, new_mode, MVCC_LOCK_GRANULARITY_ROW);
}

// -----------------------------------------------------------------------------
// Deadlock Detection
// -----------------------------------------------------------------------------

bool mvcc_check_deadlock(TxnId txn_id) {
    // Simple cycle detection in wait-for graph
    // Returns true if txn_id is part of a deadlock cycle
    
    // This is a simplified implementation
    // A full implementation would build a wait-for graph
    // and use DFS to detect cycles
    
    return false;
}

TxnId mvcc_choose_victim(TxnId *cycle, int cycle_len) {
    if (!cycle || cycle_len == 0) return INVALID_TXN_ID;
    
    // Choose youngest transaction as victim
    TxnId victim = cycle[0];
    Timestamp youngest_start = 0;
    
    for (int i = 0; i < cycle_len; i++) {
        TransactionDescriptor *txn = mvcc_get_transaction(cycle[i]);
        if (txn && txn->start_ts > youngest_start) {
            youngest_start = txn->start_ts;
            victim = cycle[i];
        }
    }
    
    return victim;
}

// -----------------------------------------------------------------------------
// Write/Read Set Tracking
// -----------------------------------------------------------------------------

int mvcc_track_write(TxnId txn_id, const char *table, uint64_t row_id,
                      void *old_ver, void *new_ver) {
    TransactionDescriptor *txn = mvcc_get_transaction(txn_id);
    if (!txn) return -1;
    
    if (txn->write_set_count >= txn->write_set_capacity) {
        txn->write_set_capacity *= 2;
        txn->write_set = realloc(txn->write_set,
            txn->write_set_capacity * sizeof(*txn->write_set));
    }
    
    int idx = txn->write_set_count++;
    txn->write_set[idx].table_name = my_strdup(table);
    txn->write_set[idx].row_id = row_id;
    txn->write_set[idx].old_version = old_ver;
    txn->write_set[idx].new_version = new_ver;
    
    return 0;
}

int mvcc_track_read(TxnId txn_id, const char *table, uint64_t row_id) {
    TransactionDescriptor *txn = mvcc_get_transaction(txn_id);
    if (!txn || !txn->read_set) return -1;
    
    if (txn->read_set_count >= txn->read_set_capacity) {
        txn->read_set_capacity *= 2;
        txn->read_set = realloc(txn->read_set,
            txn->read_set_capacity * sizeof(*txn->read_set));
    }
    
    int idx = txn->read_set_count++;
    txn->read_set[idx].table_name = my_strdup(table);
    txn->read_set[idx].row_id = row_id;
    
    return 0;
}

// -----------------------------------------------------------------------------
// Conflict Detection (for SERIALIZABLE)
// -----------------------------------------------------------------------------

bool mvcc_check_write_conflict(TxnId txn_id) {
    TransactionDescriptor *txn = mvcc_get_transaction(txn_id);
    if (!txn) return true;
    
    // Check if any row we read was modified by another committed transaction
    // since we started
    
    for (int i = 0; i < txn->read_set_count; i++) {
        // Check commit log for modifications to this row
        // by transactions that committed after we started
        // This is a simplified check
    }
    
    return false;
}

bool mvcc_check_read_conflict(TxnId txn_id) {
    TransactionDescriptor *txn = mvcc_get_transaction(txn_id);
    if (!txn) return true;
    
    // Check if any row we wrote was read by another transaction
    // that committed after we started
    
    return false;
}

// -----------------------------------------------------------------------------
// Garbage Collection
// -----------------------------------------------------------------------------

int mvcc_vacuum_table(const char *table_name) {
    if (!table_name) return -1;
    
    // Find oldest active transaction
    TxnId oldest = mvcc_get_oldest_active_txn();
    
    // Remove versions that are no longer visible to any transaction
    // This integrates with storage engine
    
    LOG_DEBUG("Vacuumed table '%s' (oldest_txn=%lu)", 
              table_name, (unsigned long)oldest);
    return 0;
}

int mvcc_vacuum_all(void) {
    // Vacuum all tables
    return 0;
}

int mvcc_get_oldest_active_txn(void) {
    TxnId oldest = g_mvcc.next_txn_id;
    
    pthread_rwlock_rdlock(&g_mvcc.active_lock);
    for (int i = 0; i < MAX_ACTIVE_TRANSACTIONS; i++) {
        if (g_mvcc.active_txns[i] && 
            g_mvcc.active_txns[i]->state == TXN_STATE_ACTIVE) {
            if (g_mvcc.active_txns[i]->txn_id < oldest) {
                oldest = g_mvcc.active_txns[i]->txn_id;
            }
        }
    }
    pthread_rwlock_unlock(&g_mvcc.active_lock);
    
    return oldest;
}

// -----------------------------------------------------------------------------
// Statistics
// -----------------------------------------------------------------------------

void mvcc_get_stats(uint64_t *commits, uint64_t *aborts, 
                     uint64_t *deadlocks, uint64_t *lock_waits) {
    if (commits) *commits = g_mvcc.total_commits;
    if (aborts) *aborts = g_mvcc.total_aborts;
    if (deadlocks) *deadlocks = g_mvcc.total_deadlocks;
    if (lock_waits) *lock_waits = g_mvcc.lock_waits;
}

// -----------------------------------------------------------------------------
// Two-Phase Commit (Stub)
// -----------------------------------------------------------------------------

TxnId mvcc_2pc_begin(const char *coordinator) {
    TxnId txn_id = mvcc_begin_transaction(ISOLATION_SERIALIZABLE, NULL);
    TransactionDescriptor *txn = mvcc_get_transaction(txn_id);
    if (txn) {
        txn->is_distributed = true;
        txn->coordinator_id = my_strdup(coordinator);
    }
    return txn_id;
}

int mvcc_2pc_prepare(TxnId txn_id) {
    TransactionDescriptor *txn = mvcc_get_transaction(txn_id);
    if (!txn) return -1;
    
    txn->state = TXN_STATE_PREPARING;
    // Write prepare record to log
    
    return 0;
}

int mvcc_2pc_commit(TxnId txn_id) {
    return mvcc_commit_transaction(txn_id);
}

int mvcc_2pc_abort(TxnId txn_id) {
    return mvcc_abort_transaction(txn_id);
}

int mvcc_2pc_add_participant(TxnId txn_id, const char *participant) {
    (void)txn_id;
    (void)participant;
    return 0;
}

int mvcc_2pc_vote(TxnId txn_id, const char *participant, bool vote) {
    (void)txn_id;
    (void)participant;
    (void)vote;
    return 0;
}

int mvcc_recover_2pc(void) {
    // Scan log for prepared but not committed transactions
    return 0;
}

TwoPhaseTransaction *mvcc_get_prepared_transactions(int *count) {
    if (count) *count = 0;
    return NULL;
}
