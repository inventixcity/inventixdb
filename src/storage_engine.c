/**
 * InventixDB Advanced Storage Engine Implementation
 * 
 * Implements:
 * 1. Slotted Pages with variable-length records
 * 2. LZ4-style page compression
 * 3. Columnar storage for analytics
 * 4. LSM Tree for write-heavy workloads
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "storage_engine.h"
#include "logger.h"

// -----------------------------------------------------------------------------
// CRC32 Checksum (for page integrity)
// -----------------------------------------------------------------------------

static uint32_t crc32_table[256];
static bool crc32_initialized = false;

static void init_crc32_table(void) {
    if (crc32_initialized) return;
    
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = true;
}

static uint32_t compute_crc32(const uint8_t *data, size_t len) {
    init_crc32_table();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

// -----------------------------------------------------------------------------
// Slotted Page Implementation
// -----------------------------------------------------------------------------

void slotted_page_init(SlottedPage *page, uint32_t page_id) {
    memset(page, 0, sizeof(SlottedPage));
    page->header.page_id = page_id;
    page->header.slot_count = 0;
    page->header.free_space_start = sizeof(SlottedPageHeader);
    page->header.free_space_end = PAGE_SIZE_SE;
    page->header.flags = PAGE_FLAG_LEAF;
    page->header.checksum = 0;
}

size_t slotted_page_free_space(SlottedPage *page) {
    size_t slot_dir_size = page->header.slot_count * sizeof(SlotEntry);
    size_t header_size = sizeof(SlottedPageHeader) + slot_dir_size;
    
    if (page->header.free_space_end <= header_size) {
        return 0;
    }
    return page->header.free_space_end - header_size - sizeof(SlotEntry);
}

int slotted_page_insert(SlottedPage *page, const uint8_t *data, size_t len, uint16_t *out_slot) {
    if (!page || !data || len == 0) return -1;
    
    // Check if we have enough space
    size_t needed = len + sizeof(SlotEntry);
    if (slotted_page_free_space(page) < needed) {
        // Try compaction first
        slotted_page_compact(page);
        if (slotted_page_free_space(page) < needed) {
            return -1;  // Page full
        }
    }
    
    // Find free slot or allocate new one
    uint16_t slot_idx = page->header.slot_count;
    
    // Check for deleted slots we can reuse
    for (uint16_t i = 0; i < page->header.slot_count; i++) {
        if (page->slots[i].length == 0) {
            slot_idx = i;
            break;
        }
    }
    
    if (slot_idx >= MAX_SLOT_COUNT) {
        return -1;  // Too many slots
    }
    
    // Calculate record position (grows from end of page)
    uint16_t record_offset = page->header.free_space_end - len;
    
    // Copy data
    memcpy((uint8_t*)page + record_offset, data, len);
    
    // Update slot
    page->slots[slot_idx].offset = record_offset;
    page->slots[slot_idx].length = len;
    
    // Update header
    if (slot_idx >= page->header.slot_count) {
        page->header.slot_count = slot_idx + 1;
        page->header.free_space_start = sizeof(SlottedPageHeader) + 
                                         page->header.slot_count * sizeof(SlotEntry);
    }
    page->header.free_space_end = record_offset;
    
    if (out_slot) *out_slot = slot_idx;
    
    return 0;
}

int slotted_page_get(SlottedPage *page, uint16_t slot, uint8_t *out_data, size_t *out_len) {
    if (!page || slot >= page->header.slot_count) return -1;
    
    SlotEntry *entry = &page->slots[slot];
    if (entry->length == 0) {
        return -1;  // Deleted record
    }
    
    if (out_data) {
        memcpy(out_data, (uint8_t*)page + entry->offset, entry->length);
    }
    if (out_len) {
        *out_len = entry->length;
    }
    
    return 0;
}

int slotted_page_delete(SlottedPage *page, uint16_t slot) {
    if (!page || slot >= page->header.slot_count) return -1;
    
    SlotEntry *entry = &page->slots[slot];
    if (entry->length == 0) {
        return -1;  // Already deleted
    }
    
    // Mark as deleted by setting length to 0
    entry->length = 0;
    
    return 0;
}

int slotted_page_update(SlottedPage *page, uint16_t slot, const uint8_t *data, size_t len) {
    if (!page || slot >= page->header.slot_count) return -1;
    
    SlotEntry *entry = &page->slots[slot];
    if (entry->length == 0) {
        return -1;  // Deleted record
    }
    
    // If new data fits in old space, update in place
    if (len <= entry->length) {
        memcpy((uint8_t*)page + entry->offset, data, len);
        entry->length = len;
        return 0;
    }
    
    // Otherwise, delete and reinsert
    slotted_page_delete(page, slot);
    
    // Check space
    size_t needed = len;
    if (slotted_page_free_space(page) < needed) {
        slotted_page_compact(page);
        if (slotted_page_free_space(page) < needed) {
            return -1;
        }
    }
    
    // Insert at new location
    uint16_t record_offset = page->header.free_space_end - len;
    memcpy((uint8_t*)page + record_offset, data, len);
    
    entry->offset = record_offset;
    entry->length = len;
    page->header.free_space_end = record_offset;
    
    return 0;
}

void slotted_page_compact(SlottedPage *page) {
    if (!page || page->header.slot_count == 0) return;
    
    // Temporary buffer for compaction
    uint8_t temp[PAGE_SIZE_SE];
    uint16_t write_pos = PAGE_SIZE_SE;
    
    // Compact records from end to start
    for (int i = page->header.slot_count - 1; i >= 0; i--) {
        SlotEntry *entry = &page->slots[i];
        if (entry->length > 0) {
            write_pos -= entry->length;
            memcpy(temp + write_pos, (uint8_t*)page + entry->offset, entry->length);
            entry->offset = write_pos;
        }
    }
    
    // Copy compacted data back
    size_t data_size = PAGE_SIZE_SE - write_pos;
    memcpy((uint8_t*)page + write_pos, temp + write_pos, data_size);
    page->header.free_space_end = write_pos;
}

// -----------------------------------------------------------------------------
// LZ4-Style Compression Implementation
// -----------------------------------------------------------------------------

/**
 * Simple LZ4-style compression using literal runs and match copying.
 * Format: [token][literal_length*][match_offset][match_length*][literals]
 * Token: 4 bits literal length, 4 bits match length
 */

