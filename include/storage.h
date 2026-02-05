#ifndef INVENTIX_STORAGE_H
#define INVENTIX_STORAGE_H

#include <stddef.h>
#include <pthread.h>

// Storage Types
typedef enum {
    VAL_TYPE_ROW,
    VAL_TYPE_JSON,
    VAL_TYPE_SCHEMA
} ValueType;

// Generic Value Wrapper
typedef struct {
    void *data;
    size_t size;
    ValueType type;
} Value;

// Key-Value Store Interface
typedef struct KVStore KVStore;

unsigned long hash(const char *str);

KVStore* kv_create();
void kv_put(KVStore *store, const char *key, void *data, size_t size, ValueType type);
Value* kv_get(KVStore *store, const char *key);
void kv_delete(KVStore *store, const char *key);
void kv_destroy(KVStore *store);

// Persistence
void kv_snapshot(KVStore *store);
void kv_recover(KVStore *store);

// Indexing
void kv_create_index(KVStore *store, const char *table, const char *col);
int kv_has_index(KVStore *store, const char *table, const char *col);
char* kv_search_index(KVStore *store, const char *table, const char *col, const char *val); // Returns PK
// Internal helper to update index on insert (exposed for executor)
void kv_update_indexes(KVStore *store, const char *table, const char *pk, const char *json_row);

// For Range Queries
typedef struct SkipNode SkipNode;
typedef int (*IndexRangeCallback)(const char *pk, const char *val, void *ctx); // Ret 0 to stop
void kv_scan_index_range(KVStore *store, const char *table, const char *col, const char *start_val, int inclusive, IndexRangeCallback cb, void *ctx);

// Iterator
typedef void (*KVIteratorCallback)(const char *key, Value *val, void *ctx);
void kv_iterate(KVStore *store, KVIteratorCallback callback, void *ctx);

// Helper functions for namespace management
char* generate_key_table(const char *table, int id);
char* generate_key_doc(const char *collection, const char *doc_id);
char* generate_key_schema(const char *table);

#endif
