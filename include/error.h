/**
 * InventixDB Error Handling Framework
 * 
 * Comprehensive error handling with:
 * - Structured error codes by category
 * - Error context with file/line/function
 * - Error chaining for root cause analysis
 * - Thread-local error state
 * - Bilingual error messages (English + Hinglish)
 * 
 * Usage:
 *   RETURN_ERROR(ERR_MEMORY, "Failed to allocate buffer");
 *   CHECK_NULL(ptr, ERR_MEMORY, "Buffer allocation failed");
 *   CHECK_TRUE(condition, ERR_INVALID_PARAM, "Invalid parameter");
 */

#ifndef INVENTIX_ERROR_H
#define INVENTIX_ERROR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// ERROR CATEGORIES (High byte)
// ============================================================================

#define ERR_CAT_NONE        0x0000
#define ERR_CAT_SYSTEM      0x0100  // OS/system errors
#define ERR_CAT_MEMORY      0x0200  // Memory allocation errors
#define ERR_CAT_IO          0x0300  // I/O errors (file, network)
#define ERR_CAT_PARSE       0x0400  // Parsing/syntax errors
#define ERR_CAT_EXEC        0x0500  // Execution errors
#define ERR_CAT_STORAGE     0x0600  // Storage engine errors
#define ERR_CAT_TXN         0x0700  // Transaction errors
#define ERR_CAT_AUTH        0x0800  // Authentication/authorization errors
#define ERR_CAT_CLUSTER     0x0900  // Distributed system errors
#define ERR_CAT_NETWORK     0x0A00  // Network errors
#define ERR_CAT_CONFIG      0x0B00  // Configuration errors

// ============================================================================
// ERROR CODES
// ============================================================================

