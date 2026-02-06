/**
 * InventixDB Secondary Index Support
 * 
 * Features:
 * 1. B+ Tree based secondary indexes
 * 2. Multi-column index support
 * 3. Unique and non-unique indexes
 * 4. Index statistics for query optimizer
 * 5. Index maintenance on INSERT/UPDATE/DELETE
 * 
 * Index Types:
 * - BTREE: Default, good for range queries
 * - HASH: Fast point lookups (future)
 * - FULLTEXT: Text search (future)
 */

#ifndef INVENTIX_INDEX_H
#define INVENTIX_INDEX_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define MAX_INDEXES_PER_TABLE   16      // Max indexes per table
#define MAX_INDEX_COLUMNS       8       // Max columns in composite index
#define MAX_INDEX_NAME_LEN      128     // Max index name length
#define INDEX_PAGE_SIZE         4096    // Page size for index B+ Tree
#define MAX_INDEX_KEY_SIZE      256     // Max composite key size

// -----------------------------------------------------------------------------
// Index Types
// -----------------------------------------------------------------------------

typedef enum {
    INDEX_TYPE_BTREE,           // B+ Tree index (default)
    INDEX_TYPE_HASH,            // Hash index (point lookups)
    INDEX_TYPE_FULLTEXT,        // Full-text search index
    INDEX_TYPE_SPATIAL          // Spatial index (future)
} IndexType;

typedef enum {
    INDEX_STATE_BUILDING,       // Index is being built
    INDEX_STATE_VALID,          // Index is valid and usable
    INDEX_STATE_INVALID,        // Index is corrupted/needs rebuild
    INDEX_STATE_DROPPED         // Index is marked for deletion
} IndexState;

// -----------------------------------------------------------------------------
// Index Column Definition
// -----------------------------------------------------------------------------

typedef enum {
    INDEX_COL_ASC,              // Ascending order
    INDEX_COL_DESC              // Descending order
} IndexColumnOrder;

typedef struct {
    char *column_name;
    int column_pos;             // Position in table schema
    IndexColumnOrder order;
    int key_prefix_len;         // For string columns, index only first N chars
} IndexColumn;

// -----------------------------------------------------------------------------
// Index Statistics (for Query Optimizer)
// -----------------------------------------------------------------------------

typedef struct {
    uint64_t total_entries;     // Total entries in index
    uint64_t distinct_keys;     // Number of distinct keys
    uint64_t leaf_pages;        // Number of leaf pages
    uint64_t depth;             // Tree depth
    
    // Value distribution
    char *min_value;            // Minimum key value
    char *max_value;            // Maximum key value
    double avg_key_size;        // Average key size in bytes
    
    // Usage statistics
    uint64_t scan_count;        // Number of index scans
    uint64_t seek_count;        // Number of index seeks
    uint64_t update_count;      // Number of updates
    uint64_t last_analyzed;     // Timestamp of last ANALYZE
    
    // Selectivity estimation
    double selectivity;         // Estimated selectivity (0.0-1.0)
} IndexStats;

// -----------------------------------------------------------------------------
// Index Entry (Key + Row Pointer)
// -----------------------------------------------------------------------------

typedef struct {
    uint8_t *key_data;          // Composite key data
    uint16_t key_len;           // Key length
    
    // Row location
    uint32_t page_id;           // Page containing the row
    uint16_t slot_id;           // Slot within the page
    
    // For MVCC
    uint64_t txn_id;            // Transaction that created this entry
    bool is_deleted;            // Soft delete flag
} IndexEntry;

// -----------------------------------------------------------------------------
// Index Definition
// -----------------------------------------------------------------------------

