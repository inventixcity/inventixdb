/**
 * InventixDB Advanced Storage Engine
 * 
 * This module provides enhanced storage capabilities:
 * 1. Slotted Pages - Variable-length row storage
 * 2. Page Compression - LZ4-style compression
 * 3. Columnar Storage - Analytics-optimized column store
 * 4. LSM Tree - Write-optimized storage engine
 * 
 * Storage Engine Selection:
 * - ROW_STORE: Traditional row-based storage (default)
 * - COLUMN_STORE: Columnar storage for analytics
 * - LSM_STORE: Log-structured merge tree for writes
 */

#ifndef INVENTIX_STORAGE_ENGINE_H
#define INVENTIX_STORAGE_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

#define PAGE_SIZE_SE        4096    // Standard page size
#define MAX_SLOT_COUNT      256     // Max slots per page
#define COMPRESSION_THRESHOLD 512   // Compress pages larger than this
#define MAX_COLUMN_COUNT    64      // Max columns per table
#define MEMTABLE_SIZE       (1024 * 1024)  // 1MB memtable for LSM
#define LSM_LEVEL_COUNT     4       // Number of LSM levels
#define SSTABLE_MAX_SIZE    (4 * 1024 * 1024)  // 4MB per SSTable

// -----------------------------------------------------------------------------
// Storage Engine Types
// -----------------------------------------------------------------------------

typedef enum {
    ENGINE_ROW_STORE,       // Traditional row-based storage
    ENGINE_COLUMN_STORE,    // Columnar storage for analytics
    ENGINE_LSM_TREE         // Log-structured merge tree
} StorageEngineType;

typedef enum {
    COMPRESS_NONE,          // No compression
    COMPRESS_LZ4,           // LZ4 fast compression
    COMPRESS_RLE            // Run-length encoding (for columnar)
} CompressionType;

// -----------------------------------------------------------------------------
// Slotted Page Structure
// -----------------------------------------------------------------------------

/**
 * Slotted Page Layout:
 * 
 * +------------------+
 * |   Page Header    |  (16 bytes)
 * +------------------+
 * |   Slot Directory |  (4 bytes per slot)
 * |   [slot 0]       |
 * |   [slot 1]       |
 * |   ...            |
 * +------------------+
 * |   Free Space     |
 * |                  |
 * +------------------+
 * |   Record Data    |  (grows downward)
 * |   [record n]     |
 * |   [record n-1]   |
 * |   ...            |
 * +------------------+
 */

typedef struct {
    uint16_t offset;        // Offset from page start
    uint16_t length;        // Length of record (0 = deleted)
} SlotEntry;

typedef struct {
    uint32_t page_id;           // Page identifier
    uint16_t slot_count;        // Number of slots
    uint16_t free_space_start;  // Start of free space
    uint16_t free_space_end;    // End of free space (where records begin)
    uint16_t flags;             // Page flags (compressed, etc.)
    uint32_t checksum;          // CRC32 checksum
} SlottedPageHeader;

typedef struct {
    SlottedPageHeader header;
    SlotEntry slots[MAX_SLOT_COUNT];
    uint8_t data[PAGE_SIZE_SE - sizeof(SlottedPageHeader)];
} SlottedPage;

// Page flags
#define PAGE_FLAG_COMPRESSED    0x0001
#define PAGE_FLAG_LEAF          0x0002
#define PAGE_FLAG_INTERNAL      0x0004
#define PAGE_FLAG_OVERFLOW      0x0008

// -----------------------------------------------------------------------------
// Variable-Length Record
// -----------------------------------------------------------------------------

/**
 * Variable-Length Record Format:
 * 
 * +------------------+
 * |  Record Header   |  (8 bytes)
 * +------------------+
 * |  Null Bitmap     |  (ceil(col_count/8) bytes)
 * +------------------+
 * |  Field Offsets   |  (2 bytes per variable field)
 * +------------------+
 * |  Fixed Fields    |  (inline fixed-size data)
 * +------------------+
 * |  Variable Fields |  (variable-length data)
 * +------------------+
 */

typedef struct {
    uint16_t record_length;     // Total record length
    uint16_t field_count;       // Number of fields
    uint32_t txn_id;            // Transaction ID (for MVCC)
} VarRecordHeader;

typedef struct {
    VarRecordHeader header;
    uint8_t *null_bitmap;       // Bitmap for NULL values
    uint16_t *var_offsets;      // Offsets to variable fields
    uint8_t *data;              // Actual field data
} VarRecord;

// -----------------------------------------------------------------------------
// Compression Module
// -----------------------------------------------------------------------------