size_t compress_lz4(const uint8_t *src, size_t src_len, 
                    uint8_t *dst, size_t dst_capacity) {
    if (!src || !dst || src_len == 0) return 0;
    
    // Simple compression: look for repeated sequences
    size_t dst_pos = 0;
    size_t src_pos = 0;
    
    // Store original size first
    if (dst_capacity < 4) return 0;
    memcpy(dst, &src_len, 4);
    dst_pos = 4;
    
    while (src_pos < src_len && dst_pos < dst_capacity - 1) {
        // Find best match in previous data
        size_t best_match_len = 0;
        size_t best_match_offset = 0;
        
        // Search window (last 255 bytes for simplicity)
        size_t search_start = src_pos > 255 ? src_pos - 255 : 0;
        
        for (size_t i = search_start; i < src_pos; i++) {
            size_t match_len = 0;
            while (src_pos + match_len < src_len && 
                   i + match_len < src_pos &&
                   src[i + match_len] == src[src_pos + match_len] &&
                   match_len < 255) {
                match_len++;
            }
            
            if (match_len >= 4 && match_len > best_match_len) {
                best_match_len = match_len;
                best_match_offset = src_pos - i;
            }
        }
        
        if (best_match_len >= 4) {
            // Emit match token
            if (dst_pos + 3 > dst_capacity) break;
            dst[dst_pos++] = 0x80 | (best_match_len > 15 ? 15 : best_match_len);
            dst[dst_pos++] = best_match_offset & 0xFF;
            if (best_match_len > 15) {
                dst[dst_pos++] = best_match_len - 15;
            }
            src_pos += best_match_len;
        } else {
            // Emit literal
            if (dst_pos + 2 > dst_capacity) break;
            dst[dst_pos++] = 0x00;  // Literal token
            dst[dst_pos++] = src[src_pos++];
        }
    }
    
    return dst_pos;
}

size_t decompress_lz4(const uint8_t *src, size_t src_len,
                      uint8_t *dst, size_t dst_capacity) {
    if (!src || !dst || src_len < 4) return 0;
    
    // Read original size
    uint32_t orig_size;
    memcpy(&orig_size, src, 4);
    if (orig_size > dst_capacity) return 0;
    
    size_t src_pos = 4;
    size_t dst_pos = 0;
    
    while (src_pos < src_len && dst_pos < orig_size) {
        uint8_t token = src[src_pos++];
        
        if (token & 0x80) {
            // Match
            if (src_pos >= src_len) break;
            uint8_t match_len = token & 0x0F;
            uint8_t offset = src[src_pos++];
            
            if (match_len == 15 && src_pos < src_len) {
                match_len += src[src_pos++];
            }
            
            // Copy from previous output
            for (size_t i = 0; i < match_len && dst_pos < orig_size; i++) {
                if (dst_pos >= offset) {
                    dst[dst_pos] = dst[dst_pos - offset];
                }
                dst_pos++;
            }
        } else {
            // Literal
            if (src_pos >= src_len) break;
            dst[dst_pos++] = src[src_pos++];
        }
    }
    
    return dst_pos;
}

// -----------------------------------------------------------------------------
// RLE Compression for Columnar Data
// -----------------------------------------------------------------------------

