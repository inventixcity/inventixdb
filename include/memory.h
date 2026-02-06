/**
 * InventixDB Memory Management System
 * 
 * Features:
 * - Arena/Region allocator for fast bulk allocations
 * - Memory Pool for fixed-size object recycling
 * - Memory quota enforcement per query/session
 * - RAII-style cleanup macros for leak prevention
 * - Memory statistics and debugging
 */

#ifndef INVENTIX_MEMORY_H
#define INVENTIX_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

#define MEM_ARENA_DEFAULT_SIZE      (64 * 1024)      // 64 KB default arena
#define MEM_ARENA_MAX_SIZE          (16 * 1024 * 1024) // 16 MB max arena
#define MEM_POOL_DEFAULT_BLOCKS     256
#define MEM_POOL_MAX_BLOCK_SIZE     4096
#define MEM_QUOTA_DEFAULT           (32 * 1024 * 1024) // 32 MB default quota
#define MEM_ALIGNMENT               16                 // Memory alignment

// ============================================================================
// ARENA ALLOCATOR
// ============================================================================

/**
 * Arena (Region) Allocator
 * 
 * Fast bump allocator that allocates memory linearly.
 * All memory is freed at once when arena is destroyed.
 * Perfect for request-scoped allocations.
 */

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    size_t size;
    size_t used;
    uint8_t data[];  // Flexible array member
} ArenaBlock;

typedef struct Arena {
    ArenaBlock *first;
    ArenaBlock *current;
    size_t total_allocated;
    size_t total_used;
    size_t block_count;
    size_t default_block_size;
    
    // Quota enforcement
    size_t quota;
    bool quota_enabled;
    
    // Statistics
    size_t alloc_count;
    size_t peak_usage;
    
    // Debug info
    const char *name;
    bool track_allocations;
} Arena;

// Arena lifecycle
Arena* arena_create(size_t initial_size);
Arena* arena_create_with_quota(size_t initial_size, size_t quota);
void arena_destroy(Arena *arena);

// Allocation
void* arena_alloc(Arena *arena, size_t size);
void* arena_alloc_zero(Arena *arena, size_t size);
void* arena_alloc_aligned(Arena *arena, size_t size, size_t alignment);
char* arena_strdup(Arena *arena, const char *str);
char* arena_strndup(Arena *arena, const char *str, size_t max_len);
void* arena_realloc(Arena *arena, void *ptr, size_t old_size, size_t new_size);

// Memory management
void arena_reset(Arena *arena);          // Reset for reuse (keep memory)
void arena_clear(Arena *arena);          // Free all blocks except first
size_t arena_get_used(Arena *arena);
size_t arena_get_allocated(Arena *arena);

// Checkpoint/restore for nested scopes
typedef struct {
    ArenaBlock *block;
    size_t used;
} ArenaCheckpoint;

ArenaCheckpoint arena_checkpoint(Arena *arena);
void arena_restore(Arena *arena, ArenaCheckpoint checkpoint);

// ============================================================================
// MEMORY POOL (Fixed-Size Allocator)
// ============================================================================

/**
 * Memory Pool
 * 
 * Efficient allocator for fixed-size objects.
 * Uses free list for O(1) alloc/free.
 * Great for frequently allocated/freed objects.
 */

typedef struct PoolBlock {
    struct PoolBlock *next;
} PoolBlock;

typedef struct MemPool {
    void *memory;
    PoolBlock *free_list;
    size_t block_size;
    size_t block_count;
    size_t used_count;
    size_t total_size;
    
    // Expansion
    struct MemPool *next_pool;
    bool can_expand;
    
    // Statistics
    size_t alloc_count;
    size_t free_count;
    size_t peak_usage;
    
    const char *name;
} MemPool;

// Pool lifecycle
MemPool* pool_create(size_t block_size, size_t block_count);
MemPool* pool_create_named(size_t block_size, size_t block_count, const char *name);
void pool_destroy(MemPool *pool);

// Allocation
void* pool_alloc(MemPool *pool);
void* pool_alloc_zero(MemPool *pool);
void pool_free(MemPool *pool, void *ptr);

// Management
void pool_reset(MemPool *pool);
size_t pool_get_used(MemPool *pool);
size_t pool_get_free(MemPool *pool);
bool pool_is_full(MemPool *pool);
bool pool_owns(MemPool *pool, void *ptr);

// ============================================================================
// MEMORY QUOTA MANAGER
// ============================================================================

/**
 * Memory Quota
 * 
 * Tracks and limits memory usage per query/session.
 * Prevents runaway allocations from consuming all memory.
 */