typedef struct {
    CompressionType type;
    size_t original_size;
    size_t compressed_size;
    uint8_t *data;
} CompressedBlock;

// LZ4-style compression functions
size_t compress_lz4(const uint8_t *src, size_t src_len, 
                    uint8_t *dst, size_t dst_capacity);
size_t decompress_lz4(const uint8_t *src, size_t src_len,
                      uint8_t *dst, size_t dst_capacity);

// RLE compression for columnar data
size_t compress_rle(const uint8_t *src, size_t src_len,
                    uint8_t *dst, size_t dst_capacity);
size_t decompress_rle(const uint8_t *src, size_t src_len,
                      uint8_t *dst, size_t dst_capacity);

// -----------------------------------------------------------------------------
// Columnar Storage
// -----------------------------------------------------------------------------

/**
 * Column Store Layout:
 * 
 * Each column stored separately:
 * +------------------+
 * |  Column Header   |
 * +------------------+
 * |  Min Value       |  (for statistics)
 * |  Max Value       |
 * +------------------+
 * |  Null Bitmap     |
 * +------------------+
 * |  Compressed Data |
 * +------------------+
 */

typedef enum {
    COL_TYPE_INT32,
    COL_TYPE_INT64,
    COL_TYPE_FLOAT,
    COL_TYPE_DOUBLE,
    COL_TYPE_STRING,
    COL_TYPE_BOOL
} ColumnDataType;

typedef struct {
    char name[64];
    ColumnDataType type;
    uint32_t row_count;
    uint32_t null_count;
    CompressionType compression;
    
    // Statistics for query optimization
    union {
        int64_t int_min;
        double float_min;
    } min_val;
    
    union {
        int64_t int_max;
        double float_max;
    } max_val;
} ColumnHeader;

typedef struct {
    ColumnHeader header;
    uint8_t *null_bitmap;
    uint8_t *data;
    size_t data_size;
} ColumnChunk;

typedef struct {
    char table_name[128];
    uint32_t column_count;
    uint32_t row_count;
    ColumnChunk *columns;
} ColumnStore;

// -----------------------------------------------------------------------------
// LSM Tree Structures
// -----------------------------------------------------------------------------

/**
 * LSM Tree Architecture:
 * 
 * Write Path:
 *   Memtable (in-memory sorted) -> WAL
 *        |
 *        v (flush when full)
 *   Level 0 SSTables (unsorted, overlapping)
 *        |
 *        v (compaction)
 *   Level 1 SSTables (sorted, non-overlapping)
 *        |
 *        v
 *   Level N SSTables
 */

typedef struct {
    char *key;
    uint8_t *value;
    size_t value_len;
    uint64_t timestamp;
    bool deleted;           // Tombstone marker
} LSMEntry;

typedef struct MemTableNode {
    LSMEntry entry;
    struct MemTableNode *left;
    struct MemTableNode *right;
    int height;             // For AVL balancing
} MemTableNode;

typedef struct {
    MemTableNode *root;
    size_t size;            // Current size in bytes
    size_t count;           // Number of entries
    uint64_t min_timestamp;
    uint64_t max_timestamp;
} MemTable;

// SSTable structure (Sorted String Table)
typedef struct {
    char filename[256];
    uint64_t min_key_hash;
    uint64_t max_key_hash;
    uint32_t entry_count;
    size_t file_size;
    
    // Bloom filter for fast negative lookups
    uint8_t *bloom_filter;
    size_t bloom_size;
    
    // Index for binary search
    uint64_t *key_index;
    uint32_t *offset_index;
    uint32_t index_count;
} SSTable;

typedef struct {
    int level;
    SSTable *tables;
    int table_count;
    int table_capacity;
} LSMLevel;

typedef struct {
    MemTable *memtable;
    MemTable *immutable;    // Being flushed
    LSMLevel levels[LSM_LEVEL_COUNT];
    char *base_path;
    uint64_t next_sstable_id;
    
    // WAL for durability
    FILE *wal_file;
    size_t wal_size;
    
    // Background compaction
    bool compacting;
} LSMTree;

// -----------------------------------------------------------------------------
// Slotted Page Functions
// -----------------------------------------------------------------------------

void slotted_page_init(SlottedPage *page, uint32_t page_id);
int slotted_page_insert(SlottedPage *page, const uint8_t *data, size_t len, uint16_t *out_slot);
int slotted_page_get(SlottedPage *page, uint16_t slot, uint8_t *out_data, size_t *out_len);
int slotted_page_delete(SlottedPage *page, uint16_t slot);
int slotted_page_update(SlottedPage *page, uint16_t slot, const uint8_t *data, size_t len);
size_t slotted_page_free_space(SlottedPage *page);
void slotted_page_compact(SlottedPage *page);
bool slotted_page_is_compressed(SlottedPage *page);
int slotted_page_compress(SlottedPage *page);
int slotted_page_decompress(SlottedPage *page);

