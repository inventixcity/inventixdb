/**
 * InventixDB Safe Memory Allocation System
 * 
 * Advanced memory management with:
 * - Automatic NULL checking after malloc/calloc/realloc
 * - Memory leak detection and tracking
 * - Allocation source tracking (file:line)
 * - Memory usage statistics
 * - Debug mode with guard bytes for overflow detection
 * 
 * Usage:
 *   void *ptr = SAFE_MALLOC(size);       // Logs error and returns NULL on failure
 *   void *ptr = SAFE_CALLOC(n, size);    // Zero-initialized
 *   ptr = SAFE_REALLOC(ptr, new_size);   // Resizes with NULL check
 *   SAFE_FREE(ptr);                      // Sets ptr to NULL after free
 *   char *s = SAFE_STRDUP(str);          // Safe string duplicate
 */

#ifndef INVENTIX_SAFE_MEM_H
#define INVENTIX_SAFE_MEM_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONFIGURATION
// ============================================================================

#define SAFE_MEM_DEBUG              1       // Enable debug mode (guard bytes, tracking)
#define SAFE_MEM_TRACK_ALLOCATIONS  1       // Track all allocations for leak detection
#define SAFE_MEM_GUARD_SIZE         8       // Guard bytes before/after allocation
#define SAFE_MEM_GUARD_PATTERN      0xDE    // Pattern for guard bytes
#define SAFE_MEM_FREED_PATTERN      0xFE    // Pattern to fill freed memory
#define SAFE_MEM_MAX_TRACKED        65536   // Max allocations to track

// ============================================================================
// ERROR CODES
// ============================================================================

typedef enum {
    MEM_OK = 0,
    MEM_ERR_NULL_PTR,
    MEM_ERR_ALLOCATION_FAILED,
    MEM_ERR_INVALID_SIZE,
    MEM_ERR_OVERFLOW_DETECTED,
    MEM_ERR_DOUBLE_FREE,
    MEM_ERR_NOT_ALLOCATED,
    MEM_ERR_QUOTA_EXCEEDED,
    MEM_ERR_CORRUPTED
} SafeMemError;

// ============================================================================
// ALLOCATION TRACKING
// ============================================================================

typedef struct {
    void *ptr;              // Allocated pointer (user-visible)
    void *real_ptr;         // Actual allocation (includes guards)
    size_t size;            // Requested size
    size_t real_size;       // Actual size (includes guards)
    const char *file;       // Source file
    int line;               // Source line
    const char *func;       // Function name
    uint64_t timestamp;     // Allocation time (ms since start)
    uint32_t alloc_id;      // Unique allocation ID
    bool freed;             // Has been freed
} AllocationRecord;

typedef struct {
    // Allocation tracking
    AllocationRecord *records;
    uint32_t record_count;
    uint32_t record_capacity;
    uint32_t next_alloc_id;
    
    // Statistics
    uint64_t total_allocations;
    uint64_t total_frees;
    uint64_t total_bytes_allocated;
    uint64_t total_bytes_freed;
    uint64_t current_bytes;
    uint64_t peak_bytes;
    uint64_t failed_allocations;
    
    // Leak detection
    uint64_t leaked_bytes;
    uint32_t leaked_count;
    
    // Error tracking
    SafeMemError last_error;
    const char *last_error_file;
    int last_error_line;
    
    // Configuration
    bool tracking_enabled;
    bool debug_enabled;
    bool log_allocations;
    size_t quota;
    bool quota_enabled;
    
    // Start time for timestamps
    uint64_t start_time;
    
} SafeMemContext;

// Global context
extern SafeMemContext g_safe_mem;

// ============================================================================
// CORE FUNCTIONS
// ============================================================================

// Initialize/shutdown
void safe_mem_init(void);
void safe_mem_shutdown(void);

// Core allocation functions (use macros instead)
void* safe_malloc_impl(size_t size, const char *file, int line, const char *func);
void* safe_calloc_impl(size_t count, size_t size, const char *file, int line, const char *func);
void* safe_realloc_impl(void *ptr, size_t new_size, const char *file, int line, const char *func);
void  safe_free_impl(void *ptr, const char *file, int line, const char *func);
char* safe_strdup_impl(const char *str, const char *file, int line, const char *func);
char* safe_strndup_impl(const char *str, size_t max_len, const char *file, int line, const char *func);

