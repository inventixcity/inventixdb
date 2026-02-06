/**
 * InventixDB Transaction System
 * 
 * Provides ACID-compliant transaction support with:
 * - BEGIN / START TRANSACTION
 * - COMMIT
 * - ROLLBACK
 * - Savepoints (optional)
 * - Transaction isolation
 * 
 * Transaction Log Format:
 * Each operation during a transaction is logged to enable rollback.
 */

#ifndef INVENTIX_TRANSACTION_H
#define INVENTIX_TRANSACTION_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// -----------------------------------------------------------------------------
// Transaction States
// -----------------------------------------------------------------------------

typedef enum {
    TXN_STATE_NONE,         // No active transaction
    TXN_STATE_ACTIVE,       // Transaction in progress
    TXN_STATE_COMMITTED,    // Transaction committed successfully
    TXN_STATE_ABORTED       // Transaction rolled back
} TransactionState;

// -----------------------------------------------------------------------------
// Transaction Operation Types (for undo log)
// -----------------------------------------------------------------------------

typedef enum {
    TXN_OP_INSERT,          // Row inserted - undo by deleting
    TXN_OP_DELETE,          // Row deleted - undo by re-inserting
    TXN_OP_UPDATE,          // Row updated - undo by restoring old value
    TXN_OP_CREATE_TABLE,    // Table created - undo by dropping
    TXN_OP_DROP_TABLE,      // Table dropped - undo by recreating
    TXN_OP_CREATE_INDEX     // Index created - undo by dropping
} TransactionOpType;

// -----------------------------------------------------------------------------
// Transaction Operation Record (Undo Log Entry)
// -----------------------------------------------------------------------------

typedef struct TxnOperation {
    TransactionOpType type;         // Type of operation
    char *table_name;               // Affected table
    char *key;                      // Storage key (for row operations)
    char *old_value;                // Previous value (for UPDATE/DELETE rollback)
    size_t old_value_size;          // Size of old value
    char *schema_json;              // For CREATE/DROP TABLE rollback
    struct TxnOperation *next;      // Linked list for operation log
} TxnOperation;

// -----------------------------------------------------------------------------
// Savepoint Structure
// -----------------------------------------------------------------------------

typedef struct Savepoint {
    char *name;                     // Savepoint name
    int operation_count;            // Number of operations at savepoint
    struct Savepoint *next;         // Linked list of savepoints
} Savepoint;

// -----------------------------------------------------------------------------
// Transaction Structure
// -----------------------------------------------------------------------------

typedef struct Transaction {
    unsigned long txn_id;           // Unique transaction ID
    TransactionState state;         // Current state
    time_t start_time;              // When transaction started
    char *user;                     // User who started transaction
    char *database;                 // Database context
    
    // Undo Log
    TxnOperation *operations;       // Head of operations list (LIFO for rollback)
    int operation_count;            // Total operations in this transaction
    
    // Savepoints
    Savepoint *savepoints;          // Stack of savepoints
    
    // Statistics
    int inserts;                    // Number of inserts
    int deletes;                    // Number of deletes
    int updates;                    // Number of updates
} Transaction;

// -----------------------------------------------------------------------------
// Transaction Manager
// -----------------------------------------------------------------------------

typedef struct TransactionManager {
    Transaction *current_txn;       // Active transaction (single-user for now)
    unsigned long next_txn_id;      // Next transaction ID counter
    bool auto_commit;               // Auto-commit mode (default: true)
    
    // Statistics
    unsigned long total_commits;
    unsigned long total_rollbacks;
    unsigned long total_transactions;
} TransactionManager;

// Global transaction manager instance
extern TransactionManager g_txn_manager;

// -----------------------------------------------------------------------------
// Transaction Manager Functions
// -----------------------------------------------------------------------------

/**
 * Initialize the transaction manager
 */
void txn_manager_init(void);

/**
 * Cleanup the transaction manager
 */
void txn_manager_destroy(void);

/**
 * Check if auto-commit is enabled
 */
bool txn_is_auto_commit(void);

/**
 * Set auto-commit mode
 */
void txn_set_auto_commit(bool enabled);

// -----------------------------------------------------------------------------
// Transaction Control Functions
// -----------------------------------------------------------------------------

/**
 * Begin a new transaction
 * @param user Current user name
 * @param database Current database name
 * @return 0 on success, -1 if transaction already active
 */
int txn_begin(const char *user, const char *database);

/**
 * Commit the current transaction
 * @return 0 on success, -1 if no active transaction
 */
int txn_commit(void);