// -----------------------------------------------------------------------------
// Variable-Length Record Functions
// -----------------------------------------------------------------------------

VarRecord* var_record_create(int field_count);
void var_record_free(VarRecord *record);
int var_record_set_int(VarRecord *record, int field_idx, int64_t value);
int var_record_set_float(VarRecord *record, int field_idx, double value);
int var_record_set_string(VarRecord *record, int field_idx, const char *value);
int var_record_set_null(VarRecord *record, int field_idx);
int64_t var_record_get_int(VarRecord *record, int field_idx);
double var_record_get_float(VarRecord *record, int field_idx);
const char* var_record_get_string(VarRecord *record, int field_idx);
bool var_record_is_null(VarRecord *record, int field_idx);
size_t var_record_serialize(VarRecord *record, uint8_t *buffer, size_t capacity);
VarRecord* var_record_deserialize(const uint8_t *buffer, size_t len);

// -----------------------------------------------------------------------------
// Column Store Functions
// -----------------------------------------------------------------------------

ColumnStore* column_store_create(const char *table_name, int column_count);
void column_store_free(ColumnStore *store);
int column_store_add_column(ColumnStore *store, const char *name, ColumnDataType type);
int column_store_insert_row(ColumnStore *store, void **values, bool *nulls);
int column_store_get_column(ColumnStore *store, int col_idx, void **out_data, 
                            uint32_t *out_count, uint8_t **out_nulls);
int column_store_flush(ColumnStore *store, const char *path);
ColumnStore* column_store_load(const char *path);

// Columnar aggregations (optimized)
int64_t column_sum_int(ColumnChunk *chunk);
double column_avg_int(ColumnChunk *chunk);
int64_t column_min_int(ColumnChunk *chunk);
int64_t column_max_int(ColumnChunk *chunk);
uint32_t column_count_non_null(ColumnChunk *chunk);

// -----------------------------------------------------------------------------
// LSM Tree Functions
// -----------------------------------------------------------------------------

LSMTree* lsm_create(const char *base_path);
void lsm_close(LSMTree *tree);
int lsm_put(LSMTree *tree, const char *key, const uint8_t *value, size_t len);
int lsm_get(LSMTree *tree, const char *key, uint8_t **out_value, size_t *out_len);
int lsm_delete(LSMTree *tree, const char *key);
int lsm_flush_memtable(LSMTree *tree);
int lsm_compact(LSMTree *tree, int level);
void lsm_recover(LSMTree *tree);

// MemTable functions
MemTable* memtable_create(void);
void memtable_free(MemTable *mt);
int memtable_put(MemTable *mt, const char *key, const uint8_t *value, size_t len);
LSMEntry* memtable_get(MemTable *mt, const char *key);
int memtable_delete(MemTable *mt, const char *key);

// SSTable functions
SSTable* sstable_create(const char *filename);
void sstable_free(SSTable *sst);
int sstable_write(SSTable *sst, MemTable *mt);
LSMEntry* sstable_get(SSTable *sst, const char *key);
bool sstable_may_contain(SSTable *sst, const char *key);

// Bloom filter
void bloom_add(uint8_t *filter, size_t size, const char *key);
bool bloom_may_contain(uint8_t *filter, size_t size, const char *key);

// -----------------------------------------------------------------------------
// Storage Engine Manager
// -----------------------------------------------------------------------------

typedef struct {
    StorageEngineType type;
    void *engine;           // Points to appropriate engine struct
    CompressionType compression;
    bool auto_compact;
    
    // Statistics
    uint64_t reads;
    uint64_t writes;
    uint64_t bytes_written;
    uint64_t bytes_read;
} StorageEngine;

StorageEngine* storage_engine_create(StorageEngineType type, const char *path);
void storage_engine_close(StorageEngine *engine);
int storage_engine_put(StorageEngine *engine, const char *key, 
                       const uint8_t *value, size_t len);
int storage_engine_get(StorageEngine *engine, const char *key,
                       uint8_t **out_value, size_t *out_len);
int storage_engine_delete(StorageEngine *engine, const char *key);
void storage_engine_set_compression(StorageEngine *engine, CompressionType type);
void storage_engine_print_stats(StorageEngine *engine);

#endif // INVENTIX_STORAGE_ENGINE_H
