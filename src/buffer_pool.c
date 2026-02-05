#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "buffer_pool.h"

BufferPool* buffer_pool_init(const char* filename) {
    BufferPool *pool = malloc(sizeof(BufferPool));
    pool->pager = pager_open(filename);
    pool->clock = 0;
    
    for (int i = 0; i < POOL_SIZE; i++) {
        pool->pages[i] = NULL;
        pool->page_ids[i] = UINT32_MAX; // Empty
        pool->is_dirty[i] = 0;
        pool->pin_count[i] = 0;
        pool->last_used[i] = 0;
    }
    return pool;
}

// Simple LRU replacement policy
int buffer_pool_evict(BufferPool* pool) {
    int lru_idx = -1;
    int min_time = 0x7FFFFFFF;
    
    for (int i = 0; i < POOL_SIZE; i++) {
        if (pool->pin_count[i] == 0) { // Can only evict unpinned pages
            if (pool->last_used[i] < min_time) {
                min_time = pool->last_used[i];
                lru_idx = i;
            }
        }
    }
    
    if (lru_idx == -1) {
        printf("Error: All pages are pinned! Cannot evict.\n");
        return -1;
    }
    
    // Write back if dirty
    if (pool->is_dirty[lru_idx]) {
        pager_write(pool->pager, pool->page_ids[lru_idx], pool->pages[lru_idx]);
    }
    
    if (pool->pages[lru_idx]) {
        free(pool->pages[lru_idx]);
        pool->pages[lru_idx] = NULL;
    }
    
    pool->page_ids[lru_idx] = UINT32_MAX;
    pool->is_dirty[lru_idx] = 0;
    pool->pin_count[lru_idx] = 0;
    
    return lru_idx;
}

void* buffer_pool_get_page(BufferPool* pool, uint32_t page_id) {
    pool->clock++;
    
    // Check if in pool
    for (int i = 0; i < POOL_SIZE; i++) {
        if (pool->page_ids[i] == page_id) {
            pool->last_used[i] = pool->clock;
            pool->pin_count[i]++;
            return pool->pages[i];
        }
    }
    
    // Find empty slot or evict
    int slot = -1;
    for (int i = 0; i < POOL_SIZE; i++) {
        if (pool->page_ids[i] == UINT32_MAX) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        slot = buffer_pool_evict(pool);
        if (slot == -1) return NULL; // Critical failure
    }
    
    void *page_data = malloc(PAGE_SIZE);
    pager_read(pool->pager, page_id, page_data);
    
    pool->pages[slot] = page_data;
    pool->page_ids[slot] = page_id;
    pool->last_used[slot] = pool->clock;
    pool->pin_count[slot] = 1; // Pin it immediately for use
    pool->is_dirty[slot] = 0;
    
    return page_data;
}

void buffer_pool_pin(BufferPool* pool, uint32_t page_id) {
    // Usually implicit in get_page, but explicit here for safety
    for (int i = 0; i < POOL_SIZE; i++) {
        if (pool->page_ids[i] == page_id) {
            pool->pin_count[i]++;
            return;
        }
    }
}

void buffer_pool_unpin(BufferPool* pool, uint32_t page_id) {
    for (int i = 0; i < POOL_SIZE; i++) {
        if (pool->page_ids[i] == page_id) {
            if (pool->pin_count[i] > 0)
                pool->pin_count[i]--;
            return;
        }
    }
}

void buffer_pool_mark_dirty(BufferPool* pool, uint32_t page_id) {
    for (int i = 0; i < POOL_SIZE; i++) {
        if (pool->page_ids[i] == page_id) {
            pool->is_dirty[i] = 1;
            return;
        }
    }
}

void buffer_pool_flush_all(BufferPool* pool) {
    for (int i = 0; i < POOL_SIZE; i++) {
        if (pool->page_ids[i] != UINT32_MAX && pool->is_dirty[i]) {
            pager_write(pool->pager, pool->page_ids[i], pool->pages[i]);
            pool->is_dirty[i] = 0;
        }
    }
}

void buffer_pool_close(BufferPool* pool) {
    buffer_pool_flush_all(pool);
    for (int i = 0; i < POOL_SIZE; i++) {
        if (pool->pages[i]) free(pool->pages[i]);
    }
    pager_close(pool->pager);
    free(pool);
}