/**
 * Rollback the current transaction
 * @param store KVStore pointer for undo operations
 * @return 0 on success, -1 if no active transaction
 */
struct KVStore;
int txn_rollback(struct KVStore *store);

/**
 * Check if a transaction is active
 */
bool txn_is_active(void);

/**
 * Get current transaction state
 */
TransactionState txn_get_state(void);

/**
 * Get current transaction ID (0 if none)
 */
unsigned long txn_get_id(void);

// -----------------------------------------------------------------------------
// Savepoint Functions
// -----------------------------------------------------------------------------

/**
 * Create a savepoint
 * @param name Savepoint name
 * @return 0 on success, -1 on error
 */
int txn_savepoint(const char *name);

/**
 * Rollback to a savepoint
 * @param name Savepoint name
 * @param store KVStore pointer for undo operations
 * @return 0 on success, -1 if savepoint not found
 */
int txn_rollback_to_savepoint(const char *name, struct KVStore *store);

/**
 * Release a savepoint
 * @param name Savepoint name
 * @return 0 on success, -1 if savepoint not found
 */
int txn_release_savepoint(const char *name);

// -----------------------------------------------------------------------------
// Transaction Logging Functions (called by executor)
// -----------------------------------------------------------------------------

/**
 * Log an INSERT operation (for potential rollback)
 * @param table_name Table where row was inserted
 * @param key Storage key of the inserted row
 */
void txn_log_insert(const char *table_name, const char *key);

/**
 * Log a DELETE operation with the deleted data
 * @param table_name Table where row was deleted
 * @param key Storage key of the deleted row
 * @param old_value The deleted row data (JSON)
 * @param old_size Size of the old value
 */
void txn_log_delete(const char *table_name, const char *key, 
                    const char *old_value, size_t old_size);

/**
 * Log an UPDATE operation with old and new values
 * @param table_name Table where row was updated
 * @param key Storage key of the row
 * @param old_value Previous value before update
 * @param old_size Size of old value
 */
void txn_log_update(const char *table_name, const char *key,
                    const char *old_value, size_t old_size);

/**
 * Log a CREATE TABLE operation
 * @param table_name Name of created table
 * @param schema_json Schema definition (for potential undo)
 */
void txn_log_create_table(const char *table_name, const char *schema_json);

/**
 * Log a DROP TABLE operation
 * @param table_name Name of dropped table
 * @param schema_json Schema definition (to recreate on rollback)
 * @param rows_json All rows data (to restore on rollback) - can be NULL for empty tables
 */
void txn_log_drop_table(const char *table_name, const char *schema_json,
                        const char *rows_json);

/**
 * Log a CREATE INDEX operation
 * @param table_name Table name
 * @param column_name Indexed column
 */
void txn_log_create_index(const char *table_name, const char *column_name);

// -----------------------------------------------------------------------------
// Transaction Status and Info
// -----------------------------------------------------------------------------

/**
 * Get transaction statistics string
 * @param buffer Output buffer
 * @param size Buffer size
 */
void txn_get_stats(char *buffer, size_t size);

/**
 * Print current transaction info (for debugging)
 */
void txn_print_info(void);

// -----------------------------------------------------------------------------
// Convenience Macros
// -----------------------------------------------------------------------------

#define TXN_ACTIVE()        txn_is_active()
#define TXN_AUTO_COMMIT()   txn_is_auto_commit()
#define TXN_ID()            txn_get_id()

// Log operations only if in a transaction
#define TXN_LOG_INSERT(table, key) \
    do { if (TXN_ACTIVE()) txn_log_insert(table, key); } while(0)

#define TXN_LOG_DELETE(table, key, old_val, old_sz) \
    do { if (TXN_ACTIVE()) txn_log_delete(table, key, old_val, old_sz); } while(0)

#define TXN_LOG_UPDATE(table, key, old_val, old_sz) \
    do { if (TXN_ACTIVE()) txn_log_update(table, key, old_val, old_sz); } while(0)

#define TXN_LOG_CREATE_TABLE(table, schema) \
    do { if (TXN_ACTIVE()) txn_log_create_table(table, schema); } while(0)

#define TXN_LOG_DROP_TABLE(table, schema, rows) \
    do { if (TXN_ACTIVE()) txn_log_drop_table(table, schema, rows); } while(0)

#define TXN_LOG_CREATE_INDEX(table, col) \
    do { if (TXN_ACTIVE()) txn_log_create_index(table, col); } while(0)

#endif // INVENTIX_TRANSACTION_H