size_t compress_rle(const uint8_t *src, size_t src_len,
                    uint8_t *dst, size_t dst_capacity) {
    if (!src || !dst || src_len == 0) return 0;
    
    size_t dst_pos = 0;
    size_t i = 0;
    
    // Store original size
    if (dst_capacity < 4) return 0;
    memcpy(dst, &src_len, 4);
    dst_pos = 4;
    
    while (i < src_len && dst_pos + 2 <= dst_capacity) {
        uint8_t current = src[i];
        size_t run_length = 1;
        
        // Count run length
        while (i + run_length < src_len && 
               src[i + run_length] == current && 
               run_length < 255) {
            run_length++;
        }
        
        // Emit run
        dst[dst_pos++] = run_length;
        dst[dst_pos++] = current;
        i += run_length;
    }
    
    return dst_pos;
}

size_t decompress_rle(const uint8_t *src, size_t src_len,
                      uint8_t *dst, size_t dst_capacity) {
    if (!src || !dst || src_len < 4) return 0;
    
    uint32_t orig_size;
    memcpy(&orig_size, src, 4);
    if (orig_size > dst_capacity) return 0;
    
    size_t src_pos = 4;
    size_t dst_pos = 0;
    
    while (src_pos + 1 < src_len && dst_pos < orig_size) {
        uint8_t count = src[src_pos++];
        uint8_t value = src[src_pos++];
        
        for (int i = 0; i < count && dst_pos < orig_size; i++) {
            dst[dst_pos++] = value;
        }
    }
    
    return dst_pos;
}

// -----------------------------------------------------------------------------
// Slotted Page Compression
// -----------------------------------------------------------------------------

bool slotted_page_is_compressed(SlottedPage *page) {
    return (page->header.flags & PAGE_FLAG_COMPRESSED) != 0;
}

int slotted_page_compress(SlottedPage *page) {
    if (slotted_page_is_compressed(page)) return 0;
    
    // Only compress data portion
    size_t data_start = page->header.free_space_end;
    size_t data_size = PAGE_SIZE_SE - data_start;
    
    if (data_size < COMPRESSION_THRESHOLD) return 0;
    
    uint8_t compressed[PAGE_SIZE_SE];
    size_t compressed_size = compress_lz4((uint8_t*)page + data_start, 
                                          data_size, compressed, PAGE_SIZE_SE);
    
    // Only use compression if it saves space
    if (compressed_size > 0 && compressed_size < data_size - 64) {
        memcpy((uint8_t*)page + data_start, compressed, compressed_size);
        page->header.flags |= PAGE_FLAG_COMPRESSED;
        // Store compressed size somewhere... for now, in free_space_end hack
        return 1;
    }
    
    return 0;
}

int slotted_page_decompress(SlottedPage *page) {
    if (!slotted_page_is_compressed(page)) return 0;
    
    size_t data_start = page->header.free_space_end;
    size_t data_size = PAGE_SIZE_SE - data_start;
    
    uint8_t decompressed[PAGE_SIZE_SE];
    size_t decompressed_size = decompress_lz4((uint8_t*)page + data_start,
                                               data_size, decompressed, PAGE_SIZE_SE);
    
    if (decompressed_size > 0) {
        memcpy((uint8_t*)page + data_start, decompressed, decompressed_size);
        page->header.flags &= ~PAGE_FLAG_COMPRESSED;
        return 1;
    }
    
    return -1;
}

// -----------------------------------------------------------------------------
// Variable-Length Record Implementation
// -----------------------------------------------------------------------------

VarRecord* var_record_create(int field_count) {
    VarRecord *record = malloc(sizeof(VarRecord));
    if (!record) return NULL;
    
    memset(record, 0, sizeof(VarRecord));
    record->header.field_count = field_count;
    
    // Allocate null bitmap
    size_t bitmap_size = (field_count + 7) / 8;
    record->null_bitmap = calloc(1, bitmap_size);
    
    // Allocate variable field offsets
    record->var_offsets = calloc(field_count, sizeof(uint16_t));
    
    // Initial data buffer
    record->data = calloc(1, 256);
    
    return record;
}

void var_record_free(VarRecord *record) {
    if (!record) return;
    free(record->null_bitmap);
    free(record->var_offsets);
    free(record->data);
    free(record);
}

int var_record_set_null(VarRecord *record, int field_idx) {
    if (!record || field_idx < 0 || field_idx >= record->header.field_count) {
        return -1;
    }
    
    int byte_idx = field_idx / 8;
    int bit_idx = field_idx % 8;
    record->null_bitmap[byte_idx] |= (1 << bit_idx);
    
    return 0;
}

bool var_record_is_null(VarRecord *record, int field_idx) {
    if (!record || field_idx < 0 || field_idx >= record->header.field_count) {
        return true;
    }
    
    int byte_idx = field_idx / 8;
    int bit_idx = field_idx % 8;
    return (record->null_bitmap[byte_idx] & (1 << bit_idx)) != 0;
}