typedef enum {
    QUOTA_OK = 0,
    QUOTA_EXCEEDED,
    QUOTA_WARNING,       // 80% threshold
    QUOTA_CRITICAL       // 95% threshold
} QuotaStatus;

typedef struct MemQuota {
    size_t limit;
    size_t used;
    size_t peak;
    size_t alloc_count;
    
    // Thresholds
    size_t warning_threshold;   // Default 80%
    size_t critical_threshold;  // Default 95%
    
    // Callbacks
    void (*on_warning)(struct MemQuota *quota, size_t requested);
    void (*on_exceeded)(struct MemQuota *quota, size_t requested);
    
    const char *name;
    void *user_data;
} MemQuota;

MemQuota* quota_create(size_t limit);
MemQuota* quota_create_named(size_t limit, const char *name);
void quota_destroy(MemQuota *quota);

bool quota_request(MemQuota *quota, size_t size);
void quota_release(MemQuota *quota, size_t size);
void quota_reset(MemQuota *quota);

QuotaStatus quota_status(MemQuota *quota);
size_t quota_remaining(MemQuota *quota);
double quota_usage_percent(MemQuota *quota);

// ============================================================================
// RAII-STYLE CLEANUP MACROS
// ============================================================================

/**
 * Cleanup macros for automatic resource management.
 * 
 * Uses GCC/Clang's cleanup attribute or manual defer pattern.
 * Ensures resources are freed even on error paths.
 */

#ifdef __GNUC__
// GCC/Clang: Use cleanup attribute for true RAII

#define CLEANUP(func) __attribute__((cleanup(func)))

// Auto-cleanup pointer types
static inline void _cleanup_free(void *p) {
    void **pp = (void**)p;
    if (*pp) { free(*pp); *pp = NULL; }
}

static inline void _cleanup_arena(Arena **p) {
    if (*p) { arena_destroy(*p); *p = NULL; }
}

static inline void _cleanup_pool(MemPool **p) {
    if (*p) { pool_destroy(*p); *p = NULL; }
}

static inline void _cleanup_quota(MemQuota **p) {
    if (*p) { quota_destroy(*p); *p = NULL; }
}

static inline void _cleanup_file(FILE **p) {
    if (*p) { fclose(*p); *p = NULL; }
}

// Convenience macros
#define AUTO_FREE      CLEANUP(_cleanup_free)
#define AUTO_ARENA     CLEANUP(_cleanup_arena)
#define AUTO_POOL      CLEANUP(_cleanup_pool)
#define AUTO_QUOTA     CLEANUP(_cleanup_quota)
#define AUTO_FILE      CLEANUP(_cleanup_file)

#else
// Fallback for non-GCC compilers - manual cleanup required
#define AUTO_FREE
#define AUTO_ARENA
#define AUTO_POOL
#define AUTO_QUOTA
#define AUTO_FILE
#endif

// ============================================================================
// DEFER MACRO (Go-style defer)
// ============================================================================

/**
 * Defer pattern for cleanup at scope exit.
 * 
 * Usage:
 *   DEFER_START {
 *       void *ptr = malloc(100);
 *       DEFER { free(ptr); };
 *       // Use ptr...
 *   } DEFER_END
 */

#define DEFER_CONCAT_(a, b) a##b
#define DEFER_CONCAT(a, b) DEFER_CONCAT_(a, b)

#ifdef __GNUC__

typedef struct {
    void (*fn)(void*);
    void *arg;
} DeferAction;

static inline void _run_defer(DeferAction *action) {
    if (action->fn) action->fn(action->arg);
}

#define DEFER_START do {
#define DEFER_END } while(0)

#define DEFER_ACTION(cleanup_fn, cleanup_arg) \
    DeferAction DEFER_CONCAT(_defer_, __LINE__) \
        __attribute__((cleanup(_run_defer))) = { \
            .fn = (void(*)(void*))(cleanup_fn), \
            .arg = (void*)(cleanup_arg) \
        }

#else
// Simplified version for other compilers
#define DEFER_START do {
#define DEFER_END } while(0)
#define DEFER_ACTION(fn, arg) /* manual cleanup required */
#endif

// ============================================================================
// SCOPED ALLOCATOR
// ============================================================================

/**
 * Scoped allocator for function-local memory management.
 * All allocations are automatically freed when scope exits.
 */

typedef struct ScopedAlloc {
    Arena *arena;
    ArenaCheckpoint checkpoint;
    struct ScopedAlloc *parent;
} ScopedAlloc;