typedef enum {
    // Success
    ERR_OK = 0,
    
    // System errors (0x01xx)
    ERR_SYSTEM_GENERIC = ERR_CAT_SYSTEM | 0x01,
    ERR_SYSTEM_INIT_FAILED,
    ERR_SYSTEM_SHUTDOWN_FAILED,
    ERR_SYSTEM_THREAD_FAILED,
    ERR_SYSTEM_LOCK_FAILED,
    ERR_SYSTEM_RESOURCE_LIMIT,
    
    // Memory errors (0x02xx)
    ERR_MEMORY = ERR_CAT_MEMORY | 0x01,
    ERR_MEMORY_ALLOC_FAILED,
    ERR_MEMORY_REALLOC_FAILED,
    ERR_MEMORY_QUOTA_EXCEEDED,
    ERR_MEMORY_LEAK_DETECTED,
    ERR_MEMORY_CORRUPTION,
    ERR_MEMORY_DOUBLE_FREE,
    ERR_MEMORY_OVERFLOW,
    
    // I/O errors (0x03xx)
    ERR_IO = ERR_CAT_IO | 0x01,
    ERR_IO_FILE_NOT_FOUND,
    ERR_IO_PERMISSION_DENIED,
    ERR_IO_READ_FAILED,
    ERR_IO_WRITE_FAILED,
    ERR_IO_SEEK_FAILED,
    ERR_IO_DISK_FULL,
    ERR_IO_CORRUPT_FILE,
    
    // Parse errors (0x04xx)
    ERR_PARSE = ERR_CAT_PARSE | 0x01,
    ERR_PARSE_SYNTAX,
    ERR_PARSE_UNEXPECTED_TOKEN,
    ERR_PARSE_UNEXPECTED_EOF,
    ERR_PARSE_INVALID_KEYWORD,
    ERR_PARSE_INVALID_TYPE,
    ERR_PARSE_INVALID_LITERAL,
    
    // Execution errors (0x05xx)
    ERR_EXEC = ERR_CAT_EXEC | 0x01,
    ERR_EXEC_TABLE_NOT_FOUND,
    ERR_EXEC_COLUMN_NOT_FOUND,
    ERR_EXEC_TABLE_EXISTS,
    ERR_EXEC_CONSTRAINT_VIOLATION,
    ERR_EXEC_TYPE_MISMATCH,
    ERR_EXEC_DIVISION_BY_ZERO,
    ERR_EXEC_NULL_VIOLATION,
    ERR_EXEC_FK_VIOLATION,
    ERR_EXEC_PK_VIOLATION,
    ERR_EXEC_UNIQUE_VIOLATION,
    ERR_EXEC_INDEX_NOT_FOUND,
    ERR_EXEC_INVALID_OPERATION,
    
    // Storage errors (0x06xx)
    ERR_STORAGE = ERR_CAT_STORAGE | 0x01,
    ERR_STORAGE_PAGE_NOT_FOUND,
    ERR_STORAGE_PAGE_CORRUPT,
    ERR_STORAGE_BTREE_CORRUPT,
    ERR_STORAGE_WAL_CORRUPT,
    ERR_STORAGE_CHECKPOINT_FAILED,
    ERR_STORAGE_RECOVERY_FAILED,
    ERR_STORAGE_KEY_NOT_FOUND,
    ERR_STORAGE_KEY_EXISTS,
    
    // Transaction errors (0x07xx)
    ERR_TXN = ERR_CAT_TXN | 0x01,
    ERR_TXN_NOT_ACTIVE,
    ERR_TXN_ALREADY_ACTIVE,
    ERR_TXN_COMMIT_FAILED,
    ERR_TXN_ROLLBACK_FAILED,
    ERR_TXN_DEADLOCK,
    ERR_TXN_SERIALIZATION_FAILURE,
    ERR_TXN_LOCK_TIMEOUT,
    ERR_TXN_SAVEPOINT_NOT_FOUND,
    
    // Auth errors (0x08xx)
    ERR_AUTH = ERR_CAT_AUTH | 0x01,
    ERR_AUTH_NOT_AUTHENTICATED,
    ERR_AUTH_INVALID_CREDENTIALS,
    ERR_AUTH_SESSION_EXPIRED,
    ERR_AUTH_PERMISSION_DENIED,
    ERR_AUTH_USER_NOT_FOUND,
    ERR_AUTH_USER_EXISTS,
    ERR_AUTH_ROLE_NOT_FOUND,
    ERR_AUTH_INVALID_TOKEN,
    
    // Cluster errors (0x09xx)
    ERR_CLUSTER = ERR_CAT_CLUSTER | 0x01,
    ERR_CLUSTER_NOT_INITIALIZED,
    ERR_CLUSTER_NODE_DOWN,
    ERR_CLUSTER_LEADER_ELECTION,
    ERR_CLUSTER_REPLICATION_FAILED,
    ERR_CLUSTER_PARTITION_ERROR,
    ERR_CLUSTER_QUORUM_LOST,
    ERR_CLUSTER_SYNC_FAILED,
    
    // Network errors (0x0Axx)
    ERR_NETWORK = ERR_CAT_NETWORK | 0x01,
    ERR_NETWORK_CONNECTION_FAILED,
    ERR_NETWORK_TIMEOUT,
    ERR_NETWORK_DISCONNECTED,
    ERR_NETWORK_PROTOCOL_ERROR,
    ERR_NETWORK_SEND_FAILED,
    ERR_NETWORK_RECV_FAILED,
    ERR_NETWORK_DNS_FAILED,
    ERR_NETWORK_SSL_ERROR,
    
    // Config errors (0x0Bxx)
    ERR_CONFIG = ERR_CAT_CONFIG | 0x01,
    ERR_CONFIG_FILE_NOT_FOUND,
    ERR_CONFIG_PARSE_ERROR,
    ERR_CONFIG_INVALID_VALUE,
    ERR_CONFIG_MISSING_REQUIRED,
    
    // Special
    ERR_INVALID_PARAM = 0xFF01,
    ERR_NULL_POINTER = 0xFF02,
    ERR_NOT_IMPLEMENTED = 0xFF03,
    ERR_INTERNAL = 0xFFFF
    
} ErrorCode;

// ============================================================================
// ERROR CONTEXT
// ============================================================================

#define MAX_ERROR_MESSAGE 256
#define MAX_ERROR_CHAIN   8

typedef struct ErrorContext {
    ErrorCode code;
    char message[MAX_ERROR_MESSAGE];
    const char *file;
    int line;
    const char *func;
    uint64_t timestamp;
} ErrorContext;

typedef struct ErrorState {
    ErrorContext current;
    ErrorContext chain[MAX_ERROR_CHAIN];
    int chain_depth;
    bool has_error;
} ErrorState;