// Simplified record serialization
size_t var_record_serialize(VarRecord *record, uint8_t *buffer, size_t capacity) {
    if (!record || !buffer) return 0;
    
    size_t bitmap_size = (record->header.field_count + 7) / 8;
    size_t offsets_size = record->header.field_count * sizeof(uint16_t);
    size_t total_size = sizeof(VarRecordHeader) + bitmap_size + offsets_size;
    
    if (total_size > capacity) return 0;
    
    uint8_t *ptr = buffer;
    
    // Write header
    memcpy(ptr, &record->header, sizeof(VarRecordHeader));
    ptr += sizeof(VarRecordHeader);
    
    // Write null bitmap
    memcpy(ptr, record->null_bitmap, bitmap_size);
    ptr += bitmap_size;
    
    // Write offsets
    memcpy(ptr, record->var_offsets, offsets_size);
    ptr += offsets_size;
    
    return ptr - buffer;
}

// -----------------------------------------------------------------------------
// Columnar Storage Implementation
// -----------------------------------------------------------------------------

ColumnStore* column_store_create(const char *table_name, int column_count) {
    ColumnStore *store = malloc(sizeof(ColumnStore));
    if (!store) return NULL;
    
    memset(store, 0, sizeof(ColumnStore));
    strncpy(store->table_name, table_name, sizeof(store->table_name) - 1);
    store->column_count = 0;
    store->row_count = 0;
    store->columns = calloc(column_count, sizeof(ColumnChunk));
    
    return store;
}

void column_store_free(ColumnStore *store) {
    if (!store) return;
    
    for (uint32_t i = 0; i < store->column_count; i++) {
        free(store->columns[i].null_bitmap);
        free(store->columns[i].data);
    }
    free(store->columns);
    free(store);
}

int column_store_add_column(ColumnStore *store, const char *name, ColumnDataType type) {
    if (!store || store->column_count >= MAX_COLUMN_COUNT) return -1;
    
    ColumnChunk *chunk = &store->columns[store->column_count];
    strncpy(chunk->header.name, name, sizeof(chunk->header.name) - 1);
    chunk->header.type = type;
    chunk->header.row_count = 0;
    chunk->header.null_count = 0;
    chunk->header.compression = COMPRESS_NONE;
    
    // Initialize min/max
    chunk->header.min_val.int_min = INT64_MAX;
    chunk->header.max_val.int_max = INT64_MIN;
    
    // Allocate initial buffers
    size_t initial_capacity = 1024;
    size_t elem_size = 8;  // Default to 8 bytes
    
    chunk->null_bitmap = calloc(1, (initial_capacity + 7) / 8);
    chunk->data = calloc(initial_capacity, elem_size);
    chunk->data_size = 0;
    
    store->column_count++;
    return store->column_count - 1;
}

// Columnar aggregation - optimized sum
int64_t column_sum_int(ColumnChunk *chunk) {
    if (!chunk || (chunk->header.type != COL_TYPE_INT32 && 
                   chunk->header.type != COL_TYPE_INT64)) {
        return 0;
    }
    
    int64_t sum = 0;
    int32_t *data32 = (int32_t*)chunk->data;
    int64_t *data64 = (int64_t*)chunk->data;
    
    for (uint32_t i = 0; i < chunk->header.row_count; i++) {
        // Check null bitmap
        if (chunk->null_bitmap[i / 8] & (1 << (i % 8))) continue;
        
        if (chunk->header.type == COL_TYPE_INT32) {
            sum += data32[i];
        } else {
            sum += data64[i];
        }
    }
    
    return sum;
}

double column_avg_int(ColumnChunk *chunk) {
    if (!chunk) return 0.0;
    
    uint32_t non_null = column_count_non_null(chunk);
    if (non_null == 0) return 0.0;
    
    return (double)column_sum_int(chunk) / non_null;
}

int64_t column_min_int(ColumnChunk *chunk) {
    return chunk ? chunk->header.min_val.int_min : 0;
}

int64_t column_max_int(ColumnChunk *chunk) {
    return chunk ? chunk->header.max_val.int_max : 0;
}

uint32_t column_count_non_null(ColumnChunk *chunk) {
    if (!chunk) return 0;
    return chunk->header.row_count - chunk->header.null_count;
}

// -----------------------------------------------------------------------------
// LSM Tree Implementation
// -----------------------------------------------------------------------------

// AVL Tree helpers for MemTable
static int avl_height(MemTableNode *node) {
    return node ? node->height : 0;
}

static int avl_balance(MemTableNode *node) {
    return node ? avl_height(node->left) - avl_height(node->right) : 0;
}

static MemTableNode* avl_rotate_right(MemTableNode *y) {
    MemTableNode *x = y->left;
    MemTableNode *T2 = x->right;
    
    x->right = y;
    y->left = T2;
    
    y->height = 1 + (avl_height(y->left) > avl_height(y->right) ? 
                     avl_height(y->left) : avl_height(y->right));
    x->height = 1 + (avl_height(x->left) > avl_height(x->right) ? 
                     avl_height(x->left) : avl_height(x->right));
    
    return x;
}

