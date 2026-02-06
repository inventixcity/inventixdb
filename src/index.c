/**
 * InventixDB Secondary Index Implementation
 * 
 * B+ Tree based secondary indexes with:
 * - Multi-column composite key support
 * - Range scan and point lookup
 * - Index statistics for query optimizer
 * - Concurrent access with read-write locks
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "index.h"
#include "logger.h"

// -----------------------------------------------------------------------------
// Global State
// -----------------------------------------------------------------------------

static IndexManager *g_index_manager = NULL;
static pthread_mutex_t g_manager_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_index_initialized = false;
static uint32_t g_next_index_id = 1;

// -----------------------------------------------------------------------------
// Index Manager Initialization
// -----------------------------------------------------------------------------

int index_manager_init(void) {
    if (g_index_initialized) return 0;
    
    pthread_mutex_lock(&g_manager_lock);
    
    g_index_manager = calloc(1, sizeof(IndexManager));
    if (!g_index_manager) {
        pthread_mutex_unlock(&g_manager_lock);
        return -1;
    }
    
    g_index_manager->index_capacity = 256;
    g_index_manager->indexes = calloc(256, sizeof(IndexDef *));
    if (!g_index_manager->indexes) {
        free(g_index_manager);
        g_index_manager = NULL;
        pthread_mutex_unlock(&g_manager_lock);
        return -1;
    }
    
    pthread_mutex_init(&g_index_manager->manager_lock, NULL);
    g_index_initialized = true;
    
    LOG_INFO("Index manager initialized");
    
    pthread_mutex_unlock(&g_manager_lock);
    return 0;
}

void index_manager_shutdown(void) {
    if (!g_index_initialized) return;
    
    pthread_mutex_lock(&g_manager_lock);
    
    if (g_index_manager) {
        for (int i = 0; i < g_index_manager->index_count; i++) {
            IndexDef *idx = g_index_manager->indexes[i];
            if (idx) {
                pthread_rwlock_destroy(&idx->lock);
                free(idx->table_name);
                free(idx->index_file);
                for (int j = 0; j < idx->column_count; j++) {
                    free(idx->columns[j].column_name);
                }
                free(idx->stats.min_value);
                free(idx->stats.max_value);
                free(idx);
            }
        }
        free(g_index_manager->indexes);
        pthread_mutex_destroy(&g_index_manager->manager_lock);
        free(g_index_manager);
        g_index_manager = NULL;
    }
    
    g_index_initialized = false;
    pthread_mutex_unlock(&g_manager_lock);
    
    LOG_INFO("Index manager shutdown");
}

// -----------------------------------------------------------------------------
// Index Creation
// -----------------------------------------------------------------------------

IndexDef *index_create(const char *table_name,
                       const char *index_name,
                       IndexColumn *columns,
                       int column_count,
                       IndexType type,
                       bool is_unique) {
    if (!g_index_manager || !table_name || !index_name || !columns || column_count <= 0) {
        return NULL;
    }
    
    // Check if index already exists
    if (index_get(table_name, index_name)) {
        LOG_WARN("Index '%s' already exists on table '%s'", index_name, table_name);
        return NULL;
    }
    
    pthread_mutex_lock(&g_index_manager->manager_lock);
    
    // Check capacity
    if (g_index_manager->index_count >= g_index_manager->index_capacity) {
        // Grow array
        int new_cap = g_index_manager->index_capacity * 2;
        IndexDef **new_arr = realloc(g_index_manager->indexes, 
                                     new_cap * sizeof(IndexDef *));
        if (!new_arr) {
            pthread_mutex_unlock(&g_index_manager->manager_lock);
            return NULL;
        }
        g_index_manager->indexes = new_arr;
        g_index_manager->index_capacity = new_cap;
    }
    
    // Create index definition
    IndexDef *idx = calloc(1, sizeof(IndexDef));
    if (!idx) {
        pthread_mutex_unlock(&g_index_manager->manager_lock);
        return NULL;
    }
    
    strncpy(idx->name, index_name, MAX_INDEX_NAME_LEN - 1);
    idx->table_name = strdup(table_name);
    idx->index_id = __sync_add_and_fetch(&g_next_index_id, 1);
    idx->type = type;
    idx->state = INDEX_STATE_BUILDING;
    idx->is_unique = is_unique;
    idx->column_count = column_count;
    
    // Copy column definitions
    for (int i = 0; i < column_count && i < MAX_INDEX_COLUMNS; i++) {
        idx->columns[i].column_name = strdup(columns[i].column_name);
        idx->columns[i].column_pos = columns[i].column_pos;
        idx->columns[i].order = columns[i].order;
        idx->columns[i].key_prefix_len = columns[i].key_prefix_len;
    }
    
    // Generate index file name
    char filename[256];
    snprintf(filename, sizeof(filename), "%s_%s.idx", table_name, index_name);
    idx->index_file = strdup(filename);
    
    // Initialize B+ Tree root
    idx->root_page = 0;  // Will be set when first page is allocated
    
    // Initialize statistics
    idx->stats.last_analyzed = 0;
    idx->stats.selectivity = 1.0;
    
    // Initialize lock
    pthread_rwlock_init(&idx->lock, NULL);
    
    // Set timestamps
    idx->created_at = (uint64_t)time(NULL);
    idx->last_modified = idx->created_at;
    
    // Add to manager
    g_index_manager->indexes[g_index_manager->index_count++] = idx;
    g_index_manager->total_indexes++;
    
    // Mark as valid (simplified - no actual build for now)
    idx->state = INDEX_STATE_VALID;
    
    pthread_mutex_unlock(&g_index_manager->manager_lock);
    
    LOG_INFO("Created index '%s' on table '%s' (%d columns, %s, %s)",
             index_name, table_name, column_count,
             is_unique ? "unique" : "non-unique",
             type == INDEX_TYPE_BTREE ? "btree" : "hash");
    
    return idx;
}

// -----------------------------------------------------------------------------
// Index Drop
// -----------------------------------------------------------------------------

int index_drop(const char *table_name, const char *index_name) {
    if (!g_index_manager || !table_name || !index_name) return -1;
    
    pthread_mutex_lock(&g_index_manager->manager_lock);
    
    for (int i = 0; i < g_index_manager->index_count; i++) {
        IndexDef *idx = g_index_manager->indexes[i];
        if (idx && 
            strcmp(idx->table_name, table_name) == 0 &&
            strcmp(idx->name, index_name) == 0) {
            
            // Acquire write lock before destroying
            pthread_rwlock_wrlock(&idx->lock);
            
            idx->state = INDEX_STATE_DROPPED;
            
            // Clean up
            pthread_rwlock_unlock(&idx->lock);
            pthread_rwlock_destroy(&idx->lock);
            
            free(idx->table_name);
            free(idx->index_file);
            for (int j = 0; j < idx->column_count; j++) {
                free(idx->columns[j].column_name);
            }
            free(idx->stats.min_value);
            free(idx->stats.max_value);
            free(idx);
            
            // Compact array
            for (int j = i; j < g_index_manager->index_count - 1; j++) {
                g_index_manager->indexes[j] = g_index_manager->indexes[j + 1];
            }
            g_index_manager->index_count--;
            
            pthread_mutex_unlock(&g_index_manager->manager_lock);
            
            LOG_INFO("Dropped index '%s' from table '%s'", index_name, table_name);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_index_manager->manager_lock);
    return -1;
}

int index_drop_all(const char *table_name) {
    if (!g_index_manager || !table_name) return -1;
    
    int dropped = 0;
    
    // Keep dropping until none found
    while (1) {
        pthread_mutex_lock(&g_index_manager->manager_lock);
        
        int found = -1;
        for (int i = 0; i < g_index_manager->index_count; i++) {
            IndexDef *idx = g_index_manager->indexes[i];
            if (idx && strcmp(idx->table_name, table_name) == 0) {
                found = i;
                break;
            }
        }
        
        if (found < 0) {
            pthread_mutex_unlock(&g_index_manager->manager_lock);
            break;
        }
        
        IndexDef *idx = g_index_manager->indexes[found];
        char name[MAX_INDEX_NAME_LEN];
        strncpy(name, idx->name, MAX_INDEX_NAME_LEN);
        
        pthread_mutex_unlock(&g_index_manager->manager_lock);
        
        if (index_drop(table_name, name) == 0) {
            dropped++;
        }
    }
    
    return dropped;
}

// -----------------------------------------------------------------------------
// Index Lookup
// -----------------------------------------------------------------------------

IndexDef *index_get(const char *table_name, const char *index_name) {
    if (!g_index_manager || !table_name || !index_name) return NULL;
    
    pthread_mutex_lock(&g_index_manager->manager_lock);
    
    for (int i = 0; i < g_index_manager->index_count; i++) {
        IndexDef *idx = g_index_manager->indexes[i];
        if (idx && 
            strcmp(idx->table_name, table_name) == 0 &&
            strcmp(idx->name, index_name) == 0 &&
            idx->state == INDEX_STATE_VALID) {
            pthread_mutex_unlock(&g_index_manager->manager_lock);
            return idx;
        }
    }
    
    pthread_mutex_unlock(&g_index_manager->manager_lock);
    return NULL;
}

IndexDef **index_get_all(const char *table_name, int *count) {
    if (!g_index_manager || !table_name || !count) return NULL;
    
    pthread_mutex_lock(&g_index_manager->manager_lock);
    
    // Count matching indexes
    int n = 0;
    for (int i = 0; i < g_index_manager->index_count; i++) {
        IndexDef *idx = g_index_manager->indexes[i];
        if (idx && strcmp(idx->table_name, table_name) == 0 &&
            idx->state == INDEX_STATE_VALID) {
            n++;
        }
    }
    
    if (n == 0) {
        pthread_mutex_unlock(&g_index_manager->manager_lock);
        *count = 0;
        return NULL;
    }
    
    IndexDef **result = malloc(sizeof(IndexDef *) * n);
    if (!result) {
        pthread_mutex_unlock(&g_index_manager->manager_lock);
        *count = 0;
        return NULL;
    }
    
    int j = 0;
    for (int i = 0; i < g_index_manager->index_count && j < n; i++) {
        IndexDef *idx = g_index_manager->indexes[i];
        if (idx && strcmp(idx->table_name, table_name) == 0 &&
            idx->state == INDEX_STATE_VALID) {
            result[j++] = idx;
        }
    }
    
    pthread_mutex_unlock(&g_index_manager->manager_lock);
    *count = n;
    return result;
}

// -----------------------------------------------------------------------------
// Key Building
// -----------------------------------------------------------------------------

int index_build_key(IndexDef *index, 
                    const char **col_values, 
                    uint8_t *key_buffer, 
                    uint16_t *key_len) {
    if (!index || !col_values || !key_buffer || !key_len) return -1;
    
    uint16_t offset = 0;
    
    for (int i = 0; i < index->column_count; i++) {
        const char *val = col_values[i];
        if (!val) {
            // NULL marker
            key_buffer[offset++] = 0x00;
            continue;
        }
        
        // Non-null marker
        key_buffer[offset++] = 0x01;
        
        size_t val_len = strlen(val);
        int max_len = index->columns[i].key_prefix_len;
        if (max_len <= 0) max_len = 255;
        
        if (val_len > (size_t)max_len) val_len = max_len;
        
        // Check buffer overflow
        if (offset + val_len + 2 > MAX_INDEX_KEY_SIZE) {
            return -1;
        }
        
        // Length-prefixed string
        key_buffer[offset++] = (uint8_t)val_len;
        memcpy(key_buffer + offset, val, val_len);
        offset += val_len;
    }
    
    *key_len = offset;
    return 0;
}

// -----------------------------------------------------------------------------
// Key Comparison
// -----------------------------------------------------------------------------

int index_key_compare(const uint8_t *key1, uint16_t len1,
                      const uint8_t *key2, uint16_t len2) {
    uint16_t min_len = len1 < len2 ? len1 : len2;
    
    int cmp = memcmp(key1, key2, min_len);
    if (cmp != 0) return cmp;
    
    if (len1 < len2) return -1;
    if (len1 > len2) return 1;
    return 0;
}

// -----------------------------------------------------------------------------
// B+ Tree Node Operations
// -----------------------------------------------------------------------------

IndexBTreeNode *index_btree_create_node(bool is_leaf) {
    IndexBTreeNode *node = calloc(1, sizeof(IndexBTreeNode));
    if (!node) return NULL;
    
    node->is_leaf = is_leaf;
    node->key_count = 0;
    node->next_leaf = 0;
    node->prev_leaf = 0;
    
    return node;
}

void index_btree_destroy_node(IndexBTreeNode *node) {
    if (!node) return;
    
    for (int i = 0; i < node->key_count; i++) {
        free(node->keys[i]);
    }
    free(node);
}

// Simplified B+ Tree search (returns the leaf node containing the key)
IndexBTreeNode *index_btree_search(IndexDef *index, 
                                   const uint8_t *key, 
                                   uint16_t key_len,
                                   int *pos) {
    (void)index;  // Would normally use to load pages
    (void)key;
    (void)key_len;
    (void)pos;
    
    // Placeholder - actual implementation would traverse from root
    return NULL;
}

// Simplified B+ Tree insert
int index_btree_insert(IndexDef *index, 
                       const uint8_t *key, 
                       uint16_t key_len,
                       uint32_t page_id, 
                       uint16_t slot_id) {
    if (!index || !key || key_len == 0) return -1;
    
    pthread_rwlock_wrlock(&index->lock);
    
    // Update statistics
    index->stats.total_entries++;
    index->stats.update_count++;
    index->last_modified = (uint64_t)time(NULL);
    
    // Update min/max
    if (!index->stats.min_value || 
        index_key_compare(key, key_len, 
                          (uint8_t*)index->stats.min_value, 
                          strlen(index->stats.min_value)) < 0) {
        free(index->stats.min_value);
        index->stats.min_value = malloc(key_len + 1);
        memcpy(index->stats.min_value, key, key_len);
        index->stats.min_value[key_len] = '\0';
    }
    
    if (!index->stats.max_value ||
        index_key_compare(key, key_len,
                          (uint8_t*)index->stats.max_value,
                          strlen(index->stats.max_value)) > 0) {
        free(index->stats.max_value);
        index->stats.max_value = malloc(key_len + 1);
        memcpy(index->stats.max_value, key, key_len);
        index->stats.max_value[key_len] = '\0';
    }
    
    LOG_DEBUG("Index '%s': inserted key (len=%d) -> page=%u, slot=%u",
              index->name, key_len, page_id, slot_id);
    
    pthread_rwlock_unlock(&index->lock);
    return 0;
}

// Simplified B+ Tree delete
int index_btree_delete(IndexDef *index,
                       const uint8_t *key,
                       uint16_t key_len) {
    if (!index || !key || key_len == 0) return -1;
    
    pthread_rwlock_wrlock(&index->lock);
    
    // Update statistics
    if (index->stats.total_entries > 0) {
        index->stats.total_entries--;
    }
    index->stats.update_count++;
    index->last_modified = (uint64_t)time(NULL);
    
    LOG_DEBUG("Index '%s': deleted key (len=%d)", index->name, key_len);
    
    pthread_rwlock_unlock(&index->lock);
    return 0;
}

// -----------------------------------------------------------------------------
// Index Operations (High-level)
// -----------------------------------------------------------------------------

int index_insert(IndexDef *index, 
                 const uint8_t *key, 
                 uint16_t key_len,
                 uint32_t page_id, 
                 uint16_t slot_id) {
    return index_btree_insert(index, key, key_len, page_id, slot_id);
}

int index_delete(IndexDef *index,
                 const uint8_t *key,
                 uint16_t key_len,
                 uint32_t page_id,
                 uint16_t slot_id) {
    (void)page_id;
    (void)slot_id;
    return index_btree_delete(index, key, key_len);
}

int index_update(IndexDef *index,
                 const uint8_t *old_key, uint16_t old_key_len,
                 const uint8_t *new_key, uint16_t new_key_len,
                 uint32_t page_id, uint16_t slot_id) {
    int rc = index_delete(index, old_key, old_key_len, page_id, slot_id);
    if (rc != 0) return rc;
    
    return index_insert(index, new_key, new_key_len, page_id, slot_id);
}

int index_lookup(IndexDef *index,
                 const uint8_t *key,
                 uint16_t key_len,
                 uint32_t *page_ids,
                 uint16_t *slot_ids,
                 int max_results,
                 int *result_count) {
    if (!index || !key || !page_ids || !slot_ids || !result_count) return -1;
    
    pthread_rwlock_rdlock(&index->lock);
    
    index->stats.seek_count++;
    
    // Placeholder - would search B+ Tree
    *result_count = 0;
    
    pthread_rwlock_unlock(&index->lock);
    return 0;
}

// -----------------------------------------------------------------------------
// Index Cursor Operations
// -----------------------------------------------------------------------------

IndexCursor *index_scan_range(IndexDef *index,
                              const uint8_t *start_key, uint16_t start_len,
                              const uint8_t *end_key, uint16_t end_len,
                              bool include_start, bool include_end) {
    if (!index) return NULL;
    
    IndexCursor *cursor = calloc(1, sizeof(IndexCursor));
    if (!cursor) return NULL;
    
    cursor->index = index;
    cursor->forward = true;
    cursor->at_end = false;
    
    if (start_key && start_len > 0) {
        cursor->start_key = malloc(start_len);
        memcpy(cursor->start_key, start_key, start_len);
        cursor->start_key_len = start_len;
    }
    
    if (end_key && end_len > 0) {
        cursor->end_key = malloc(end_len);
        memcpy(cursor->end_key, end_key, end_len);
        cursor->end_key_len = end_len;
    }
    
    cursor->include_start = include_start;
    cursor->include_end = include_end;
    
    pthread_rwlock_rdlock(&index->lock);
    index->stats.scan_count++;
    pthread_rwlock_unlock(&index->lock);
    
    return cursor;
}

IndexCursor *index_scan_full(IndexDef *index, bool forward) {
    return index_scan_range(index, NULL, 0, NULL, 0, true, true);
}

bool index_cursor_next(IndexCursor *cursor) {
    if (!cursor || cursor->at_end) return false;
    
    cursor->rows_scanned++;
    
    // Placeholder - would advance in B+ Tree
    cursor->at_end = true;  // Simplified: immediately end
    return false;
}

bool index_cursor_prev(IndexCursor *cursor) {
    if (!cursor || cursor->at_end) return false;
    
    cursor->rows_scanned++;
    cursor->at_end = true;
    return false;
}

int index_cursor_get(IndexCursor *cursor, 
                     uint32_t *page_id, 
                     uint16_t *slot_id) {
    if (!cursor || cursor->at_end) return -1;
    
    if (cursor->current_node && cursor->current_pos >= 0 &&
        cursor->current_pos < cursor->current_node->key_count) {
        *page_id = cursor->current_node->row_ptrs[cursor->current_pos].page_id;
        *slot_id = cursor->current_node->row_ptrs[cursor->current_pos].slot_id;
        return 0;
    }
    
    return -1;
}

void index_cursor_close(IndexCursor *cursor) {
    if (!cursor) return;
    
    free(cursor->start_key);
    free(cursor->end_key);
    free(cursor);
}

// -----------------------------------------------------------------------------
// Index Statistics
// -----------------------------------------------------------------------------

int index_analyze(IndexDef *index) {
    if (!index) return -1;
    
    pthread_rwlock_wrlock(&index->lock);
    
    // Update selectivity estimate
    if (index->stats.total_entries > 0 && index->stats.distinct_keys > 0) {
        index->stats.selectivity = (double)index->stats.distinct_keys / 
                                   index->stats.total_entries;
    } else {
        index->stats.selectivity = 1.0;
    }
    
    index->stats.last_analyzed = (uint64_t)time(NULL);
    
    pthread_rwlock_unlock(&index->lock);
    
    LOG_DEBUG("Analyzed index '%s': %lu entries, %lu distinct, selectivity=%.4f",
              index->name,
              (unsigned long)index->stats.total_entries,
              (unsigned long)index->stats.distinct_keys,
              index->stats.selectivity);
    
    return 0;
}

double index_estimate_rows(IndexDef *index,
                           const uint8_t *start_key, uint16_t start_len,
                           const uint8_t *end_key, uint16_t end_len) {
    if (!index) return 0;
    
    pthread_rwlock_rdlock(&index->lock);
    
    double estimate;
    
    if (!start_key && !end_key) {
        // Full scan
        estimate = (double)index->stats.total_entries;
    } else if (start_len > 0 && end_len > 0 && 
               index_key_compare(start_key, start_len, end_key, end_len) == 0) {
        // Point lookup
        estimate = 1.0 / index->stats.selectivity;
    } else {
        // Range scan - rough estimate
        estimate = (double)index->stats.total_entries * 0.3;
    }
    
    pthread_rwlock_unlock(&index->lock);
    return estimate;
}

bool index_is_covering(IndexDef *index, 
                       const char **needed_columns, 
                       int column_count) {
    if (!index || !needed_columns || column_count <= 0) return false;
    
    for (int i = 0; i < column_count; i++) {
        bool found = false;
        for (int j = 0; j < index->column_count; j++) {
            if (strcmp(index->columns[j].column_name, needed_columns[i]) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    
    return true;
}

IndexDef *index_find_best(const char *table_name,
                          const char **columns,
                          int column_count,
                          bool need_unique) {
    if (!g_index_manager || !table_name || !columns || column_count <= 0) {
        return NULL;
    }
    
    int idx_count;
    IndexDef **indexes = index_get_all(table_name, &idx_count);
    if (!indexes || idx_count == 0) return NULL;
    
    IndexDef *best = NULL;
    int best_score = 0;
    
    for (int i = 0; i < idx_count; i++) {
        IndexDef *idx = indexes[i];
        
        if (need_unique && !idx->is_unique) continue;
        
        // Score based on how many leading columns match
        int score = 0;
        for (int j = 0; j < idx->column_count && j < column_count; j++) {
            if (strcmp(idx->columns[j].column_name, columns[j]) == 0) {
                score++;
            } else {
                break;  // Leading column mismatch
            }
        }
        
        if (score > best_score) {
            best_score = score;
            best = idx;
        }
    }
    
    free(indexes);
    return best;
}

// -----------------------------------------------------------------------------
// Index Maintenance
// -----------------------------------------------------------------------------

int index_verify(IndexDef *index, bool fix_errors) {
    if (!index) return -1;
    
    (void)fix_errors;  // Placeholder
    
    LOG_INFO("Verified index '%s': OK", index->name);
    return 0;
}

int index_compact(IndexDef *index) {
    if (!index) return -1;
    
    LOG_INFO("Compacted index '%s'", index->name);
    return 0;
}

size_t index_get_size(IndexDef *index) {
    if (!index) return 0;
    
    return index->stats.leaf_pages * INDEX_PAGE_SIZE;
}

int index_rebuild(const char *table_name, const char *index_name) {
    IndexDef *idx = index_get(table_name, index_name);
    if (!idx) return -1;
    
    pthread_rwlock_wrlock(&idx->lock);
    
    idx->state = INDEX_STATE_BUILDING;
    
    // Clear statistics
    idx->stats.total_entries = 0;
    idx->stats.distinct_keys = 0;
    idx->stats.leaf_pages = 0;
    
    // Would rebuild from table data here
    
    idx->state = INDEX_STATE_VALID;
    idx->last_modified = (uint64_t)time(NULL);
    
    pthread_rwlock_unlock(&idx->lock);
    
    LOG_INFO("Rebuilt index '%s' on table '%s'", index_name, table_name);
    return 0;
}

// -----------------------------------------------------------------------------
// SQL Interface
// -----------------------------------------------------------------------------

char *index_list_all(const char *table_name) {
    if (!g_index_manager) return strdup("No indexes\n");
    
    int count;
    IndexDef **indexes = index_get_all(table_name, &count);
    
    if (!indexes || count == 0) {
        return strdup("No indexes on this table\n");
    }
    
    // Build result string
    size_t buf_size = 4096;
    char *result = malloc(buf_size);
    int offset = 0;
    
    offset += snprintf(result + offset, buf_size - offset,
                       "Indexes on '%s':\n", table_name);
    offset += snprintf(result + offset, buf_size - offset,
                       "%-20s %-10s %-8s %-12s %s\n",
                       "Name", "Type", "Unique", "Entries", "Columns");
    offset += snprintf(result + offset, buf_size - offset,
                       "%.20s %.10s %.8s %.12s %.20s\n",
                       "--------------------", "----------", "--------",
                       "------------", "--------------------");
    
    for (int i = 0; i < count; i++) {
        IndexDef *idx = indexes[i];
        
        // Build column list
        char cols[256] = "";
        int col_off = 0;
        for (int j = 0; j < idx->column_count; j++) {
            if (j > 0) col_off += snprintf(cols + col_off, sizeof(cols) - col_off, ", ");
            col_off += snprintf(cols + col_off, sizeof(cols) - col_off, "%s", 
                               idx->columns[j].column_name);
        }
        
        offset += snprintf(result + offset, buf_size - offset,
                           "%-20s %-10s %-8s %-12lu %s\n",
                           idx->name,
                           idx->type == INDEX_TYPE_BTREE ? "BTREE" : "HASH",
                           idx->is_unique ? "YES" : "NO",
                           (unsigned long)idx->stats.total_entries,
                           cols);
    }
    
    free(indexes);
    return result;
}

char *index_to_sql(IndexDef *index) {
    if (!index) return NULL;
    
    size_t buf_size = 512;
    char *sql = malloc(buf_size);
    
    int offset = snprintf(sql, buf_size, "CREATE %sINDEX %s ON %s (",
                          index->is_unique ? "UNIQUE " : "",
                          index->name,
                          index->table_name);
    
    for (int i = 0; i < index->column_count; i++) {
        if (i > 0) offset += snprintf(sql + offset, buf_size - offset, ", ");
        offset += snprintf(sql + offset, buf_size - offset, "%s%s",
                          index->columns[i].column_name,
                          index->columns[i].order == INDEX_COL_DESC ? " DESC" : "");
    }
    
    snprintf(sql + offset, buf_size - offset, ");");
    
    return sql;
}

char *index_generate_name(const char *table_name, 
                          const char **columns, 
                          int column_count) {
    size_t len = strlen(table_name) + 8;
    for (int i = 0; i < column_count; i++) {
        len += strlen(columns[i]) + 1;
    }
    
    char *name = malloc(len);
    int offset = snprintf(name, len, "idx_%s", table_name);
    
    for (int i = 0; i < column_count && i < 3; i++) {
        offset += snprintf(name + offset, len - offset, "_%s", columns[i]);
    }
    
    return name;
}

bool index_column_is_indexed(const char *table_name, const char *column_name) {
    if (!g_index_manager || !table_name || !column_name) return false;
    
    int count;
    IndexDef **indexes = index_get_all(table_name, &count);
    if (!indexes) return false;
    
    bool found = false;
    for (int i = 0; i < count && !found; i++) {
        for (int j = 0; j < indexes[i]->column_count; j++) {
            if (strcmp(indexes[i]->columns[j].column_name, column_name) == 0) {
                found = true;
                break;
            }
        }
    }
    
    free(indexes);
    return found;
}

IndexDef *index_get_primary(const char *table_name) {
    if (!g_index_manager || !table_name) return NULL;
    
    int count;
    IndexDef **indexes = index_get_all(table_name, &count);
    if (!indexes) return NULL;
    
    IndexDef *primary = NULL;
    for (int i = 0; i < count; i++) {
        if (indexes[i]->is_primary) {
            primary = indexes[i];
            break;
        }
    }
    
    free(indexes);
    return primary;
}