// ============================================================================
// ERROR HANDLING FUNCTIONS
// ============================================================================

// Initialize error system
void error_init(void);
void error_shutdown(void);

// Set/get current error
void error_set(ErrorCode code, const char *message, const char *file, int line, const char *func);
void error_setf(ErrorCode code, const char *file, int line, const char *func, const char *fmt, ...);
ErrorCode error_get(void);
const char* error_get_message(void);
ErrorContext* error_get_context(void);

// Error chaining
void error_push(void);  // Push current error onto chain
void error_pop(void);   // Restore previous error from chain

// Clear error state
void error_clear(void);

// Check if error occurred
bool error_occurred(void);

// Error info
const char* error_code_name(ErrorCode code);
const char* error_code_name_hinglish(ErrorCode code);
int error_category(ErrorCode code);
const char* error_category_name(int category);

// Print error with context
void error_print(void);
void error_print_chain(void);

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

// Set error and return
#define RETURN_ERROR(code, msg) do { \
    error_set((code), (msg), __FILE__, __LINE__, __func__); \
    return (code); \
} while(0)

#define RETURN_ERROR_F(code, fmt, ...) do { \
    error_setf((code), __FILE__, __LINE__, __func__, (fmt), ##__VA_ARGS__); \
    return (code); \
} while(0)

#define RETURN_NULL_ERROR(code, msg) do { \
    error_set((code), (msg), __FILE__, __LINE__, __func__); \
    return NULL; \
} while(0)

#define RETURN_FALSE_ERROR(code, msg) do { \
    error_set((code), (msg), __FILE__, __LINE__, __func__); \
    return false; \
} while(0)

// Check conditions
#define CHECK_NULL(ptr, code, msg) do { \
    if ((ptr) == NULL) { \
        error_set((code), (msg), __FILE__, __LINE__, __func__); \
        return (code); \
    } \
} while(0)

#define CHECK_NULL_RET_NULL(ptr, code, msg) do { \
    if ((ptr) == NULL) { \
        error_set((code), (msg), __FILE__, __LINE__, __func__); \
        return NULL; \
    } \
} while(0)

#define CHECK_TRUE(cond, code, msg) do { \
    if (!(cond)) { \
        error_set((code), (msg), __FILE__, __LINE__, __func__); \
        return (code); \
    } \
} while(0)

#define CHECK_TRUE_RET_FALSE(cond, code, msg) do { \
    if (!(cond)) { \
        error_set((code), (msg), __FILE__, __LINE__, __func__); \
        return false; \
    } \
} while(0)

// Check and goto cleanup
#define CHECK_NULL_GOTO(ptr, code, msg, label) do { \
    if ((ptr) == NULL) { \
        error_set((code), (msg), __FILE__, __LINE__, __func__); \
        goto label; \
    } \
} while(0)

#define CHECK_TRUE_GOTO(cond, code, msg, label) do { \
    if (!(cond)) { \
        error_set((code), (msg), __FILE__, __LINE__, __func__); \
        goto label; \
    } \
} while(0)

// Propagate error from called function
#define PROPAGATE_ERROR(call) do { \
    ErrorCode _err = (call); \
    if (_err != ERR_OK) { \
        error_push(); \
        return _err; \
    } \
} while(0)

#define PROPAGATE_ERROR_GOTO(call, label) do { \
    ErrorCode _err = (call); \
    if (_err != ERR_OK) { \
        error_push(); \
        goto label; \
    } \
} while(0)

// Memory allocation with error handling
#define ALLOC_OR_FAIL(ptr, size) do { \
    (ptr) = malloc(size); \
    if ((ptr) == NULL) { \
        RETURN_ERROR(ERR_MEMORY_ALLOC_FAILED, "Memory allocation failed / Yaaddash na mili"); \
    } \
} while(0)

#define CALLOC_OR_FAIL(ptr, count, size) do { \
    (ptr) = calloc((count), (size)); \
    if ((ptr) == NULL) { \
        RETURN_ERROR(ERR_MEMORY_ALLOC_FAILED, "Memory allocation failed / Yaaddash na mili"); \
    } \
} while(0)

#ifdef __cplusplus
}
#endif

#endif // INVENTIX_ERROR_H
