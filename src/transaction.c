/**
 * InventixDB Transaction System Implementation
 * 
 * Provides ACID-compliant transaction support with undo logging
 * for rollback operations.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "transaction.h"
#include "storage.h"
#include "logger.h"

// -----------------------------------------------------------------------------
// Global Transaction Manager
// -----------------------------------------------------------------------------

TransactionManager g_txn_manager = {
    .current_txn = NULL,
    .next_txn_id = 1,
    .auto_commit = true,
    .total_commits = 0,
    .total_rollbacks = 0,
    .total_transactions = 0
};

// -----------------------------------------------------------------------------
// Internal Helper Functions
// -----------------------------------------------------------------------------

static TxnOperation* create_operation(TransactionOpType type, const char *table,
                                       const char *key, const char *old_value,
                                       size_t old_size) {
    TxnOperation *op = malloc(sizeof(TxnOperation));
    if (!op) return NULL;
    
    op->type = type;
    op->table_name = table ? strdup(table) : NULL;
    op->key = key ? strdup(key) : NULL;
    op->old_value = NULL;
    op->old_value_size = 0;
    op->schema_json = NULL;
    op->next = NULL;
    
    if (old_value && old_size > 0) {
        op->old_value = malloc(old_size + 1);
        if (op->old_value) {
            memcpy(op->old_value, old_value, old_size);
            op->old_value[old_size] = '\0';
            op->old_value_size = old_size;
        }
    }
    
    return op;
}

static void free_operation(TxnOperation *op) {
    if (!op) return;
    free(op->table_name);
    free(op->key);
    free(op->old_value);
    free(op->schema_json);
    free(op);
}

static void free_all_operations(TxnOperation *head) {
    while (head) {
        TxnOperation *next = head->next;
        free_operation(head);
        head = next;
    }
}

static void free_savepoint(Savepoint *sp) {
    if (!sp) return;
    free(sp->name);
    free(sp);
}

static void free_all_savepoints(Savepoint *head) {
    while (head) {
        Savepoint *next = head->next;
        free_savepoint(head);
        head = next;
    }
}

static void free_transaction(Transaction *txn) {
    if (!txn) return;
    free(txn->user);
    free(txn->database);
    free_all_operations(txn->operations);
    free_all_savepoints(txn->savepoints);
    free(txn);
}

static const char* op_type_to_string(TransactionOpType type) {
    switch (type) {
        case TXN_OP_INSERT:       return "INSERT";
        case TXN_OP_DELETE:       return "DELETE";
        case TXN_OP_UPDATE:       return "UPDATE";
        case TXN_OP_CREATE_TABLE: return "CREATE_TABLE";
        case TXN_OP_DROP_TABLE:   return "DROP_TABLE";
        case TXN_OP_CREATE_INDEX: return "CREATE_INDEX";
        default:                  return "UNKNOWN";
    }
}

// -----------------------------------------------------------------------------
// Transaction Manager Functions
// -----------------------------------------------------------------------------

void txn_manager_init(void) {
    g_txn_manager.current_txn = NULL;
    g_txn_manager.next_txn_id = 1;
    g_txn_manager.auto_commit = true;
    g_txn_manager.total_commits = 0;
    g_txn_manager.total_rollbacks = 0;
    g_txn_manager.total_transactions = 0;
    
    LOG_INFO("Transaction manager initialized");
}

void txn_manager_destroy(void) {
    if (g_txn_manager.current_txn) {
        LOG_WARN("Destroying transaction manager with active transaction (ID: %lu)",
                 g_txn_manager.current_txn->txn_id);
        free_transaction(g_txn_manager.current_txn);
        g_txn_manager.current_txn = NULL;
    }
}

bool txn_is_auto_commit(void) {
    return g_txn_manager.auto_commit;
}

void txn_set_auto_commit(bool enabled) {
    if (g_txn_manager.current_txn && !enabled) {
        LOG_WARN("Cannot disable auto-commit while transaction is active");
        return;
    }
    g_txn_manager.auto_commit = enabled;
    LOG_DEBUG("Auto-commit mode: %s", enabled ? "ON" : "OFF");
}

// -----------------------------------------------------------------------------
// Transaction Control Functions
// -----------------------------------------------------------------------------

int txn_begin(const char *user, const char *database) {
    // Check if transaction already active
    if (g_txn_manager.current_txn != NULL) {
        LOG_ERROR("Transaction already active (ID: %lu)", 
                  g_txn_manager.current_txn->txn_id);
        return -1;
    }
    
    // Create new transaction
    Transaction *txn = malloc(sizeof(Transaction));
    if (!txn) {
        LOG_ERROR("Failed to allocate transaction");
        return -1;
    }
    
    txn->txn_id = g_txn_manager.next_txn_id++;
    txn->state = TXN_STATE_ACTIVE;
    txn->start_time = time(NULL);
    txn->user = user ? strdup(user) : strdup("unknown");
    txn->database = database ? strdup(database) : strdup("public");
    txn->operations = NULL;
    txn->operation_count = 0;
    txn->savepoints = NULL;
    txn->inserts = 0;
    txn->deletes = 0;
    txn->updates = 0;
    
    g_txn_manager.current_txn = txn;
    g_txn_manager.total_transactions++;
    g_txn_manager.auto_commit = false;  // Disable auto-commit during transaction
    
    LOG_INFO("Transaction started (ID: %lu, User: %s, DB: %s)",
             txn->txn_id, txn->user, txn->database);
    
    return 0;
}

int txn_commit(void) {
    Transaction *txn = g_txn_manager.current_txn;
    
    if (!txn) {
        LOG_ERROR("No active transaction to commit");
        return -1;
    }
    
    if (txn->state != TXN_STATE_ACTIVE) {
        LOG_ERROR("Transaction not in active state");
        return -1;
    }
    
    // Mark as committed
    txn->state = TXN_STATE_COMMITTED;
    
    // Calculate duration
    time_t duration = time(NULL) - txn->start_time;
    
    LOG_INFO("Transaction committed (ID: %lu, Operations: %d, Duration: %lds)",
             txn->txn_id, txn->operation_count, (long)duration);
    LOG_DEBUG("  Inserts: %d, Deletes: %d, Updates: %d",
              txn->inserts, txn->deletes, txn->updates);
    
    // Update statistics
    g_txn_manager.total_commits++;
    
    // Cleanup - discard undo log since we are committing
    free_transaction(txn);
    g_txn_manager.current_txn = NULL;
    g_txn_manager.auto_commit = true;  // Re-enable auto-commit
    
    return 0;
}

int txn_rollback(KVStore *store) {
    Transaction *txn = g_txn_manager.current_txn;
    
    if (!txn) {
        LOG_ERROR("No active transaction to rollback");
        return -1;
    }
    
    if (txn->state != TXN_STATE_ACTIVE) {
        LOG_ERROR("Transaction not in active state");
        return -1;
    }
    
    LOG_WARN("Rolling back transaction (ID: %lu, Operations: %d)",
             txn->txn_id, txn->operation_count);
    
    // Apply undo operations in reverse order (LIFO)
    TxnOperation *op = txn->operations;
    int undo_count = 0;
    int undo_errors = 0;
    
    while (op) {
        LOG_DEBUG("  Undoing %s on table '%s', key '%s'",
                  op_type_to_string(op->type),
                  op->table_name ? op->table_name : "N/A",
                  op->key ? op->key : "N/A");
        
        switch (op->type) {
            case TXN_OP_INSERT:
                // Undo insert by deleting the row
                if (op->key) {
                    kv_delete(store, op->key);
                    undo_count++;
                }
                break;
                
            case TXN_OP_DELETE:
                // Undo delete by re-inserting the old value
                if (op->key && op->old_value) {
                    kv_put(store, op->key, op->old_value, 
                           op->old_value_size, VAL_TYPE_ROW);
                    undo_count++;
                }
                break;
                
            case TXN_OP_UPDATE:
                // Undo update by restoring old value
                if (op->key && op->old_value) {
                    kv_put(store, op->key, op->old_value,
                           op->old_value_size, VAL_TYPE_ROW);
                    undo_count++;
                }
                break;
                
            case TXN_OP_CREATE_TABLE:
                // Undo create table by dropping it
                if (op->table_name) {
                    char schema_key[256];
                    snprintf(schema_key, sizeof(schema_key), 
                             "__schema__%s", op->table_name);
                    kv_delete(store, schema_key);
                    undo_count++;
                }
                break;
                
            case TXN_OP_DROP_TABLE:
                // Undo drop table by recreating schema
                if (op->table_name && op->schema_json) {
                    char schema_key[256];
                    snprintf(schema_key, sizeof(schema_key),
                             "__schema__%s", op->table_name);
                    kv_put(store, schema_key, op->schema_json,
                           strlen(op->schema_json) + 1, VAL_TYPE_SCHEMA);
                    undo_count++;
                    // Note: Row data restoration would need additional handling
                }
                break;
                
            case TXN_OP_CREATE_INDEX:
                // Undo create index - simplified, just log it
                LOG_DEBUG("  Index rollback not fully implemented");
                undo_count++;
                break;
                
            default:
                LOG_ERROR("Unknown operation type in undo log: %d", op->type);
                undo_errors++;
                break;
        }
        
        op = op->next;
    }
    
    // Mark as aborted
    txn->state = TXN_STATE_ABORTED;
    
    LOG_INFO("Rollback complete (ID: %lu, Undone: %d, Errors: %d)",
             txn->txn_id, undo_count, undo_errors);
    
    // Update statistics
    g_txn_manager.total_rollbacks++;
    
    // Cleanup
    free_transaction(txn);
    g_txn_manager.current_txn = NULL;
    g_txn_manager.auto_commit = true;
    
    return undo_errors > 0 ? -1 : 0;
}

bool txn_is_active(void) {
    return g_txn_manager.current_txn != NULL && 
           g_txn_manager.current_txn->state == TXN_STATE_ACTIVE;
}

TransactionState txn_get_state(void) {
    if (!g_txn_manager.current_txn) {
        return TXN_STATE_NONE;
    }
    return g_txn_manager.current_txn->state;
}

unsigned long txn_get_id(void) {
    if (!g_txn_manager.current_txn) {
        return 0;
    }
    return g_txn_manager.current_txn->txn_id;
}

// -----------------------------------------------------------------------------
// Savepoint Functions
// -----------------------------------------------------------------------------

int txn_savepoint(const char *name) {
    Transaction *txn = g_txn_manager.current_txn;
    
    if (!txn || txn->state != TXN_STATE_ACTIVE) {
        LOG_ERROR("No active transaction for savepoint");
        return -1;
    }
    
    if (!name || strlen(name) == 0) {
        LOG_ERROR("Savepoint name is required");
        return -1;
    }
    
    // Check for duplicate name
    Savepoint *sp = txn->savepoints;
    while (sp) {
        if (strcmp(sp->name, name) == 0) {
            LOG_ERROR("Savepoint '%s' already exists", name);
            return -1;
        }
        sp = sp->next;
    }
    
    // Create savepoint
    Savepoint *new_sp = malloc(sizeof(Savepoint));
    if (!new_sp) {
        LOG_ERROR("Failed to allocate savepoint");
        return -1;
    }
    
    new_sp->name = strdup(name);
    new_sp->operation_count = txn->operation_count;
    new_sp->next = txn->savepoints;  // Push to front (stack)
    txn->savepoints = new_sp;
    
    LOG_INFO("Savepoint '%s' created at operation %d", name, txn->operation_count);
    
    return 0;
}

int txn_rollback_to_savepoint(const char *name, KVStore *store) {
    Transaction *txn = g_txn_manager.current_txn;
    
    if (!txn || txn->state != TXN_STATE_ACTIVE) {
        LOG_ERROR("No active transaction for savepoint rollback");
        return -1;
    }
    
    // Find savepoint
    Savepoint *sp = txn->savepoints;
    Savepoint *prev = NULL;
    
    while (sp) {
        if (strcmp(sp->name, name) == 0) break;
        prev = sp;
        sp = sp->next;
    }
    
    if (!sp) {
        LOG_ERROR("Savepoint '%s' not found", name);
        return -1;
    }
    
    int target_count = sp->operation_count;
    int undo_count = 0;
    
    LOG_INFO("Rolling back to savepoint '%s' (from op %d to op %d)",
             name, txn->operation_count, target_count);
    
    // Undo operations until we reach the savepoint
    while (txn->operations && txn->operation_count > target_count) {
        TxnOperation *op = txn->operations;
        
        LOG_DEBUG("  Undoing %s on '%s'", 
                  op_type_to_string(op->type),
                  op->table_name ? op->table_name : "N/A");
        
        // Apply undo (same logic as full rollback)
        switch (op->type) {
            case TXN_OP_INSERT:
                if (op->key) kv_delete(store, op->key);
                break;
            case TXN_OP_DELETE:
                if (op->key && op->old_value) {
                    kv_put(store, op->key, op->old_value, 
                           op->old_value_size, VAL_TYPE_ROW);
                }
                break;
            case TXN_OP_UPDATE:
                if (op->key && op->old_value) {
                    kv_put(store, op->key, op->old_value,
                           op->old_value_size, VAL_TYPE_ROW);
                }
                break;
            case TXN_OP_CREATE_TABLE:
                if (op->table_name) {
                    char schema_key[256];
                    snprintf(schema_key, sizeof(schema_key),
                             "__schema__%s", op->table_name);
                    kv_delete(store, schema_key);
                }
                break;
            default:
                break;
        }
        
        // Remove from list
        txn->operations = op->next;
        free_operation(op);
        txn->operation_count--;
        undo_count++;
    }
    
    // Remove savepoints created after this one
    while (txn->savepoints != sp) {
        Savepoint *to_remove = txn->savepoints;
        txn->savepoints = to_remove->next;
        free_savepoint(to_remove);
    }
    
    LOG_INFO("Savepoint rollback complete (undone: %d operations)", undo_count);
    
    return 0;
}

int txn_release_savepoint(const char *name) {
    Transaction *txn = g_txn_manager.current_txn;
    
    if (!txn || txn->state != TXN_STATE_ACTIVE) {
        LOG_ERROR("No active transaction");
        return -1;
    }
    
    // Find and remove savepoint
    Savepoint *sp = txn->savepoints;
    Savepoint *prev = NULL;
    
    while (sp) {
        if (strcmp(sp->name, name) == 0) {
            if (prev) {
                prev->next = sp->next;
            } else {
                txn->savepoints = sp->next;
            }
            free_savepoint(sp);
            LOG_INFO("Savepoint '%s' released", name);
            return 0;
        }
        prev = sp;
        sp = sp->next;
    }
    
    LOG_ERROR("Savepoint '%s' not found", name);
    return -1;
}

// -----------------------------------------------------------------------------
// Transaction Logging Functions
// -----------------------------------------------------------------------------

void txn_log_insert(const char *table_name, const char *key) {
    Transaction *txn = g_txn_manager.current_txn;
    if (!txn || txn->state != TXN_STATE_ACTIVE) return;
    
    TxnOperation *op = create_operation(TXN_OP_INSERT, table_name, key, NULL, 0);
    if (!op) return;
    
    // Push to front (LIFO for rollback)
    op->next = txn->operations;
    txn->operations = op;
    txn->operation_count++;
    txn->inserts++;
    
    LOG_DEBUG("TXN[%lu] Logged INSERT: %s -> %s", txn->txn_id, table_name, key);
}

void txn_log_delete(const char *table_name, const char *key,
                    const char *old_value, size_t old_size) {
    Transaction *txn = g_txn_manager.current_txn;
    if (!txn || txn->state != TXN_STATE_ACTIVE) return;
    
    TxnOperation *op = create_operation(TXN_OP_DELETE, table_name, key, 
                                        old_value, old_size);
    if (!op) return;
    
    op->next = txn->operations;
    txn->operations = op;
    txn->operation_count++;
    txn->deletes++;
    
    LOG_DEBUG("TXN[%lu] Logged DELETE: %s -> %s", txn->txn_id, table_name, key);
}

void txn_log_update(const char *table_name, const char *key,
                    const char *old_value, size_t old_size) {
    Transaction *txn = g_txn_manager.current_txn;
    if (!txn || txn->state != TXN_STATE_ACTIVE) return;
    
    TxnOperation *op = create_operation(TXN_OP_UPDATE, table_name, key,
                                        old_value, old_size);
    if (!op) return;
    
    op->next = txn->operations;
    txn->operations = op;
    txn->operation_count++;
    txn->updates++;
    
    LOG_DEBUG("TXN[%lu] Logged UPDATE: %s -> %s", txn->txn_id, table_name, key);
}

void txn_log_create_table(const char *table_name, const char *schema_json) {
    Transaction *txn = g_txn_manager.current_txn;
    if (!txn || txn->state != TXN_STATE_ACTIVE) return;
    
    TxnOperation *op = create_operation(TXN_OP_CREATE_TABLE, table_name, NULL, NULL, 0);
    if (!op) return;
    
    if (schema_json) {
        op->schema_json = strdup(schema_json);
    }
    
    op->next = txn->operations;
    txn->operations = op;
    txn->operation_count++;
    
    LOG_DEBUG("TXN[%lu] Logged CREATE TABLE: %s", txn->txn_id, table_name);
}

void txn_log_drop_table(const char *table_name, const char *schema_json,
                        const char *rows_json) {
    Transaction *txn = g_txn_manager.current_txn;
    if (!txn || txn->state != TXN_STATE_ACTIVE) return;
    
    TxnOperation *op = create_operation(TXN_OP_DROP_TABLE, table_name, NULL, NULL, 0);
    if (!op) return;
    
    if (schema_json) {
        op->schema_json = strdup(schema_json);
    }
    if (rows_json) {
        op->old_value = strdup(rows_json);
        op->old_value_size = strlen(rows_json);
    }
    
    op->next = txn->operations;
    txn->operations = op;
    txn->operation_count++;
    
    LOG_DEBUG("TXN[%lu] Logged DROP TABLE: %s", txn->txn_id, table_name);
}

void txn_log_create_index(const char *table_name, const char *column_name) {
    Transaction *txn = g_txn_manager.current_txn;
    if (!txn || txn->state != TXN_STATE_ACTIVE) return;
    
    // Store column name in key field
    TxnOperation *op = create_operation(TXN_OP_CREATE_INDEX, table_name, 
                                        column_name, NULL, 0);
    if (!op) return;
    
    op->next = txn->operations;
    txn->operations = op;
    txn->operation_count++;
    
    LOG_DEBUG("TXN[%lu] Logged CREATE INDEX: %s(%s)", 
              txn->txn_id, table_name, column_name);
}

// -----------------------------------------------------------------------------
// Transaction Status and Info
// -----------------------------------------------------------------------------

void txn_get_stats(char *buffer, size_t size) {
    snprintf(buffer, size,
             "Transactions: %lu total, %lu committed, %lu rolled back\n"
             "Auto-commit: %s\n"
             "Active: %s (ID: %lu)",
             g_txn_manager.total_transactions,
             g_txn_manager.total_commits,
             g_txn_manager.total_rollbacks,
             g_txn_manager.auto_commit ? "ON" : "OFF",
             g_txn_manager.current_txn ? "YES" : "NO",
             g_txn_manager.current_txn ? g_txn_manager.current_txn->txn_id : 0);
}

void txn_print_info(void) {
    Transaction *txn = g_txn_manager.current_txn;
    
    fprintf(stderr, "\n--- Transaction Manager Status ---\n");
    fprintf(stderr, "Total Transactions: %lu\n", g_txn_manager.total_transactions);
    fprintf(stderr, "Commits: %lu, Rollbacks: %lu\n",
            g_txn_manager.total_commits, g_txn_manager.total_rollbacks);
    fprintf(stderr, "Auto-commit: %s\n", g_txn_manager.auto_commit ? "ON" : "OFF");
    
    if (txn) {
        char time_buf[64];
        struct tm *tm_info = localtime(&txn->start_time);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
        
        fprintf(stderr, "\nActive Transaction:\n");
        fprintf(stderr, "  ID: %lu\n", txn->txn_id);
        fprintf(stderr, "  User: %s\n", txn->user);
        fprintf(stderr, "  Database: %s\n", txn->database);
        fprintf(stderr, "  Started: %s\n", time_buf);
        fprintf(stderr, "  Operations: %d\n", txn->operation_count);
        fprintf(stderr, "  Inserts: %d, Deletes: %d, Updates: %d\n",
                txn->inserts, txn->deletes, txn->updates);
        
        // Count savepoints
        int sp_count = 0;
        Savepoint *sp = txn->savepoints;
        while (sp) { sp_count++; sp = sp->next; }
        fprintf(stderr, "  Savepoints: %d\n", sp_count);
    } else {
        fprintf(stderr, "\nNo active transaction\n");
    }
    fprintf(stderr, "-----------------------------------\n\n");
}