static MemTableNode* avl_rotate_left(MemTableNode *x) {
    MemTableNode *y = x->right;
    MemTableNode *T2 = y->left;
    
    y->left = x;
    x->right = T2;
    
    x->height = 1 + (avl_height(x->left) > avl_height(x->right) ? 
                     avl_height(x->left) : avl_height(x->right));
    y->height = 1 + (avl_height(y->left) > avl_height(y->right) ? 
                     avl_height(y->left) : avl_height(y->right));
    
    return y;
}

static MemTableNode* memtable_node_create(const char *key, const uint8_t *value, 
                                          size_t len, bool deleted) {
    MemTableNode *node = malloc(sizeof(MemTableNode));
    if (!node) return NULL;
    
    node->entry.key = strdup(key);
    node->entry.value = malloc(len);
    memcpy(node->entry.value, value, len);
    node->entry.value_len = len;
    node->entry.timestamp = (uint64_t)time(NULL);
    node->entry.deleted = deleted;
    
    node->left = NULL;
    node->right = NULL;
    node->height = 1;
    
    return node;
}

static MemTableNode* memtable_insert_node(MemTableNode *node, const char *key,
                                          const uint8_t *value, size_t len,
                                          bool deleted) {
    if (!node) {
        return memtable_node_create(key, value, len, deleted);
    }
    
    int cmp = strcmp(key, node->entry.key);
    
    if (cmp < 0) {
        node->left = memtable_insert_node(node->left, key, value, len, deleted);
    } else if (cmp > 0) {
        node->right = memtable_insert_node(node->right, key, value, len, deleted);
    } else {
        // Update existing entry
        free(node->entry.value);
        node->entry.value = malloc(len);
        memcpy(node->entry.value, value, len);
        node->entry.value_len = len;
        node->entry.timestamp = (uint64_t)time(NULL);
        node->entry.deleted = deleted;
        return node;
    }
    
    // Update height
    node->height = 1 + (avl_height(node->left) > avl_height(node->right) ?
                        avl_height(node->left) : avl_height(node->right));
    
    // Balance
    int balance = avl_balance(node);
    
    // Left Left
    if (balance > 1 && strcmp(key, node->left->entry.key) < 0) {
        return avl_rotate_right(node);
    }
    
    // Right Right
    if (balance < -1 && strcmp(key, node->right->entry.key) > 0) {
        return avl_rotate_left(node);
    }
    
    // Left Right
    if (balance > 1 && strcmp(key, node->left->entry.key) > 0) {
        node->left = avl_rotate_left(node->left);
        return avl_rotate_right(node);
    }
    
    // Right Left
    if (balance < -1 && strcmp(key, node->right->entry.key) < 0) {
        node->right = avl_rotate_right(node->right);
        return avl_rotate_left(node);
    }
    
    return node;
}

static LSMEntry* memtable_find_node(MemTableNode *node, const char *key) {
    if (!node) return NULL;
    
    int cmp = strcmp(key, node->entry.key);
    
    if (cmp < 0) {
        return memtable_find_node(node->left, key);
    } else if (cmp > 0) {
        return memtable_find_node(node->right, key);
    } else {
        return &node->entry;
    }
}

static void memtable_free_node(MemTableNode *node) {
    if (!node) return;
    
    memtable_free_node(node->left);
    memtable_free_node(node->right);
    
    free(node->entry.key);
    free(node->entry.value);
    free(node);
}

MemTable* memtable_create(void) {
    MemTable *mt = malloc(sizeof(MemTable));
    if (!mt) return NULL;
    
    memset(mt, 0, sizeof(MemTable));
    return mt;
}

void memtable_free(MemTable *mt) {
    if (!mt) return;
    memtable_free_node(mt->root);
    free(mt);
}

int memtable_put(MemTable *mt, const char *key, const uint8_t *value, size_t len) {
    if (!mt || !key) return -1;
    
    mt->root = memtable_insert_node(mt->root, key, value, len, false);
    mt->size += len + strlen(key) + sizeof(LSMEntry);
    mt->count++;
    
    return 0;
}

LSMEntry* memtable_get(MemTable *mt, const char *key) {
    if (!mt || !key) return NULL;
    return memtable_find_node(mt->root, key);
}

int memtable_delete(MemTable *mt, const char *key) {
    if (!mt || !key) return -1;
    
    // Insert tombstone
    uint8_t tombstone = 0;
    mt->root = memtable_insert_node(mt->root, key, &tombstone, 1, true);
    
    return 0;
}

// -----------------------------------------------------------------------------
// Bloom Filter Implementation
// -----------------------------------------------------------------------------