#define SCOPED_ALLOC(name, size) \
    Arena *DEFER_CONCAT(_scope_arena_, __LINE__) = arena_create(size); \
    ScopedAlloc name = { \
        .arena = DEFER_CONCAT(_scope_arena_, __LINE__), \
        .checkpoint = arena_checkpoint(DEFER_CONCAT(_scope_arena_, __LINE__)) \
    }; \
    DEFER_ACTION(arena_destroy, name.arena)

#define scoped_alloc(scope, size) arena_alloc((scope).arena, size)
#define scoped_strdup(scope, str) arena_strdup((scope).arena, str)

// ============================================================================
// GLOBAL MEMORY TRACKING
// ============================================================================

/**
 * Global memory statistics and tracking.
 * Useful for debugging memory leaks and monitoring usage.
 */

typedef struct MemStats {
    // Global counters
    size_t total_allocated;
    size_t total_freed;
    size_t current_usage;
    size_t peak_usage;
    
    // Allocation counts
    size_t malloc_count;
    size_t free_count;
    size_t realloc_count;
    
    // Arena stats
    size_t arena_count;
    size_t arena_memory;
    
    // Pool stats
    size_t pool_count;
    size_t pool_memory;
    
    // Tracking enabled
    bool tracking_enabled;
} MemStats;

extern MemStats g_mem_stats;

void mem_tracking_enable(bool enable);
void mem_tracking_disable(void);
void mem_stats_reset(void);
void mem_stats_print(void);

// Tracked allocation wrappers
void* mem_alloc(size_t size);
void* mem_alloc_zero(size_t size);
void* mem_realloc(void *ptr, size_t size);
void mem_free(void *ptr);
char* mem_strdup(const char *str);

// ============================================================================
// SESSION MEMORY CONTEXT
// ============================================================================

/**
 * Per-session memory context for server connections.
 * Tracks all memory used by a session with automatic cleanup.
 */

typedef struct SessionMemory {
    Arena *arena;           // Primary arena for session
    MemQuota *quota;        // Memory limit for session
    MemPool *small_pool;    // Pool for small objects (< 64 bytes)
    MemPool *medium_pool;   // Pool for medium objects (< 256 bytes)
    
    // Query-level tracking
    Arena *query_arena;     // Reset per query
    size_t query_count;
    
    // Statistics
    size_t total_allocated;
    size_t queries_executed;
    
    const char *session_id;
} SessionMemory;

SessionMemory* session_memory_create(size_t quota_limit);
void session_memory_destroy(SessionMemory *mem);
void session_memory_reset_query(SessionMemory *mem);

void* session_alloc(SessionMemory *mem, size_t size);
void* session_alloc_query(SessionMemory *mem, size_t size);
char* session_strdup(SessionMemory *mem, const char *str);

// ============================================================================
// UTILITY MACROS
// ============================================================================

// Safe allocation with NULL check
#define MEM_ALLOC(size) mem_alloc(size)
#define MEM_ALLOC_ZERO(size) mem_alloc_zero(size)
// Avoid collision with Windows MEM_FREE
#define INVENTIX_MEM_FREE(ptr) do { mem_free(ptr); (ptr) = NULL; } while(0)

// Array allocation
#define MEM_ALLOC_ARRAY(type, count) \
    ((type*)mem_alloc_zero(sizeof(type) * (count)))

// Struct allocation
#define MEM_NEW(type) ((type*)mem_alloc_zero(sizeof(type)))

// Arena allocation
#define ARENA_NEW(arena, type) \
    ((type*)arena_alloc_zero(arena, sizeof(type)))

#define ARENA_ARRAY(arena, type, count) \
    ((type*)arena_alloc_zero(arena, sizeof(type) * (count)))

// Pool allocation
#define POOL_NEW(pool) pool_alloc_zero(pool)
#define POOL_DELETE(pool, ptr) pool_free(pool, ptr)

// ============================================================================
// HINGLISH ALIASES
// ============================================================================

// Arena
#define smriti_kshetra_banao    arena_create
#define smriti_kshetra_mitao    arena_destroy
#define smriti_ayojit           arena_alloc
#define smriti_saaf             arena_reset

// Pool
#define talab_banao             pool_create
#define talab_mitao             pool_destroy
#define talab_se_lo             pool_alloc
#define talab_mein_dalo         pool_free

// Quota
#define seema_banao             quota_create
#define seema_jaanch            quota_request
#define seema_sthiti            quota_status

#endif // INVENTIX_MEMORY_H
