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

// Iterator
typedef void (*KVIteratorCallback)(const char *key, Value *val, void *ctx);
void kv_iterate(KVStore *store, KVIteratorCallback callback, void *ctx);

// Helper functions for namespace management
char* generate_key_table(const char *table, int id);
char* generate_key_doc(const char *collection, const char *doc_id);
char* generate_key_schema(const char *table);

#endif