typedef struct {
    char name[MAX_INDEX_NAME_LEN];
    char *table_name;
    uint32_t index_id;          // Unique index ID
    
    IndexType type;
    IndexState state;
    bool is_unique;
    bool is_primary;            // Is this the primary key index?
    bool is_clustered;          // Is this a clustered index?
    
    // Columns
    IndexColumn columns[MAX_INDEX_COLUMNS];
    int column_count;
    
    // B+ Tree root
    uint32_t root_page;
    char *index_file;           // File storing index data
    
    // Statistics
    IndexStats stats;
    
    // Concurrency
    pthread_rwlock_t lock;
    
    // Metadata
    uint64_t created_at;
    uint64_t last_modified;
} IndexDef;

// -----------------------------------------------------------------------------
// Index B+ Tree Node
// -----------------------------------------------------------------------------

#define INDEX_BTREE_ORDER       64      // Keys per node

typedef struct IndexBTreeNode {
    bool is_leaf;
    int key_count;
    
    // For internal nodes: child pointers
    uint32_t children[INDEX_BTREE_ORDER + 1];
    
    // Keys (composite key data)
    uint8_t *keys[INDEX_BTREE_ORDER];
    uint16_t key_lengths[INDEX_BTREE_ORDER];
    
    // For leaf nodes: row pointers
    struct {
        uint32_t page_id;
        uint16_t slot_id;
    } row_ptrs[INDEX_BTREE_ORDER];
    
    // Leaf chain
    uint32_t next_leaf;
    uint32_t prev_leaf;
    
    // Node metadata
    uint32_t page_id;
} IndexBTreeNode;

// -----------------------------------------------------------------------------
// Index Cursor (for range scans)
// -----------------------------------------------------------------------------

typedef struct {
    IndexDef *index;
    IndexBTreeNode *current_node;
    int current_pos;
    
    // Range bounds
    uint8_t *start_key;
    uint16_t start_key_len;
    uint8_t *end_key;
    uint16_t end_key_len;
    bool include_start;
    bool include_end;
    
    // State
    bool at_end;
    bool forward;               // Scan direction
    
    // Statistics
    uint64_t rows_scanned;
    uint64_t pages_accessed;
} IndexCursor;

// -----------------------------------------------------------------------------
// Index Manager
// -----------------------------------------------------------------------------

typedef struct {
    IndexDef **indexes;
    int index_count;
    int index_capacity;
    
    // Global statistics
    uint64_t total_indexes;
    uint64_t total_index_memory;
    
    pthread_mutex_t manager_lock;
} IndexManager;

// -----------------------------------------------------------------------------
// Index API - Lifecycle
// -----------------------------------------------------------------------------

// Initialize/shutdown
int index_manager_init(void);
void index_manager_shutdown(void);

// Create index
IndexDef *index_create(const char *table_name,
                       const char *index_name,
                       IndexColumn *columns,
                       int column_count,
                       IndexType type,
                       bool is_unique);

// Drop index
int index_drop(const char *table_name, const char *index_name);
int index_drop_all(const char *table_name);  // Drop all indexes on table

// Rebuild index
int index_rebuild(const char *table_name, const char *index_name);

// Get index
IndexDef *index_get(const char *table_name, const char *index_name);
IndexDef **index_get_all(const char *table_name, int *count);

// -----------------------------------------------------------------------------
// Index API - Operations
// -----------------------------------------------------------------------------

// Build composite key from column values
int index_build_key(IndexDef *index, 
                    const char **col_values, 
                    uint8_t *key_buffer, 
                    uint16_t *key_len);

// Insert entry
int index_insert(IndexDef *index, 
                 const uint8_t *key, 
                 uint16_t key_len,
                 uint32_t page_id, 
                 uint16_t slot_id);

// Delete entry
int index_delete(IndexDef *index,
                 const uint8_t *key,
                 uint16_t key_len,
                 uint32_t page_id,
                 uint16_t slot_id);

// Update entry (delete old + insert new)
int index_update(IndexDef *index,
                 const uint8_t *old_key, uint16_t old_key_len,
                 const uint8_t *new_key, uint16_t new_key_len,
                 uint32_t page_id, uint16_t slot_id);

