/**
 * InventixDB Safe Memory Allocation Implementation
 * 
 * Advanced memory management with leak detection,
 * guard bytes, and comprehensive tracking.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#define THREAD_LOCAL __declspec(thread)
#else
#include <pthread.h>
#include <sys/time.h>
#define THREAD_LOCAL __thread
#endif

#include "safe_mem.h"
#include "logger.h"

// ============================================================================
// GLOBAL STATE
// ============================================================================

SafeMemContext g_safe_mem = {0};

#ifdef _WIN32
static CRITICAL_SECTION g_safe_mem_lock;
static int g_lock_initialized = 0;
#else
static pthread_mutex_t g_safe_mem_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

// ============================================================================
// LOCK MANAGEMENT
// ============================================================================

static void safe_mem_lock(void) {
#ifdef _WIN32
    if (!g_lock_initialized) {
        InitializeCriticalSection(&g_safe_mem_lock);
        g_lock_initialized = 1;
    }
    EnterCriticalSection(&g_safe_mem_lock);
#else
    pthread_mutex_lock(&g_safe_mem_lock);
#endif
}

static void safe_mem_unlock(void) {
#ifdef _WIN32
    LeaveCriticalSection(&g_safe_mem_lock);
#else
    pthread_mutex_unlock(&g_safe_mem_lock);
#endif
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

static uint64_t get_timestamp_ms(void) {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (t / 10000) - 11644473600000ULL;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

static void set_guard_bytes(void *ptr, size_t size) {
    memset(ptr, SAFE_MEM_GUARD_PATTERN, size);
}

static bool check_guard_bytes(void *ptr, size_t size) {
    unsigned char *bytes = (unsigned char*)ptr;
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != SAFE_MEM_GUARD_PATTERN) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void safe_mem_init(void) {
    safe_mem_lock();
    
    if (g_safe_mem.records != NULL) {
        // Already initialized
        safe_mem_unlock();
        return;
    }
    
    memset(&g_safe_mem, 0, sizeof(SafeMemContext));
    
    g_safe_mem.record_capacity = 1024;  // Start with 1024, grow as needed
    g_safe_mem.records = (AllocationRecord*)calloc(
        g_safe_mem.record_capacity, sizeof(AllocationRecord));
    
    if (!g_safe_mem.records) {
        fprintf(stderr, "FATAL: Failed to initialize safe memory system\n");
        safe_mem_unlock();
        return;
    }
    
    g_safe_mem.tracking_enabled = SAFE_MEM_TRACK_ALLOCATIONS;
    g_safe_mem.debug_enabled = SAFE_MEM_DEBUG;
    g_safe_mem.start_time = get_timestamp_ms();
    g_safe_mem.log_allocations = false;  // Too verbose
    
    safe_mem_unlock();
    
    LOG_INFO("Safe memory system initialized (tracking=%s, debug=%s)",
             g_safe_mem.tracking_enabled ? "ON" : "OFF",
             g_safe_mem.debug_enabled ? "ON" : "OFF");
}

void safe_mem_shutdown(void) {
    // Check for leaks before shutdown
    safe_mem_print_leaks();
    safe_mem_print_stats();
    
    safe_mem_lock();
    
    if (g_safe_mem.records) {
        free(g_safe_mem.records);
        g_safe_mem.records = NULL;
    }
    
    memset(&g_safe_mem, 0, sizeof(SafeMemContext));
    
    safe_mem_unlock();
    
    LOG_INFO("Safe memory system shutdown");
}

// ============================================================================
// ALLOCATION TRACKING
// ============================================================================

static AllocationRecord* find_record(void *ptr) {
    for (uint32_t i = 0; i < g_safe_mem.record_count; i++) {
        if (g_safe_mem.records[i].ptr == ptr && !g_safe_mem.records[i].freed) {
            return &g_safe_mem.records[i];
        }
    }
    return NULL;
}

static AllocationRecord* add_record(void *ptr, void *real_ptr, size_t size, 
                                     size_t real_size, const char *file, 
                                     int line, const char *func) {
    // Grow records array if needed
    if (g_safe_mem.record_count >= g_safe_mem.record_capacity) {
        uint32_t new_capacity = g_safe_mem.record_capacity * 2;
        if (new_capacity > SAFE_MEM_MAX_TRACKED) {
            // Compact by removing freed entries
            uint32_t write_idx = 0;
            for (uint32_t i = 0; i < g_safe_mem.record_count; i++) {
                if (!g_safe_mem.records[i].freed) {
                    if (write_idx != i) {
                        g_safe_mem.records[write_idx] = g_safe_mem.records[i];
                    }
                    write_idx++;
                }
            }
            g_safe_mem.record_count = write_idx;
            
            if (g_safe_mem.record_count >= g_safe_mem.record_capacity) {
                LOG_WARN("Safe memory tracking limit reached (%u allocations)", 
                         SAFE_MEM_MAX_TRACKED);
                return NULL;
            }
        } else {
            AllocationRecord *new_records = (AllocationRecord*)realloc(
                g_safe_mem.records, new_capacity * sizeof(AllocationRecord));
            if (!new_records) {
                LOG_WARN("Failed to grow allocation tracking array");
                return NULL;
            }
            g_safe_mem.records = new_records;
            g_safe_mem.record_capacity = new_capacity;
        }
    }
    
    AllocationRecord *rec = &g_safe_mem.records[g_safe_mem.record_count++];
    rec->ptr = ptr;
    rec->real_ptr = real_ptr;
    rec->size = size;
    rec->real_size = real_size;
    rec->file = file;
    rec->line = line;
    rec->func = func;
    rec->timestamp = get_timestamp_ms() - g_safe_mem.start_time;
    rec->alloc_id = g_safe_mem.next_alloc_id++;
    rec->freed = false;
    
    return rec;
}

// ============================================================================
// CORE ALLOCATION FUNCTIONS
// ============================================================================

void* safe_malloc_impl(size_t size, const char *file, int line, const char *func) {
    if (size == 0) {
        LOG_WARN("safe_malloc called with size 0 at %s:%d", file, line);
        return NULL;
    }
    
    safe_mem_lock();
    
    // Check quota
    if (g_safe_mem.quota_enabled && 
        g_safe_mem.current_bytes + size > g_safe_mem.quota) {
        g_safe_mem.last_error = MEM_ERR_QUOTA_EXCEEDED;
        g_safe_mem.last_error_file = file;
        g_safe_mem.last_error_line = line;
        g_safe_mem.failed_allocations++;
        safe_mem_unlock();
        LOG_ERROR("Memory quota exceeded: requested %zu, current %llu, quota %zu at %s:%d",
                  size, (unsigned long long)g_safe_mem.current_bytes, 
                  g_safe_mem.quota, file, line);
        return NULL;
    }
    
    size_t real_size = size;
    size_t guard_offset = 0;
    
#if SAFE_MEM_DEBUG
    // Add guard bytes before and after
    real_size = SAFE_MEM_GUARD_SIZE + size + SAFE_MEM_GUARD_SIZE;
    guard_offset = SAFE_MEM_GUARD_SIZE;
#endif
    
    void *real_ptr = malloc(real_size);
    if (!real_ptr) {
        g_safe_mem.last_error = MEM_ERR_ALLOCATION_FAILED;
        g_safe_mem.last_error_file = file;
        g_safe_mem.last_error_line = line;
        g_safe_mem.failed_allocations++;
        safe_mem_unlock();
        LOG_ERROR("malloc failed: %zu bytes at %s:%d (%s)", size, file, line, func);
        return NULL;
    }
    
    void *user_ptr = (char*)real_ptr + guard_offset;
    
#if SAFE_MEM_DEBUG
    // Set guard bytes
    set_guard_bytes(real_ptr, SAFE_MEM_GUARD_SIZE);
    set_guard_bytes((char*)user_ptr + size, SAFE_MEM_GUARD_SIZE);
#endif
    
    // Track allocation
    if (g_safe_mem.tracking_enabled) {
        add_record(user_ptr, real_ptr, size, real_size, file, line, func);
    }
    
    // Update statistics
    g_safe_mem.total_allocations++;
    g_safe_mem.total_bytes_allocated += size;
    g_safe_mem.current_bytes += size;
    if (g_safe_mem.current_bytes > g_safe_mem.peak_bytes) {
        g_safe_mem.peak_bytes = g_safe_mem.current_bytes;
    }
    
    if (g_safe_mem.log_allocations) {
        LOG_DEBUG("ALLOC: %p (%zu bytes) at %s:%d (%s)", 
                  user_ptr, size, file, line, func);
    }
    
    safe_mem_unlock();
    return user_ptr;
}

void* safe_calloc_impl(size_t count, size_t size, const char *file, int line, const char *func) {
    size_t total = count * size;
    
    // Check for overflow
    if (count != 0 && total / count != size) {
        LOG_ERROR("calloc overflow: %zu * %zu at %s:%d", count, size, file, line);
        return NULL;
    }
    
    void *ptr = safe_malloc_impl(total, file, line, func);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void* safe_realloc_impl(void *ptr, size_t new_size, const char *file, int line, const char *func) {
    if (!ptr) {
        return safe_malloc_impl(new_size, file, line, func);
    }
    
    if (new_size == 0) {
        safe_free_impl(ptr, file, line, func);
        return NULL;
    }
    
    safe_mem_lock();
    
    AllocationRecord *rec = NULL;
    size_t old_size = 0;
    void *old_real_ptr = NULL;
    
    if (g_safe_mem.tracking_enabled) {
        rec = find_record(ptr);
        if (rec) {
            old_size = rec->size;
            old_real_ptr = rec->real_ptr;
            
#if SAFE_MEM_DEBUG
            // Verify guard bytes
            if (!check_guard_bytes(old_real_ptr, SAFE_MEM_GUARD_SIZE) ||
                !check_guard_bytes((char*)ptr + old_size, SAFE_MEM_GUARD_SIZE)) {
                g_safe_mem.last_error = MEM_ERR_OVERFLOW_DETECTED;
                safe_mem_unlock();
                LOG_ERROR("Buffer overflow detected during realloc at %s:%d", file, line);
                return NULL;
            }
#endif
        }
    }
    
    safe_mem_unlock();
    
    // Allocate new block
    void *new_ptr = safe_malloc_impl(new_size, file, line, func);
    if (!new_ptr) {
        return NULL;
    }
    
    // Copy old data
    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    if (copy_size > 0) {
        memcpy(new_ptr, ptr, copy_size);
    }
    
    // Free old block
    safe_free_impl(ptr, file, line, func);
    
    return new_ptr;
}

void safe_free_impl(void *ptr, const char *file, int line, const char *func) {
    if (!ptr) {
        return;  // free(NULL) is valid
    }
    
    safe_mem_lock();
    
    AllocationRecord *rec = NULL;
    size_t freed_size = 0;
    void *real_ptr = ptr;
    
    if (g_safe_mem.tracking_enabled) {
        rec = find_record(ptr);
        if (rec) {
            if (rec->freed) {
                g_safe_mem.last_error = MEM_ERR_DOUBLE_FREE;
                g_safe_mem.last_error_file = file;
                g_safe_mem.last_error_line = line;
                safe_mem_unlock();
                LOG_ERROR("Double free detected: %p at %s:%d (originally freed elsewhere)",
                          ptr, file, line);
                return;
            }
            
            freed_size = rec->size;
            real_ptr = rec->real_ptr;
            
#if SAFE_MEM_DEBUG
            // Verify guard bytes
            if (!check_guard_bytes(real_ptr, SAFE_MEM_GUARD_SIZE)) {
                LOG_WARN("Buffer underflow detected at %s:%d for allocation from %s:%d",
                         file, line, rec->file, rec->line);
            }
            if (!check_guard_bytes((char*)ptr + rec->size, SAFE_MEM_GUARD_SIZE)) {
                LOG_WARN("Buffer overflow detected at %s:%d for allocation from %s:%d",
                         file, line, rec->file, rec->line);
            }
            
            // Fill with pattern to detect use-after-free
            memset(ptr, SAFE_MEM_FREED_PATTERN, rec->size);
#endif
            
            rec->freed = true;
        } else {
            LOG_WARN("Freeing untracked pointer %p at %s:%d", ptr, file, line);
#if SAFE_MEM_DEBUG
            // Can't determine guard offset, use raw pointer
            real_ptr = ptr;
#endif
        }
    }
    
    // Update statistics
    g_safe_mem.total_frees++;
    g_safe_mem.total_bytes_freed += freed_size;
    g_safe_mem.current_bytes -= freed_size;
    
    if (g_safe_mem.log_allocations) {
        LOG_DEBUG("FREE: %p (%zu bytes) at %s:%d (%s)", 
                  ptr, freed_size, file, line, func);
    }
    
    safe_mem_unlock();
    
    free(real_ptr);
}

char* safe_strdup_impl(const char *str, const char *file, int line, const char *func) {
    if (!str) {
        LOG_WARN("safe_strdup called with NULL at %s:%d", file, line);
        return NULL;
    }
    
    size_t len = strlen(str) + 1;
    char *copy = (char*)safe_malloc_impl(len, file, line, func);
    if (copy) {
        memcpy(copy, str, len);
    }
    return copy;
}

char* safe_strndup_impl(const char *str, size_t max_len, const char *file, int line, const char *func) {
    if (!str) {
        LOG_WARN("safe_strndup called with NULL at %s:%d", file, line);
        return NULL;
    }
    
    size_t len = 0;
    while (len < max_len && str[len] != '\0') {
        len++;
    }
    
    char *copy = (char*)safe_malloc_impl(len + 1, file, line, func);
    if (copy) {
        memcpy(copy, str, len);
        copy[len] = '\0';
    }
    return copy;
}

// ============================================================================
// STATISTICS & DIAGNOSTICS
// ============================================================================

uint64_t safe_mem_current_usage(void) {
    return g_safe_mem.current_bytes;
}

uint64_t safe_mem_peak_usage(void) {
    return g_safe_mem.peak_bytes;
}

uint64_t safe_mem_total_allocated(void) {
    return g_safe_mem.total_bytes_allocated;
}

uint64_t safe_mem_total_freed(void) {
    return g_safe_mem.total_bytes_freed;
}

MemoryStats safe_mem_get_stats(void) {
    MemoryStats stats;
    safe_mem_lock();
    stats.total_allocations = g_safe_mem.total_allocations;
    stats.total_frees = g_safe_mem.total_frees;
    stats.current_bytes = g_safe_mem.current_bytes;
    stats.peak_bytes = g_safe_mem.peak_bytes;
    stats.failed_allocations = g_safe_mem.failed_allocations;
    stats.avg_allocation_size = g_safe_mem.total_allocations > 0 ?
        (double)g_safe_mem.total_bytes_allocated / g_safe_mem.total_allocations : 0;
    safe_mem_unlock();
    return stats;
}

void safe_mem_print_stats(void) {
    MemoryStats stats = safe_mem_get_stats();
    
    LOG_INFO("=== Memory Statistics ===");
    LOG_INFO("  Total allocations:  %llu", (unsigned long long)stats.total_allocations);
    LOG_INFO("  Total frees:        %llu", (unsigned long long)stats.total_frees);
    LOG_INFO("  Current usage:      %llu bytes (%.2f MB)", 
             (unsigned long long)stats.current_bytes,
             stats.current_bytes / (1024.0 * 1024.0));
    LOG_INFO("  Peak usage:         %llu bytes (%.2f MB)",
             (unsigned long long)stats.peak_bytes,
             stats.peak_bytes / (1024.0 * 1024.0));
    LOG_INFO("  Failed allocations: %llu", (unsigned long long)stats.failed_allocations);
    LOG_INFO("  Avg allocation:     %.2f bytes", stats.avg_allocation_size);
}

void safe_mem_reset_stats(void) {
    safe_mem_lock();
    g_safe_mem.total_allocations = 0;
    g_safe_mem.total_frees = 0;
    g_safe_mem.total_bytes_allocated = 0;
    g_safe_mem.total_bytes_freed = 0;
    g_safe_mem.peak_bytes = g_safe_mem.current_bytes;
    g_safe_mem.failed_allocations = 0;
    safe_mem_unlock();
}

// ============================================================================
// LEAK DETECTION
// ============================================================================

LeakReport* safe_mem_check_leaks(void) {
    LeakReport *report = (LeakReport*)calloc(1, sizeof(LeakReport));
    if (!report) return NULL;
    
    safe_mem_lock();
    
    // Count leaks first
    for (uint32_t i = 0; i < g_safe_mem.record_count; i++) {
        if (!g_safe_mem.records[i].freed) {
            report->leak_count++;
            report->leaked_bytes += g_safe_mem.records[i].size;
        }
    }
    
    if (report->leak_count > 0) {
        report->leak_capacity = report->leak_count;
        report->leaks = (AllocationRecord*)calloc(report->leak_count, sizeof(AllocationRecord));
        
        if (report->leaks) {
            uint32_t idx = 0;
            for (uint32_t i = 0; i < g_safe_mem.record_count && idx < report->leak_count; i++) {
                if (!g_safe_mem.records[i].freed) {
                    report->leaks[idx++] = g_safe_mem.records[i];
                }
            }
        }
    }
    
    safe_mem_unlock();
    return report;
}

void safe_mem_free_leak_report(LeakReport *report) {
    if (report) {
        free(report->leaks);
        free(report);
    }
}

void safe_mem_print_leaks(void) {
    LeakReport *report = safe_mem_check_leaks();
    if (!report) return;
    
    if (report->leak_count == 0) {
        LOG_INFO("=== No Memory Leaks Detected ===");
    } else {
        LOG_WARN("=== MEMORY LEAKS DETECTED ===");
        LOG_WARN("  Leaked allocations: %u", report->leak_count);
        LOG_WARN("  Leaked bytes:       %llu (%.2f KB)",
                 (unsigned long long)report->leaked_bytes,
                 report->leaked_bytes / 1024.0);
        
        // Print details of first 20 leaks
        uint32_t show_count = report->leak_count < 20 ? report->leak_count : 20;
        for (uint32_t i = 0; i < show_count; i++) {
            AllocationRecord *rec = &report->leaks[i];
            LOG_WARN("  [%u] %p: %zu bytes at %s:%d (%s)",
                     rec->alloc_id, rec->ptr, rec->size,
                     rec->file, rec->line, rec->func);
        }
        
        if (report->leak_count > 20) {
            LOG_WARN("  ... and %u more leaks", report->leak_count - 20);
        }
    }
    
    safe_mem_free_leak_report(report);
}

// ============================================================================
// GUARD BYTE VERIFICATION
// ============================================================================

bool safe_mem_verify_all(void) {
#if SAFE_MEM_DEBUG
    safe_mem_lock();
    
    bool all_valid = true;
    for (uint32_t i = 0; i < g_safe_mem.record_count; i++) {
        AllocationRecord *rec = &g_safe_mem.records[i];
        if (rec->freed) continue;
        
        // Check front guard
        if (!check_guard_bytes(rec->real_ptr, SAFE_MEM_GUARD_SIZE)) {
            LOG_ERROR("Buffer underflow: allocation #%u at %s:%d (%s)",
                      rec->alloc_id, rec->file, rec->line, rec->func);
            all_valid = false;
        }
        
        // Check back guard
        if (!check_guard_bytes((char*)rec->ptr + rec->size, SAFE_MEM_GUARD_SIZE)) {
            LOG_ERROR("Buffer overflow: allocation #%u at %s:%d (%s)",
                      rec->alloc_id, rec->file, rec->line, rec->func);
            all_valid = false;
        }
    }
    
    safe_mem_unlock();
    return all_valid;
#else
    return true;
#endif
}

bool safe_mem_verify_ptr(void *ptr) {
#if SAFE_MEM_DEBUG
    if (!ptr) return false;
    
    safe_mem_lock();
    AllocationRecord *rec = find_record(ptr);
    bool valid = true;
    
    if (rec) {
        if (!check_guard_bytes(rec->real_ptr, SAFE_MEM_GUARD_SIZE) ||
            !check_guard_bytes((char*)ptr + rec->size, SAFE_MEM_GUARD_SIZE)) {
            valid = false;
        }
    } else {
        valid = false;
    }
    
    safe_mem_unlock();
    return valid;
#else
    return ptr != NULL;
#endif
}

// ============================================================================
// QUOTA MANAGEMENT
// ============================================================================

void safe_mem_set_quota(size_t quota) {
    safe_mem_lock();
    g_safe_mem.quota = quota;
    g_safe_mem.quota_enabled = true;
    safe_mem_unlock();
    LOG_INFO("Memory quota set to %zu bytes (%.2f MB)", quota, quota / (1024.0 * 1024.0));
}

void safe_mem_disable_quota(void) {
    safe_mem_lock();
    g_safe_mem.quota_enabled = false;
    safe_mem_unlock();
}

bool safe_mem_quota_exceeded(void) {
    return g_safe_mem.quota_enabled && 
           g_safe_mem.current_bytes >= g_safe_mem.quota;
}

// ============================================================================
// DEBUG OPTIONS
// ============================================================================

void safe_mem_enable_tracking(bool enable) {
    g_safe_mem.tracking_enabled = enable;
}

void safe_mem_enable_debug(bool enable) {
    g_safe_mem.debug_enabled = enable;
}

void safe_mem_enable_logging(bool enable) {
    g_safe_mem.log_allocations = enable;
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

SafeMemError safe_mem_last_error(void) {
    return g_safe_mem.last_error;
}

const char* safe_mem_error_string(SafeMemError error) {
    switch (error) {
        case MEM_OK:                    return "No error";
        case MEM_ERR_NULL_PTR:          return "Null pointer";
        case MEM_ERR_ALLOCATION_FAILED: return "Allocation failed";
        case MEM_ERR_INVALID_SIZE:      return "Invalid size";
        case MEM_ERR_OVERFLOW_DETECTED: return "Buffer overflow detected";
        case MEM_ERR_DOUBLE_FREE:       return "Double free detected";
        case MEM_ERR_NOT_ALLOCATED:     return "Pointer not allocated";
        case MEM_ERR_QUOTA_EXCEEDED:    return "Memory quota exceeded";
        case MEM_ERR_CORRUPTED:         return "Memory corrupted";
        default:                        return "Unknown error";
    }
}

void safe_mem_clear_error(void) {
    g_safe_mem.last_error = MEM_OK;
    g_safe_mem.last_error_file = NULL;
    g_safe_mem.last_error_line = 0;
}
