#ifndef INVENTIX_BUFFER_POOL_H
#define INVENTIX_BUFFER_POOL_H

#include "pager.h"

#define POOL_SIZE 10 // Max pages in RAM

typedef struct {
    Pager *pager;
    void* pages[POOL_SIZE];
    uint32_t page_ids[POOL_SIZE]; // Which page ID corresponds to the slot
    int is_dirty[POOL_SIZE];      // Has it been modified?
    int pin_count[POOL_SIZE];     // Is it currently being used?
    int last_used[POOL_SIZE];     // For LRU eviction
    int clock;                    // Simulating time
} BufferPool;

BufferPool* buffer_pool_init(const char* filename);
void* buffer_pool_get_page(BufferPool* pool, uint32_t page_id);
void buffer_pool_pin(BufferPool* pool, uint32_t page_id);
void buffer_pool_unpin(BufferPool* pool, uint32_t page_id);
void buffer_pool_mark_dirty(BufferPool* pool, uint32_t page_id);
void buffer_pool_flush_all(BufferPool* pool);
void buffer_pool_close(BufferPool* pool);

#endif