// Point lookup
int index_lookup(IndexDef *index,
                 const uint8_t *key,
                 uint16_t key_len,
                 uint32_t *page_ids,
                 uint16_t *slot_ids,
                 int max_results,
                 int *result_count);

// Range scan
IndexCursor *index_scan_range(IndexDef *index,
                              const uint8_t *start_key, uint16_t start_len,
                              const uint8_t *end_key, uint16_t end_len,
                              bool include_start, bool include_end);

// Full scan
IndexCursor *index_scan_full(IndexDef *index, bool forward);

// Cursor operations
bool index_cursor_next(IndexCursor *cursor);
bool index_cursor_prev(IndexCursor *cursor);
int index_cursor_get(IndexCursor *cursor, 
                     uint32_t *page_id, 
                     uint16_t *slot_id);
void index_cursor_close(IndexCursor *cursor);

// -----------------------------------------------------------------------------
// Index API - Statistics
// -----------------------------------------------------------------------------

// Analyze index (update statistics)
int index_analyze(IndexDef *index);

// Estimate rows for query optimizer
double index_estimate_rows(IndexDef *index,
                           const uint8_t *start_key, uint16_t start_len,
                           const uint8_t *end_key, uint16_t end_len);

// Check if index can cover query (all needed columns in index)
bool index_is_covering(IndexDef *index, 
                       const char **needed_columns, 
                       int column_count);

// Get best index for columns
IndexDef *index_find_best(const char *table_name,
                          const char **columns,
                          int column_count,
                          bool need_unique);

// -----------------------------------------------------------------------------
// Index API - Maintenance
// -----------------------------------------------------------------------------

// Check index integrity
int index_verify(IndexDef *index, bool fix_errors);

// Compact index (reclaim space)
int index_compact(IndexDef *index);

// Get index size
size_t index_get_size(IndexDef *index);

// Persist index metadata
int index_save_metadata(IndexDef *index);
int index_load_metadata(const char *table_name, const char *index_name, IndexDef **index);

// -----------------------------------------------------------------------------
// Index API - SQL Interface
// -----------------------------------------------------------------------------

// Parse CREATE INDEX statement
int index_parse_create(const char *sql, 
                       char **table_name,
                       char **index_name,
                       IndexColumn **columns,
                       int *column_count,
                       bool *is_unique);

// Generate CREATE INDEX statement from definition
char *index_to_sql(IndexDef *index);

// List indexes (for SHOW INDEXES)
char *index_list_all(const char *table_name);

// -----------------------------------------------------------------------------
// Index B+ Tree Operations (Internal)
// -----------------------------------------------------------------------------

// Node operations
IndexBTreeNode *index_btree_create_node(bool is_leaf);
void index_btree_destroy_node(IndexBTreeNode *node);

// Search
IndexBTreeNode *index_btree_search(IndexDef *index, 
                                   const uint8_t *key, 
                                   uint16_t key_len,
                                   int *pos);

// Insert with split
int index_btree_insert(IndexDef *index, 
                       const uint8_t *key, 
                       uint16_t key_len,
                       uint32_t page_id, 
                       uint16_t slot_id);

// Delete with merge
int index_btree_delete(IndexDef *index,
                       const uint8_t *key,
                       uint16_t key_len);

// Key comparison
int index_key_compare(const uint8_t *key1, uint16_t len1,
                      const uint8_t *key2, uint16_t len2);

// -----------------------------------------------------------------------------
// Utility Functions
// -----------------------------------------------------------------------------

// Generate index name from table and columns
char *index_generate_name(const char *table_name, 
                          const char **columns, 
                          int column_count);

// Check if column is indexed
bool index_column_is_indexed(const char *table_name, const char *column_name);

// Get primary key index
IndexDef *index_get_primary(const char *table_name);

#endif // INVENTIX_INDEX_H