// ============================================================================
// CONVENIENCE MACROS (USE THESE!)
// ============================================================================

#define SAFE_MALLOC(size) \
    safe_malloc_impl((size), __FILE__, __LINE__, __func__)

#define SAFE_CALLOC(count, size) \
    safe_calloc_impl((count), (size), __FILE__, __LINE__, __func__)

#define SAFE_REALLOC(ptr, new_size) \
    safe_realloc_impl((ptr), (new_size), __FILE__, __LINE__, __func__)

#define SAFE_FREE(ptr) do { \
    safe_free_impl((ptr), __FILE__, __LINE__, __func__); \
    (ptr) = NULL; \
} while(0)

#define SAFE_STRDUP(str) \
    safe_strdup_impl((str), __FILE__, __LINE__, __func__)

#define SAFE_STRNDUP(str, max_len) \
    safe_strndup_impl((str), (max_len), __FILE__, __LINE__, __func__)

// Allocation with error handling
#define SAFE_MALLOC_OR_RETURN(ptr, size, ret_val) do { \
    (ptr) = SAFE_MALLOC(size); \
    if (!(ptr)) { \
        LOG_ERROR("Memory allocation failed: %zu bytes at %s:%d", (size_t)(size), __FILE__, __LINE__); \
        return (ret_val); \
    } \
} while(0)

#define SAFE_CALLOC_OR_RETURN(ptr, count, size, ret_val) do { \
    (ptr) = SAFE_CALLOC((count), (size)); \
    if (!(ptr)) { \
        LOG_ERROR("Memory allocation failed: %zu x %zu bytes at %s:%d", (size_t)(count), (size_t)(size), __FILE__, __LINE__); \
        return (ret_val); \
    } \
} while(0)

#define SAFE_MALLOC_OR_GOTO(ptr, size, label) do { \
    (ptr) = SAFE_MALLOC(size); \
    if (!(ptr)) { \
        LOG_ERROR("Memory allocation failed: %zu bytes at %s:%d", (size_t)(size), __FILE__, __LINE__); \
        goto label; \
    } \
} while(0)

// ============================================================================
// DIAGNOSTICS & LEAK DETECTION
// ============================================================================

// Get current memory usage
uint64_t safe_mem_current_usage(void);
uint64_t safe_mem_peak_usage(void);
uint64_t safe_mem_total_allocated(void);
uint64_t safe_mem_total_freed(void);

// Leak detection
typedef struct {
    uint32_t leak_count;
    uint64_t leaked_bytes;
    AllocationRecord *leaks;  // Array of leaked allocations
    uint32_t leak_capacity;
} LeakReport;

LeakReport* safe_mem_check_leaks(void);
void safe_mem_free_leak_report(LeakReport *report);
void safe_mem_print_leaks(void);

// Memory statistics
typedef struct {
    uint64_t total_allocations;
    uint64_t total_frees;
    uint64_t current_bytes;
    uint64_t peak_bytes;
    uint64_t failed_allocations;
    double avg_allocation_size;
} MemoryStats;

MemoryStats safe_mem_get_stats(void);
void safe_mem_print_stats(void);
void safe_mem_reset_stats(void);

// Guard byte verification
bool safe_mem_verify_all(void);
bool safe_mem_verify_ptr(void *ptr);

// Quota management
void safe_mem_set_quota(size_t quota);
void safe_mem_disable_quota(void);
bool safe_mem_quota_exceeded(void);

// Debug options
void safe_mem_enable_tracking(bool enable);
void safe_mem_enable_debug(bool enable);
void safe_mem_enable_logging(bool enable);

// ============================================================================
// ERROR HANDLING
// ============================================================================

SafeMemError safe_mem_last_error(void);
const char* safe_mem_error_string(SafeMemError error);
void safe_mem_clear_error(void);

#ifdef __cplusplus
}
#endif

#endif // INVENTIX_SAFE_MEM_H