static uint64_t bloom_hash1(const char *key) {
    uint64_t hash = 5381;
    int c;
    while ((c = *key++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

static uint64_t bloom_hash2(const char *key) {
    uint64_t hash = 0;
    int c;
    while ((c = *key++))
        hash = c + (hash << 6) + (hash << 16) - hash;
    return hash;
}

void bloom_add(uint8_t *filter, size_t size, const char *key) {
    if (!filter || !key) return;
    
    size_t bits = size * 8;
    uint64_t h1 = bloom_hash1(key);
    uint64_t h2 = bloom_hash2(key);
    
    // Use 4 hash functions
    for (int i = 0; i < 4; i++) {
        uint64_t idx = (h1 + i * h2) % bits;
        filter[idx / 8] |= (1 << (idx % 8));
    }
}

bool bloom_may_contain(uint8_t *filter, size_t size, const char *key) {
    if (!filter || !key) return false;
    
    size_t bits = size * 8;
    uint64_t h1 = bloom_hash1(key);
    uint64_t h2 = bloom_hash2(key);
    
    for (int i = 0; i < 4; i++) {
        uint64_t idx = (h1 + i * h2) % bits;
        if (!(filter[idx / 8] & (1 << (idx % 8)))) {
            return false;
        }
    }
    
    return true;
}

// -----------------------------------------------------------------------------
// LSM Tree Main Functions
// -----------------------------------------------------------------------------

LSMTree* lsm_create(const char *base_path) {
    LSMTree *tree = malloc(sizeof(LSMTree));
    if (!tree) return NULL;
    
    memset(tree, 0, sizeof(LSMTree));
    tree->base_path = strdup(base_path);
    tree->memtable = memtable_create();
    tree->next_sstable_id = 1;
    
    // Open WAL
    char wal_path[512];
    snprintf(wal_path, sizeof(wal_path), "%s/wal.log", base_path);
    tree->wal_file = fopen(wal_path, "ab+");
    
    // Initialize levels
    for (int i = 0; i < LSM_LEVEL_COUNT; i++) {
        tree->levels[i].level = i;
        tree->levels[i].table_count = 0;
        tree->levels[i].table_capacity = 10;
        tree->levels[i].tables = calloc(10, sizeof(SSTable));
    }
    
    LOG_INFO("LSM Tree created at %s", base_path);
    
    return tree;
}

void lsm_close(LSMTree *tree) {
    if (!tree) return;
    
    // Flush memtable if not empty
    if (tree->memtable && tree->memtable->count > 0) {
        lsm_flush_memtable(tree);
    }
    
    memtable_free(tree->memtable);
    memtable_free(tree->immutable);
    
    if (tree->wal_file) {
        fclose(tree->wal_file);
    }
    
    // Free levels and SSTables
    for (int i = 0; i < LSM_LEVEL_COUNT; i++) {
        for (int j = 0; j < tree->levels[i].table_count; j++) {
            free(tree->levels[i].tables[j].bloom_filter);
            free(tree->levels[i].tables[j].key_index);
            free(tree->levels[i].tables[j].offset_index);
        }
        free(tree->levels[i].tables);
    }
    
    free(tree->base_path);
    free(tree);
}

int lsm_put(LSMTree *tree, const char *key, const uint8_t *value, size_t len) {
    if (!tree || !key || !value) return -1;
    
    // Write to WAL first
    if (tree->wal_file) {
        uint32_t key_len = strlen(key);
        fwrite(&key_len, sizeof(key_len), 1, tree->wal_file);
        fwrite(key, 1, key_len, tree->wal_file);
        fwrite(&len, sizeof(len), 1, tree->wal_file);
        fwrite(value, 1, len, tree->wal_file);
        fflush(tree->wal_file);
    }
    
    // Insert into memtable
    memtable_put(tree->memtable, key, value, len);
    
    // Check if memtable is full
    if (tree->memtable->size >= MEMTABLE_SIZE) {
        lsm_flush_memtable(tree);
    }
    
    return 0;
}

int lsm_get(LSMTree *tree, const char *key, uint8_t **out_value, size_t *out_len) {
    if (!tree || !key) return -1;
    
    // Check memtable first
    LSMEntry *entry = memtable_get(tree->memtable, key);
    if (entry) {
        if (entry->deleted) return -1;  // Tombstone
        *out_value = malloc(entry->value_len);
        memcpy(*out_value, entry->value, entry->value_len);
        *out_len = entry->value_len;
        return 0;
    }
    
    // Check immutable memtable
    if (tree->immutable) {
        entry = memtable_get(tree->immutable, key);
        if (entry) {
            if (entry->deleted) return -1;
            *out_value = malloc(entry->value_len);
            memcpy(*out_value, entry->value, entry->value_len);
            *out_len = entry->value_len;
            return 0;
        }
    }
    
    // Check SSTables level by level
    for (int lvl = 0; lvl < LSM_LEVEL_COUNT; lvl++) {
        LSMLevel *level = &tree->levels[lvl];
        
        for (int i = level->table_count - 1; i >= 0; i--) {
            SSTable *sst = &level->tables[i];
            
            // Quick bloom filter check
            if (sst->bloom_filter && !bloom_may_contain(sst->bloom_filter, 
                                                         sst->bloom_size, key)) {
                continue;  // Definitely not here
            }
            
            // Search SSTable (simplified - would do binary search in real impl)
            entry = sstable_get(sst, key);
            if (entry) {
                if (entry->deleted) {
                    free(entry->key);
                    free(entry->value);
                    free(entry);
                    return -1;
                }
                *out_value = entry->value;
                *out_len = entry->value_len;
                free(entry->key);
                free(entry);
                return 0;
            }
        }
    }
    
    return -1;  // Not found
}

int lsm_delete(LSMTree *tree, const char *key) {
    if (!tree || !key) return -1;
    return memtable_delete(tree->memtable, key);
}

// Simplified SSTable creation
SSTable* sstable_create(const char *filename) {
    SSTable *sst = malloc(sizeof(SSTable));
    if (!sst) return NULL;
    
    memset(sst, 0, sizeof(SSTable));
    strncpy(sst->filename, filename, sizeof(sst->filename) - 1);
    
    // Allocate bloom filter (1KB)
    sst->bloom_size = 1024;
    sst->bloom_filter = calloc(1, sst->bloom_size);
    
    return sst;
}

void sstable_free(SSTable *sst) {
    if (!sst) return;
    free(sst->bloom_filter);
    free(sst->key_index);
    free(sst->offset_index);
    // Note: don't free sst itself if it's part of an array
}

LSMEntry* sstable_get(SSTable *sst, const char *key) {
    if (!sst || !key) return NULL;
    
    // Simplified: read from file and search
    FILE *f = fopen(sst->filename, "rb");
    if (!f) return NULL;
    
    LSMEntry *result = NULL;
    
    while (!feof(f)) {
        uint32_t key_len;
        if (fread(&key_len, sizeof(key_len), 1, f) != 1) break;
        
        char *read_key = malloc(key_len + 1);
        fread(read_key, 1, key_len, f);
        read_key[key_len] = '\0';
        
        uint8_t deleted;
        fread(&deleted, 1, 1, f);
        
        size_t value_len;
        fread(&value_len, sizeof(value_len), 1, f);
        
        if (strcmp(read_key, key) == 0) {
            result = malloc(sizeof(LSMEntry));
            result->key = read_key;
            result->value = malloc(value_len);
            fread(result->value, 1, value_len, f);
            result->value_len = value_len;
            result->deleted = deleted;
            break;
        } else {
            fseek(f, value_len, SEEK_CUR);
            free(read_key);
        }
    }
    
    fclose(f);
    return result;
}

// Helper to write memtable entries to SSTable in order
static void sstable_write_node(FILE *f, MemTableNode *node, SSTable *sst) {
    if (!node) return;
    
    // In-order traversal for sorted output
    sstable_write_node(f, node->left, sst);
    
    // Write entry
    uint32_t key_len = strlen(node->entry.key);
    fwrite(&key_len, sizeof(key_len), 1, f);
    fwrite(node->entry.key, 1, key_len, f);
    
    uint8_t deleted = node->entry.deleted ? 1 : 0;
    fwrite(&deleted, 1, 1, f);
    
    fwrite(&node->entry.value_len, sizeof(node->entry.value_len), 1, f);
    fwrite(node->entry.value, 1, node->entry.value_len, f);
    
    // Add to bloom filter
    bloom_add(sst->bloom_filter, sst->bloom_size, node->entry.key);
    sst->entry_count++;
    
    sstable_write_node(f, node->right, sst);
}

int sstable_write(SSTable *sst, MemTable *mt) {
    if (!sst || !mt) return -1;
    
    FILE *f = fopen(sst->filename, "wb");
    if (!f) return -1;
    
    sstable_write_node(f, mt->root, sst);
    
    sst->file_size = ftell(f);
    fclose(f);
    
    return 0;
}

int lsm_flush_memtable(LSMTree *tree) {
    if (!tree || !tree->memtable || tree->memtable->count == 0) return 0;
    
    // Create new SSTable
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/level0_%llu.sst", 
             tree->base_path, (unsigned long long)tree->next_sstable_id++);
    
    SSTable *sst = sstable_create(filename);
    if (!sst) return -1;
    
    // Write memtable to SSTable
    if (sstable_write(sst, tree->memtable) != 0) {
        sstable_free(sst);
        free(sst);
        return -1;
    }
    
    // Add to level 0
    LSMLevel *level0 = &tree->levels[0];
    if (level0->table_count >= level0->table_capacity) {
        level0->table_capacity *= 2;
        level0->tables = realloc(level0->tables, 
                                  level0->table_capacity * sizeof(SSTable));
    }
    level0->tables[level0->table_count++] = *sst;
    free(sst);
    
    // Clear memtable
    memtable_free(tree->memtable);
    tree->memtable = memtable_create();
    
    // Truncate WAL
    if (tree->wal_file) {
        fclose(tree->wal_file);
        char wal_path[512];
        snprintf(wal_path, sizeof(wal_path), "%s/wal.log", tree->base_path);
        tree->wal_file = fopen(wal_path, "wb+");
    }
    
    LOG_INFO("Flushed memtable to SSTable: %s", filename);
    
    // Trigger compaction if needed
    if (level0->table_count >= 4) {
        lsm_compact(tree, 0);
    }
    
    return 0;
}

int lsm_compact(LSMTree *tree, int level) {
    // Simplified compaction - just log for now
    LOG_INFO("Compaction triggered for level %d", level);
    tree->compacting = true;
    
    // Real implementation would merge SSTables and push to next level
    // For this prototype, we just acknowledge the trigger
    
    tree->compacting = false;
    return 0;
}

// -----------------------------------------------------------------------------
// Storage Engine Manager Implementation
// -----------------------------------------------------------------------------

StorageEngine* storage_engine_create(StorageEngineType type, const char *path) {
    StorageEngine *engine = malloc(sizeof(StorageEngine));
    if (!engine) return NULL;
    
    memset(engine, 0, sizeof(StorageEngine));
    engine->type = type;
    engine->compression = COMPRESS_NONE;
    engine->auto_compact = true;
    
    switch (type) {
        case ENGINE_ROW_STORE:
            // Row store uses the default KVStore from storage.c
            LOG_INFO("Created Row Store engine at %s", path);
            break;
            
        case ENGINE_COLUMN_STORE:
            engine->engine = column_store_create("default", MAX_COLUMN_COUNT);
            LOG_INFO("Created Column Store engine at %s", path);
            break;
            
        case ENGINE_LSM_TREE:
            engine->engine = lsm_create(path);
            LOG_INFO("Created LSM Tree engine at %s", path);
            break;
    }
    
    return engine;
}

void storage_engine_close(StorageEngine *engine) {
    if (!engine) return;
    
    switch (engine->type) {
        case ENGINE_ROW_STORE:
            // Handled by kv_destroy
            break;
            
        case ENGINE_COLUMN_STORE:
            column_store_free((ColumnStore*)engine->engine);
            break;
            
        case ENGINE_LSM_TREE:
            lsm_close((LSMTree*)engine->engine);
            break;
    }
    
    free(engine);
}

int storage_engine_put(StorageEngine *engine, const char *key,
                       const uint8_t *value, size_t len) {
    if (!engine || !key || !value) return -1;
    
    engine->writes++;
    engine->bytes_written += len;
    
    switch (engine->type) {
        case ENGINE_ROW_STORE:
            // Use existing KVStore
            return 0;
            
        case ENGINE_COLUMN_STORE:
            // Columnar insert would need row-to-column transformation
            return 0;
            
        case ENGINE_LSM_TREE:
            return lsm_put((LSMTree*)engine->engine, key, value, len);
    }
    
    return -1;
}

int storage_engine_get(StorageEngine *engine, const char *key,
                       uint8_t **out_value, size_t *out_len) {
    if (!engine || !key) return -1;
    
    engine->reads++;
    
    switch (engine->type) {
        case ENGINE_ROW_STORE:
            return 0;
            
        case ENGINE_COLUMN_STORE:
            return 0;
            
        case ENGINE_LSM_TREE:
            return lsm_get((LSMTree*)engine->engine, key, out_value, out_len);
    }
    
    return -1;
}

int storage_engine_delete(StorageEngine *engine, const char *key) {
    if (!engine || !key) return -1;
    
    switch (engine->type) {
        case ENGINE_LSM_TREE:
            return lsm_delete((LSMTree*)engine->engine, key);
        default:
            return 0;
    }
}

void storage_engine_set_compression(StorageEngine *engine, CompressionType type) {
    if (engine) {
        engine->compression = type;
    }
}

void storage_engine_print_stats(StorageEngine *engine) {
    if (!engine) return;
    
    const char *type_str = "Unknown";
    switch (engine->type) {
        case ENGINE_ROW_STORE: type_str = "Row Store"; break;
        case ENGINE_COLUMN_STORE: type_str = "Column Store"; break;
        case ENGINE_LSM_TREE: type_str = "LSM Tree"; break;
    }
    
    fprintf(stderr, "\n--- Storage Engine Statistics ---\n");
    fprintf(stderr, "Type: %s\n", type_str);
    fprintf(stderr, "Compression: %s\n", 
            engine->compression == COMPRESS_NONE ? "None" :
            engine->compression == COMPRESS_LZ4 ? "LZ4" : "RLE");
    fprintf(stderr, "Reads: %llu\n", (unsigned long long)engine->reads);
    fprintf(stderr, "Writes: %llu\n", (unsigned long long)engine->writes);
    fprintf(stderr, "Bytes Read: %llu\n", (unsigned long long)engine->bytes_read);
    fprintf(stderr, "Bytes Written: %llu\n", (unsigned long long)engine->bytes_written);
    fprintf(stderr, "---------------------------------\n\n");
}
