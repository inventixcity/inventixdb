/**
 * InventixDB Memory Management Implementation
 * 
 * Arena allocator, memory pools, quota management, and tracking.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "memory.h"
#include "logger.h"

// ============================================================================
// GLOBAL STATE
// ============================================================================

MemStats g_mem_stats = {0};

#ifdef _WIN32
static CRITICAL_SECTION g_mem_lock;
static int g_lock_initialized = 0;
#else
static pthread_mutex_t g_mem_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static void mem_lock_init(void) {
#ifdef _WIN32
    if (!g_lock_initialized) {
        InitializeCriticalSection(&g_mem_lock);
        g_lock_initialized = 1;
    }
#endif
}

static void mem_lock(void) {
#ifdef _WIN32
    EnterCriticalSection(&g_mem_lock);
#else
    pthread_mutex_lock(&g_mem_lock);
#endif
}

static void mem_unlock(void) {
#ifdef _WIN32
    LeaveCriticalSection(&g_mem_lock);
#else
    pthread_mutex_unlock(&g_mem_lock);
#endif
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

static size_t align_up(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

// ============================================================================
// ARENA ALLOCATOR IMPLEMENTATION
// ============================================================================

static ArenaBlock* arena_block_create(size_t size) {
    size_t total = sizeof(ArenaBlock) + size;
    ArenaBlock *block = (ArenaBlock*)malloc(total);
    if (!block) return NULL;
    
    block->next = NULL;
    block->size = size;
    block->used = 0;
    
    if (g_mem_stats.tracking_enabled) {
        mem_lock();
        g_mem_stats.total_allocated += total;
        g_mem_stats.current_usage += total;
        g_mem_stats.malloc_count++;
        if (g_mem_stats.current_usage > g_mem_stats.peak_usage) {
            g_mem_stats.peak_usage = g_mem_stats.current_usage;
        }
        g_mem_stats.arena_memory += total;
        mem_unlock();
    }
    
    return block;
}

static void arena_block_destroy(ArenaBlock *block) {
    if (!block) return;
    
    if (g_mem_stats.tracking_enabled) {
        size_t total = sizeof(ArenaBlock) + block->size;
        mem_lock();
        g_mem_stats.total_freed += total;
        g_mem_stats.current_usage -= total;
        g_mem_stats.free_count++;
        g_mem_stats.arena_memory -= total;
        mem_unlock();
    }
    
    free(block);
}

Arena* arena_create(size_t initial_size) {
    return arena_create_with_quota(initial_size, 0);
}

Arena* arena_create_with_quota(size_t initial_size, size_t quota) {
    mem_lock_init();
    
    if (initial_size == 0) initial_size = MEM_ARENA_DEFAULT_SIZE;
    if (initial_size > MEM_ARENA_MAX_SIZE) initial_size = MEM_ARENA_MAX_SIZE;
    
    Arena *arena = (Arena*)calloc(1, sizeof(Arena));
    if (!arena) return NULL;
    
    ArenaBlock *block = arena_block_create(initial_size);
    if (!block) {
        free(arena);
        return NULL;
    }
    
    arena->first = block;
    arena->current = block;
    arena->total_allocated = initial_size;
    arena->default_block_size = initial_size;
    arena->block_count = 1;
    
    if (quota > 0) {
        arena->quota = quota;
        arena->quota_enabled = true;
    }
    
    if (g_mem_stats.tracking_enabled) {
        mem_lock();
        g_mem_stats.arena_count++;
        mem_unlock();
    }
    
    return arena;
}

void arena_destroy(Arena *arena) {
    if (!arena) return;
    
    ArenaBlock *block = arena->first;
    while (block) {
        ArenaBlock *next = block->next;
        arena_block_destroy(block);
        block = next;
    }
    
    if (g_mem_stats.tracking_enabled) {
        mem_lock();
        g_mem_stats.arena_count--;
        mem_unlock();
    }
    
    free(arena);
}

void* arena_alloc(Arena *arena, size_t size) {
    if (!arena || size == 0) return NULL;
    
    size = align_up(size, MEM_ALIGNMENT);
    
    // Check quota
    if (arena->quota_enabled && 
        arena->total_used + size > arena->quota) {
        LOG_WARN("Arena quota exceeded: %zu + %zu > %zu",
                 arena->total_used, size, arena->quota);
        return NULL;
    }
    
    // Try current block
    ArenaBlock *block = arena->current;
    if (block->used + size <= block->size) {
        void *ptr = block->data + block->used;
        block->used += size;
        arena->total_used += size;
        arena->alloc_count++;
        
        if (arena->total_used > arena->peak_usage) {
            arena->peak_usage = arena->total_used;
        }
        
        return ptr;
    }
    
    // Need new block
    size_t new_size = arena->default_block_size;
    if (size > new_size) new_size = size * 2;
    
    ArenaBlock *new_block = arena_block_create(new_size);
    if (!new_block) return NULL;
    
    block->next = new_block;
    arena->current = new_block;
    arena->total_allocated += new_size;
    arena->block_count++;
    
    void *ptr = new_block->data;
    new_block->used = size;
    arena->total_used += size;
    arena->alloc_count++;
    
    if (arena->total_used > arena->peak_usage) {
        arena->peak_usage = arena->total_used;
    }
    
    return ptr;
}

void* arena_alloc_zero(Arena *arena, size_t size) {
    void *ptr = arena_alloc(arena, size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

void* arena_alloc_aligned(Arena *arena, size_t size, size_t alignment) {
    if (!arena || size == 0) return NULL;
    
    // Ensure alignment is power of 2
    if (alignment & (alignment - 1)) {
        alignment = MEM_ALIGNMENT;
    }
    
    size_t total = size + alignment;
    void *ptr = arena_alloc(arena, total);
    if (!ptr) return NULL;
    
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    
    return (void*)aligned;
}

char* arena_strdup(Arena *arena, const char *str) {
    if (!arena || !str) return NULL;
    
    size_t len = strlen(str) + 1;
    char *copy = (char*)arena_alloc(arena, len);
    if (copy) memcpy(copy, str, len);
    return copy;
}

char* arena_strndup(Arena *arena, const char *str, size_t max_len) {
    if (!arena || !str) return NULL;
    
    size_t len = strlen(str);
    if (len > max_len) len = max_len;
    
    char *copy = (char*)arena_alloc(arena, len + 1);
    if (copy) {
        memcpy(copy, str, len);
        copy[len] = '\0';
    }
    return copy;
}

void* arena_realloc(Arena *arena, void *ptr, size_t old_size, size_t new_size) {
    if (!arena) return NULL;
    if (!ptr) return arena_alloc(arena, new_size);
    if (new_size == 0) return NULL;
    
    // Arena doesn't support true realloc, so allocate new and copy
    void *new_ptr = arena_alloc(arena, new_size);
    if (new_ptr && ptr) {
        memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
    }
    return new_ptr;
}

void arena_reset(Arena *arena) {
    if (!arena) return;
    
    // Reset all blocks
    ArenaBlock *block = arena->first;
    while (block) {
        block->used = 0;
        block = block->next;
    }
    
    arena->current = arena->first;
    arena->total_used = 0;
    arena->alloc_count = 0;
}

void arena_clear(Arena *arena) {
    if (!arena || !arena->first) return;
    
    // Free all blocks except first
    ArenaBlock *block = arena->first->next;
    while (block) {
        ArenaBlock *next = block->next;
        arena->total_allocated -= block->size;
        arena->block_count--;
        arena_block_destroy(block);
        block = next;
    }
    
    arena->first->next = NULL;
    arena->first->used = 0;
    arena->current = arena->first;
    arena->total_used = 0;
}

size_t arena_get_used(Arena *arena) {
    return arena ? arena->total_used : 0;
}

size_t arena_get_allocated(Arena *arena) {
    return arena ? arena->total_allocated : 0;
}

ArenaCheckpoint arena_checkpoint(Arena *arena) {
    ArenaCheckpoint cp = {0};
    if (arena) {
        cp.block = arena->current;
        cp.used = arena->current ? arena->current->used : 0;
    }
    return cp;
}

void arena_restore(Arena *arena, ArenaCheckpoint checkpoint) {
    if (!arena || !checkpoint.block) return;
    
    // Find and reset to checkpoint
    ArenaBlock *block = checkpoint.block;
    
    // Free blocks after checkpoint
    ArenaBlock *next = block->next;
    while (next) {
        ArenaBlock *to_free = next;
        next = next->next;
        arena->total_allocated -= to_free->size;
        arena->block_count--;
        arena_block_destroy(to_free);
    }
    
    block->next = NULL;
    block->used = checkpoint.used;
    arena->current = block;
    
    // Recalculate total_used
    arena->total_used = 0;
    block = arena->first;
    while (block) {
        arena->total_used += block->used;
        block = block->next;
    }
}

// ============================================================================
// MEMORY POOL IMPLEMENTATION
// ============================================================================

MemPool* pool_create(size_t block_size, size_t block_count) {
    return pool_create_named(block_size, block_count, NULL);
}

MemPool* pool_create_named(size_t block_size, size_t block_count, const char *name) {
    mem_lock_init();
    
    if (block_size < sizeof(PoolBlock)) {
        block_size = sizeof(PoolBlock);
    }
    block_size = align_up(block_size, MEM_ALIGNMENT);
    
    if (block_count == 0) block_count = MEM_POOL_DEFAULT_BLOCKS;
    
    MemPool *pool = (MemPool*)calloc(1, sizeof(MemPool));
    if (!pool) return NULL;
    
    size_t total = block_size * block_count;
    pool->memory = malloc(total);
    if (!pool->memory) {
        free(pool);
        return NULL;
    }
    
    pool->block_size = block_size;
    pool->block_count = block_count;
    pool->total_size = total;
    pool->name = name;
    pool->can_expand = true;
    
    // Build free list
    pool->free_list = NULL;
    uint8_t *ptr = (uint8_t*)pool->memory;
    for (size_t i = 0; i < block_count; i++) {
        PoolBlock *block = (PoolBlock*)(ptr + i * block_size);
        block->next = pool->free_list;
        pool->free_list = block;
    }
    
    if (g_mem_stats.tracking_enabled) {
        mem_lock();
        g_mem_stats.total_allocated += total + sizeof(MemPool);
        g_mem_stats.current_usage += total + sizeof(MemPool);
        g_mem_stats.malloc_count++;
        g_mem_stats.pool_count++;
        g_mem_stats.pool_memory += total;
        if (g_mem_stats.current_usage > g_mem_stats.peak_usage) {
            g_mem_stats.peak_usage = g_mem_stats.current_usage;
        }
        mem_unlock();
    }
    
    return pool;
}

void pool_destroy(MemPool *pool) {
    if (!pool) return;
    
    // Destroy expansion pools first
    MemPool *next = pool->next_pool;
    while (next) {
        MemPool *to_free = next;
        next = next->next_pool;
        
        if (g_mem_stats.tracking_enabled) {
            mem_lock();
            g_mem_stats.total_freed += to_free->total_size + sizeof(MemPool);
            g_mem_stats.current_usage -= to_free->total_size + sizeof(MemPool);
            g_mem_stats.pool_memory -= to_free->total_size;
            g_mem_stats.pool_count--;
            mem_unlock();
        }
        
        free(to_free->memory);
        free(to_free);
    }
    
    if (g_mem_stats.tracking_enabled) {
        mem_lock();
        g_mem_stats.total_freed += pool->total_size + sizeof(MemPool);
        g_mem_stats.current_usage -= pool->total_size + sizeof(MemPool);
        g_mem_stats.pool_memory -= pool->total_size;
        g_mem_stats.pool_count--;
        mem_unlock();
    }
    
    free(pool->memory);
    free(pool);
}

void* pool_alloc(MemPool *pool) {
    if (!pool) return NULL;
    
    // Try to get from free list
    if (pool->free_list) {
        PoolBlock *block = pool->free_list;
        pool->free_list = block->next;
        pool->used_count++;
        pool->alloc_count++;
        
        if (pool->used_count > pool->peak_usage) {
            pool->peak_usage = pool->used_count;
        }
        
        return block;
    }
    
    // Try expansion pools
    MemPool *expansion = pool->next_pool;
    while (expansion) {
        if (expansion->free_list) {
            PoolBlock *block = expansion->free_list;
            expansion->free_list = block->next;
            expansion->used_count++;
            pool->alloc_count++;
            return block;
        }
        expansion = expansion->next_pool;
    }
    
    // Need to expand
    if (!pool->can_expand) return NULL;
    
    MemPool *new_pool = pool_create_named(pool->block_size, pool->block_count, pool->name);
    if (!new_pool) return NULL;
    
    // Link to expansion chain
    new_pool->next_pool = pool->next_pool;
    pool->next_pool = new_pool;
    new_pool->can_expand = false;  // Only root pool can expand
    
    return pool_alloc(pool);
}

void* pool_alloc_zero(MemPool *pool) {
    void *ptr = pool_alloc(pool);
    if (ptr) memset(ptr, 0, pool->block_size);
    return ptr;
}

void pool_free(MemPool *pool, void *ptr) {
    if (!pool || !ptr) return;
    
    // Find which pool owns this pointer
    MemPool *owner = pool;
    while (owner) {
        if (pool_owns(owner, ptr)) {
            PoolBlock *block = (PoolBlock*)ptr;
            block->next = owner->free_list;
            owner->free_list = block;
            owner->used_count--;
            owner->free_count++;
            return;
        }
        owner = owner->next_pool;
    }
    
    // Pointer not from this pool - log warning
    LOG_WARN("pool_free: pointer %p not from pool %s", ptr, pool->name ? pool->name : "unnamed");
}

void pool_reset(MemPool *pool) {
    if (!pool) return;
    
    // Rebuild free list
    pool->free_list = NULL;
    uint8_t *ptr = (uint8_t*)pool->memory;
    for (size_t i = 0; i < pool->block_count; i++) {
        PoolBlock *block = (PoolBlock*)(ptr + i * pool->block_size);
        block->next = pool->free_list;
        pool->free_list = block;
    }
    pool->used_count = 0;
    
    // Reset expansion pools
    MemPool *expansion = pool->next_pool;
    while (expansion) {
        pool_reset(expansion);
        expansion = expansion->next_pool;
    }
}

size_t pool_get_used(MemPool *pool) {
    if (!pool) return 0;
    
    size_t total = pool->used_count;
    MemPool *expansion = pool->next_pool;
    while (expansion) {
        total += expansion->used_count;
        expansion = expansion->next_pool;
    }
    return total;
}

size_t pool_get_free(MemPool *pool) {
    if (!pool) return 0;
    
    size_t total = pool->block_count - pool->used_count;
    MemPool *expansion = pool->next_pool;
    while (expansion) {
        total += expansion->block_count - expansion->used_count;
        expansion = expansion->next_pool;
    }
    return total;
}

bool pool_is_full(MemPool *pool) {
    return pool && !pool->free_list && !pool->can_expand;
}

bool pool_owns(MemPool *pool, void *ptr) {
    if (!pool || !ptr) return false;
    
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t start = (uintptr_t)pool->memory;
    uintptr_t end = start + pool->total_size;
    
    return p >= start && p < end;
}

// ============================================================================
// MEMORY QUOTA IMPLEMENTATION
// ============================================================================

MemQuota* quota_create(size_t limit) {
    return quota_create_named(limit, NULL);
}

MemQuota* quota_create_named(size_t limit, const char *name) {
    MemQuota *quota = (MemQuota*)calloc(1, sizeof(MemQuota));
    if (!quota) return NULL;
    
    quota->limit = limit > 0 ? limit : MEM_QUOTA_DEFAULT;
    quota->warning_threshold = (size_t)(quota->limit * 0.8);
    quota->critical_threshold = (size_t)(quota->limit * 0.95);
    quota->name = name;
    
    return quota;
}

void quota_destroy(MemQuota *quota) {
    free(quota);
}

bool quota_request(MemQuota *quota, size_t size) {
    if (!quota) return true;  // No quota = allow
    
    if (quota->used + size > quota->limit) {
        if (quota->on_exceeded) {
            quota->on_exceeded(quota, size);
        }
        LOG_WARN("Quota exceeded: %s requested %zu, limit %zu, used %zu",
                 quota->name ? quota->name : "unnamed", size, quota->limit, quota->used);
        return false;
    }
    
    quota->used += size;
    quota->alloc_count++;
    
    if (quota->used > quota->peak) {
        quota->peak = quota->used;
    }
    
    // Check thresholds
    if (quota->used >= quota->critical_threshold) {
        LOG_WARN("Quota critical: %s at %.1f%%", 
                 quota->name ? quota->name : "unnamed",
                 quota_usage_percent(quota));
    } else if (quota->used >= quota->warning_threshold) {
        if (quota->on_warning) {
            quota->on_warning(quota, size);
        }
    }
    
    return true;
}

void quota_release(MemQuota *quota, size_t size) {
    if (!quota) return;
    
    if (size > quota->used) {
        quota->used = 0;
    } else {
        quota->used -= size;
    }
}

void quota_reset(MemQuota *quota) {
    if (!quota) return;
    quota->used = 0;
    quota->alloc_count = 0;
}

QuotaStatus quota_status(MemQuota *quota) {
    if (!quota) return QUOTA_OK;
    
    if (quota->used >= quota->limit) return QUOTA_EXCEEDED;
    if (quota->used >= quota->critical_threshold) return QUOTA_CRITICAL;
    if (quota->used >= quota->warning_threshold) return QUOTA_WARNING;
    return QUOTA_OK;
}

size_t quota_remaining(MemQuota *quota) {
    if (!quota) return SIZE_MAX;
    return quota->limit > quota->used ? quota->limit - quota->used : 0;
}

double quota_usage_percent(MemQuota *quota) {
    if (!quota || quota->limit == 0) return 0.0;
    return (double)quota->used / quota->limit * 100.0;
}

// ============================================================================
// GLOBAL MEMORY TRACKING
// ============================================================================

void mem_tracking_enable(bool enable) {
    mem_lock_init();
    g_mem_stats.tracking_enabled = enable;
}

void mem_tracking_disable(void) {
    g_mem_stats.tracking_enabled = false;
}

void mem_stats_reset(void) {
    mem_lock();
    memset(&g_mem_stats, 0, sizeof(g_mem_stats));
    mem_unlock();
}

void mem_stats_print(void) {
    printf("\n=== Memory Statistics ===\n");
    printf("Total Allocated: %zu bytes\n", g_mem_stats.total_allocated);
    printf("Total Freed:     %zu bytes\n", g_mem_stats.total_freed);
    printf("Current Usage:   %zu bytes\n", g_mem_stats.current_usage);
    printf("Peak Usage:      %zu bytes\n", g_mem_stats.peak_usage);
    printf("Malloc Count:    %zu\n", g_mem_stats.malloc_count);
    printf("Free Count:      %zu\n", g_mem_stats.free_count);
    printf("Arena Count:     %zu (memory: %zu)\n", g_mem_stats.arena_count, g_mem_stats.arena_memory);
    printf("Pool Count:      %zu (memory: %zu)\n", g_mem_stats.pool_count, g_mem_stats.pool_memory);
    printf("=========================\n\n");
}

void* mem_alloc(size_t size) {
    void *ptr = malloc(size);
    
    if (g_mem_stats.tracking_enabled && ptr) {
        mem_lock();
        g_mem_stats.total_allocated += size;
        g_mem_stats.current_usage += size;
        g_mem_stats.malloc_count++;
        if (g_mem_stats.current_usage > g_mem_stats.peak_usage) {
            g_mem_stats.peak_usage = g_mem_stats.current_usage;
        }
        mem_unlock();
    }
    
    return ptr;
}

void* mem_alloc_zero(size_t size) {
    void *ptr = calloc(1, size);
    
    if (g_mem_stats.tracking_enabled && ptr) {
        mem_lock();
        g_mem_stats.total_allocated += size;
        g_mem_stats.current_usage += size;
        g_mem_stats.malloc_count++;
        if (g_mem_stats.current_usage > g_mem_stats.peak_usage) {
            g_mem_stats.peak_usage = g_mem_stats.current_usage;
        }
        mem_unlock();
    }
    
    return ptr;
}

void* mem_realloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    
    if (g_mem_stats.tracking_enabled) {
        mem_lock();
        g_mem_stats.realloc_count++;
        mem_unlock();
    }
    
    return new_ptr;
}

void mem_free(void *ptr) {
    if (!ptr) return;
    
    // Note: We can't track exact size freed without additional metadata
    if (g_mem_stats.tracking_enabled) {
        mem_lock();
        g_mem_stats.free_count++;
        mem_unlock();
    }
    
    free(ptr);
}

char* mem_strdup(const char *str) {
    if (!str) return NULL;
    
    size_t len = strlen(str) + 1;
    char *copy = (char*)mem_alloc(len);
    if (copy) memcpy(copy, str, len);
    return copy;
}

// ============================================================================
// SESSION MEMORY CONTEXT
// ============================================================================

SessionMemory* session_memory_create(size_t quota_limit) {
    SessionMemory *mem = (SessionMemory*)calloc(1, sizeof(SessionMemory));
    if (!mem) return NULL;
    
    // Create quota
    if (quota_limit > 0) {
        mem->quota = quota_create_named(quota_limit, "session");
        if (!mem->quota) {
            free(mem);
            return NULL;
        }
    }
    
    // Create main arena
    mem->arena = arena_create(MEM_ARENA_DEFAULT_SIZE);
    if (!mem->arena) {
        quota_destroy(mem->quota);
        free(mem);
        return NULL;
    }
    
    // Create query arena
    mem->query_arena = arena_create(16 * 1024);  // 16 KB for queries
    if (!mem->query_arena) {
        arena_destroy(mem->arena);
        quota_destroy(mem->quota);
        free(mem);
        return NULL;
    }
    
    // Create pools for small/medium objects
    mem->small_pool = pool_create_named(64, 256, "small");
    mem->medium_pool = pool_create_named(256, 64, "medium");
    
    return mem;
}

void session_memory_destroy(SessionMemory *mem) {
    if (!mem) return;
    
    arena_destroy(mem->arena);
    arena_destroy(mem->query_arena);
    pool_destroy(mem->small_pool);
    pool_destroy(mem->medium_pool);
    quota_destroy(mem->quota);
    free(mem);
}

void session_memory_reset_query(SessionMemory *mem) {
    if (!mem) return;
    
    arena_reset(mem->query_arena);
    mem->query_count++;
}

void* session_alloc(SessionMemory *mem, size_t size) {
    if (!mem) return NULL;
    
    // Check quota
    if (mem->quota && !quota_request(mem->quota, size)) {
        return NULL;
    }
    
    // Use pool for small allocations
    if (size <= 64 && mem->small_pool) {
        return pool_alloc(mem->small_pool);
    }
    if (size <= 256 && mem->medium_pool) {
        return pool_alloc(mem->medium_pool);
    }
    
    // Use arena for larger allocations
    void *ptr = arena_alloc(mem->arena, size);
    if (ptr) mem->total_allocated += size;
    return ptr;
}

void* session_alloc_query(SessionMemory *mem, size_t size) {
    if (!mem) return NULL;
    
    if (mem->quota && !quota_request(mem->quota, size)) {
        return NULL;
    }
    
    return arena_alloc(mem->query_arena, size);
}

char* session_strdup(SessionMemory *mem, const char *str) {
    if (!mem || !str) return NULL;
    return arena_strdup(mem->arena, str);
}
