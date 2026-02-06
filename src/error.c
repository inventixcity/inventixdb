/**
 * InventixDB Error Handling Implementation
 * 
 * Thread-local error state with chaining and bilingual messages.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#define THREAD_LOCAL __declspec(thread)
#else
#include <pthread.h>
#define THREAD_LOCAL __thread
#endif

#include "error.h"
#include "logger.h"

// ============================================================================
// THREAD-LOCAL ERROR STATE
// ============================================================================

static THREAD_LOCAL ErrorState tls_error_state = {0};
static bool g_error_initialized = false;

// ============================================================================
// TIMESTAMP
// ============================================================================

static uint64_t get_timestamp_ms(void) {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (t / 10000) - 11644473600000ULL;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void error_init(void) {
    g_error_initialized = true;
    memset(&tls_error_state, 0, sizeof(ErrorState));
}

void error_shutdown(void) {
    g_error_initialized = false;
}

// ============================================================================
// ERROR SETTING
// ============================================================================

void error_set(ErrorCode code, const char *message, const char *file, int line, const char *func) {
    ErrorContext *ctx = &tls_error_state.current;
    
    ctx->code = code;
    ctx->file = file;
    ctx->line = line;
    ctx->func = func;
    ctx->timestamp = get_timestamp_ms();
    
    if (message) {
        strncpy(ctx->message, message, MAX_ERROR_MESSAGE - 1);
        ctx->message[MAX_ERROR_MESSAGE - 1] = '\0';
    } else {
        ctx->message[0] = '\0';
    }
    
    tls_error_state.has_error = true;
    
    // Log the error
    LOG_ERROR("[%s] %s at %s:%d (%s)", 
              error_code_name(code), message ? message : "", file, line, func);
}

void error_setf(ErrorCode code, const char *file, int line, const char *func, const char *fmt, ...) {
    char message[MAX_ERROR_MESSAGE];
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, MAX_ERROR_MESSAGE, fmt, args);
    va_end(args);
    
    error_set(code, message, file, line, func);
}

ErrorCode error_get(void) {
    return tls_error_state.current.code;
}

const char* error_get_message(void) {
    return tls_error_state.current.message;
}

ErrorContext* error_get_context(void) {
    return &tls_error_state.current;
}

// ============================================================================
// ERROR CHAINING
// ============================================================================

void error_push(void) {
    if (tls_error_state.chain_depth < MAX_ERROR_CHAIN) {
        tls_error_state.chain[tls_error_state.chain_depth++] = tls_error_state.current;
    }
}

void error_pop(void) {
    if (tls_error_state.chain_depth > 0) {
        tls_error_state.current = tls_error_state.chain[--tls_error_state.chain_depth];
    }
}

// ============================================================================
// ERROR CLEARING
// ============================================================================

void error_clear(void) {
    memset(&tls_error_state, 0, sizeof(ErrorState));
}

bool error_occurred(void) {
    return tls_error_state.has_error;
}

// ============================================================================
// ERROR INFO
// ============================================================================

const char* error_code_name(ErrorCode code) {
    switch (code) {
        case ERR_OK: return "OK";
        
        // System
        case ERR_SYSTEM_GENERIC: return "SYSTEM_ERROR";
        case ERR_SYSTEM_INIT_FAILED: return "INIT_FAILED";
        case ERR_SYSTEM_SHUTDOWN_FAILED: return "SHUTDOWN_FAILED";
        case ERR_SYSTEM_THREAD_FAILED: return "THREAD_FAILED";
        case ERR_SYSTEM_LOCK_FAILED: return "LOCK_FAILED";
        case ERR_SYSTEM_RESOURCE_LIMIT: return "RESOURCE_LIMIT";
        
        // Memory
        case ERR_MEMORY: return "MEMORY_ERROR";
        case ERR_MEMORY_ALLOC_FAILED: return "ALLOC_FAILED";
        case ERR_MEMORY_REALLOC_FAILED: return "REALLOC_FAILED";
        case ERR_MEMORY_QUOTA_EXCEEDED: return "QUOTA_EXCEEDED";
        case ERR_MEMORY_LEAK_DETECTED: return "LEAK_DETECTED";
        case ERR_MEMORY_CORRUPTION: return "CORRUPTION";
        case ERR_MEMORY_DOUBLE_FREE: return "DOUBLE_FREE";
        case ERR_MEMORY_OVERFLOW: return "OVERFLOW";
        
        // I/O
        case ERR_IO: return "IO_ERROR";
        case ERR_IO_FILE_NOT_FOUND: return "FILE_NOT_FOUND";
        case ERR_IO_PERMISSION_DENIED: return "PERMISSION_DENIED";
        case ERR_IO_READ_FAILED: return "READ_FAILED";
        case ERR_IO_WRITE_FAILED: return "WRITE_FAILED";
        case ERR_IO_SEEK_FAILED: return "SEEK_FAILED";
        case ERR_IO_DISK_FULL: return "DISK_FULL";
        case ERR_IO_CORRUPT_FILE: return "CORRUPT_FILE";
        
        // Parse
        case ERR_PARSE: return "PARSE_ERROR";
        case ERR_PARSE_SYNTAX: return "SYNTAX_ERROR";
        case ERR_PARSE_UNEXPECTED_TOKEN: return "UNEXPECTED_TOKEN";
        case ERR_PARSE_UNEXPECTED_EOF: return "UNEXPECTED_EOF";
        case ERR_PARSE_INVALID_KEYWORD: return "INVALID_KEYWORD";
        case ERR_PARSE_INVALID_TYPE: return "INVALID_TYPE";
        case ERR_PARSE_INVALID_LITERAL: return "INVALID_LITERAL";
        
        // Execution
        case ERR_EXEC: return "EXEC_ERROR";
        case ERR_EXEC_TABLE_NOT_FOUND: return "TABLE_NOT_FOUND";
        case ERR_EXEC_COLUMN_NOT_FOUND: return "COLUMN_NOT_FOUND";
        case ERR_EXEC_TABLE_EXISTS: return "TABLE_EXISTS";
        case ERR_EXEC_CONSTRAINT_VIOLATION: return "CONSTRAINT_VIOLATION";
        case ERR_EXEC_TYPE_MISMATCH: return "TYPE_MISMATCH";
        case ERR_EXEC_DIVISION_BY_ZERO: return "DIVISION_BY_ZERO";
        case ERR_EXEC_NULL_VIOLATION: return "NULL_VIOLATION";
        case ERR_EXEC_FK_VIOLATION: return "FK_VIOLATION";
        case ERR_EXEC_PK_VIOLATION: return "PK_VIOLATION";
        case ERR_EXEC_UNIQUE_VIOLATION: return "UNIQUE_VIOLATION";
        case ERR_EXEC_INDEX_NOT_FOUND: return "INDEX_NOT_FOUND";
        case ERR_EXEC_INVALID_OPERATION: return "INVALID_OPERATION";
        
        // Storage
        case ERR_STORAGE: return "STORAGE_ERROR";
        case ERR_STORAGE_PAGE_NOT_FOUND: return "PAGE_NOT_FOUND";
        case ERR_STORAGE_PAGE_CORRUPT: return "PAGE_CORRUPT";
        case ERR_STORAGE_BTREE_CORRUPT: return "BTREE_CORRUPT";
        case ERR_STORAGE_WAL_CORRUPT: return "WAL_CORRUPT";
        case ERR_STORAGE_CHECKPOINT_FAILED: return "CHECKPOINT_FAILED";
        case ERR_STORAGE_RECOVERY_FAILED: return "RECOVERY_FAILED";
        case ERR_STORAGE_KEY_NOT_FOUND: return "KEY_NOT_FOUND";
        case ERR_STORAGE_KEY_EXISTS: return "KEY_EXISTS";
        
        // Transaction
        case ERR_TXN: return "TXN_ERROR";
        case ERR_TXN_NOT_ACTIVE: return "TXN_NOT_ACTIVE";
        case ERR_TXN_ALREADY_ACTIVE: return "TXN_ALREADY_ACTIVE";
        case ERR_TXN_COMMIT_FAILED: return "COMMIT_FAILED";
        case ERR_TXN_ROLLBACK_FAILED: return "ROLLBACK_FAILED";
        case ERR_TXN_DEADLOCK: return "DEADLOCK";
        case ERR_TXN_SERIALIZATION_FAILURE: return "SERIALIZATION_FAILURE";
        case ERR_TXN_LOCK_TIMEOUT: return "LOCK_TIMEOUT";
        case ERR_TXN_SAVEPOINT_NOT_FOUND: return "SAVEPOINT_NOT_FOUND";
        
        // Auth
        case ERR_AUTH: return "AUTH_ERROR";
        case ERR_AUTH_NOT_AUTHENTICATED: return "NOT_AUTHENTICATED";
        case ERR_AUTH_INVALID_CREDENTIALS: return "INVALID_CREDENTIALS";
        case ERR_AUTH_SESSION_EXPIRED: return "SESSION_EXPIRED";
        case ERR_AUTH_PERMISSION_DENIED: return "PERMISSION_DENIED";
        case ERR_AUTH_USER_NOT_FOUND: return "USER_NOT_FOUND";
        case ERR_AUTH_USER_EXISTS: return "USER_EXISTS";
        case ERR_AUTH_ROLE_NOT_FOUND: return "ROLE_NOT_FOUND";
        case ERR_AUTH_INVALID_TOKEN: return "INVALID_TOKEN";
        
        // Cluster
        case ERR_CLUSTER: return "CLUSTER_ERROR";
        case ERR_CLUSTER_NOT_INITIALIZED: return "CLUSTER_NOT_INIT";
        case ERR_CLUSTER_NODE_DOWN: return "NODE_DOWN";
        case ERR_CLUSTER_LEADER_ELECTION: return "LEADER_ELECTION";
        case ERR_CLUSTER_REPLICATION_FAILED: return "REPLICATION_FAILED";
        case ERR_CLUSTER_PARTITION_ERROR: return "PARTITION_ERROR";
        case ERR_CLUSTER_QUORUM_LOST: return "QUORUM_LOST";
        case ERR_CLUSTER_SYNC_FAILED: return "SYNC_FAILED";
        
        // Network
        case ERR_NETWORK: return "NETWORK_ERROR";
        case ERR_NETWORK_CONNECTION_FAILED: return "CONNECTION_FAILED";
        case ERR_NETWORK_TIMEOUT: return "TIMEOUT";
        case ERR_NETWORK_DISCONNECTED: return "DISCONNECTED";
        case ERR_NETWORK_PROTOCOL_ERROR: return "PROTOCOL_ERROR";
        case ERR_NETWORK_SEND_FAILED: return "SEND_FAILED";
        case ERR_NETWORK_RECV_FAILED: return "RECV_FAILED";
        case ERR_NETWORK_DNS_FAILED: return "DNS_FAILED";
        case ERR_NETWORK_SSL_ERROR: return "SSL_ERROR";
        
        // Config
        case ERR_CONFIG: return "CONFIG_ERROR";
        case ERR_CONFIG_FILE_NOT_FOUND: return "CONFIG_NOT_FOUND";
        case ERR_CONFIG_PARSE_ERROR: return "CONFIG_PARSE_ERROR";
        case ERR_CONFIG_INVALID_VALUE: return "CONFIG_INVALID_VALUE";
        case ERR_CONFIG_MISSING_REQUIRED: return "CONFIG_MISSING";
        
        // Special
        case ERR_INVALID_PARAM: return "INVALID_PARAM";
        case ERR_NULL_POINTER: return "NULL_POINTER";
        case ERR_NOT_IMPLEMENTED: return "NOT_IMPLEMENTED";
        case ERR_INTERNAL: return "INTERNAL_ERROR";
        
        default: return "UNKNOWN_ERROR";
    }
}

const char* error_code_name_hinglish(ErrorCode code) {
    switch (code) {
        case ERR_OK: return "THEEK_HAI";
        
        // Memory
        case ERR_MEMORY: return "YAADDASH_GALTI";
        case ERR_MEMORY_ALLOC_FAILED: return "JAGAH_NA_MILI";
        case ERR_MEMORY_QUOTA_EXCEEDED: return "HADD_PAAR";
        case ERR_MEMORY_LEAK_DETECTED: return "RISSAV_MILA";
        
        // Parse
        case ERR_PARSE: return "PADHAI_GALTI";
        case ERR_PARSE_SYNTAX: return "LIKHAWAT_GALTI";
        case ERR_PARSE_UNEXPECTED_TOKEN: return "ACHANAK_LAFZ";
        
        // Execution
        case ERR_EXEC_TABLE_NOT_FOUND: return "TABLE_NA_MILI";
        case ERR_EXEC_COLUMN_NOT_FOUND: return "COLUMN_NA_MILA";
        case ERR_EXEC_CONSTRAINT_VIOLATION: return "PABANDI_TOOTI";
        
        // Transaction
        case ERR_TXN_DEADLOCK: return "TALA_LAGA";
        case ERR_TXN_COMMIT_FAILED: return "PUKKA_NA_HUA";
        case ERR_TXN_ROLLBACK_FAILED: return "WAPSI_NA_HUI";
        
        // Auth
        case ERR_AUTH_NOT_AUTHENTICATED: return "PEHCHAN_NA_HUI";
        case ERR_AUTH_PERMISSION_DENIED: return "IJAZAT_NA_MILI";
        
        // Network
        case ERR_NETWORK_TIMEOUT: return "WAQT_KHATAM";
        case ERR_NETWORK_CONNECTION_FAILED: return "JUDNA_NA_SULA";
        case ERR_NETWORK_DISCONNECTED: return "TUT_GAYA";
        
        default: return error_code_name(code);
    }
}

int error_category(ErrorCode code) {
    return code & 0xFF00;
}

const char* error_category_name(int category) {
    switch (category) {
        case ERR_CAT_NONE:    return "NONE";
        case ERR_CAT_SYSTEM:  return "SYSTEM";
        case ERR_CAT_MEMORY:  return "MEMORY";
        case ERR_CAT_IO:      return "IO";
        case ERR_CAT_PARSE:   return "PARSE";
        case ERR_CAT_EXEC:    return "EXECUTION";
        case ERR_CAT_STORAGE: return "STORAGE";
        case ERR_CAT_TXN:     return "TRANSACTION";
        case ERR_CAT_AUTH:    return "AUTH";
        case ERR_CAT_CLUSTER: return "CLUSTER";
        case ERR_CAT_NETWORK: return "NETWORK";
        case ERR_CAT_CONFIG:  return "CONFIG";
        default:              return "UNKNOWN";
    }
}

// ============================================================================
// ERROR PRINTING
// ============================================================================

void error_print(void) {
    ErrorContext *ctx = &tls_error_state.current;
    
    if (!tls_error_state.has_error) {
        printf("No error\n");
        return;
    }
    
    printf("Error: [%s] %s\n", error_code_name(ctx->code), ctx->message);
    printf("  Location: %s:%d in %s()\n", ctx->file, ctx->line, ctx->func);
    printf("  Category: %s\n", error_category_name(error_category(ctx->code)));
}

void error_print_chain(void) {
    error_print();
    
    if (tls_error_state.chain_depth > 0) {
        printf("Error chain:\n");
        for (int i = tls_error_state.chain_depth - 1; i >= 0; i--) {
            ErrorContext *ctx = &tls_error_state.chain[i];
            printf("  [%d] [%s] %s at %s:%d\n", 
                   i, error_code_name(ctx->code), ctx->message, ctx->file, ctx->line);
        }
    }
}
