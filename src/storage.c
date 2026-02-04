#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "storage.h"

#define INITIAL_CAPACITY 1024
#define LOG_FILE "inventix.log"
#define SNAP_FILE "inventix.snap"

typedef struct {
    char *key;
    Value val;
    int occupied; // 0 or 1
} KVEntry;

struct KVStore {
    KVEntry *entries;
    int capacity;
    int count;
    FILE *log_fp;
    pthread_mutex_t lock;
};

// Utils
unsigned long hash(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

// Log Format: [1 byte Type] [4 byte KeyLen] [Key] [4 byte ValLen] [4 byte ValType] [Value]
// If Type == 0 (PUT), Type == 1 (DELETE)

void kv_append_log(KVStore *store, int op_type, const char *key, void *data, size_t size, ValueType vtype) {
    if (!store->log_fp) return;
    
    // Op: 1=PUT, 2=DELETE
    fputc(op_type, store->log_fp);
    
    int klen = strlen(key);
    fwrite(&klen, sizeof(int), 1, store->log_fp);
    fwrite(key, 1, klen, store->log_fp);

    if (op_type == 1) { // PUT
        fwrite(&size, sizeof(size_t), 1, store->log_fp);
        fwrite(&vtype, sizeof(ValueType), 1, store->log_fp);
        fwrite(data, 1, size, store->log_fp);
    }
    
    fflush(store->log_fp);
}

// Internal put without logging or locking
void kv_put_internal(KVStore *store, const char *key, void *data, size_t size, ValueType type) {
    unsigned long h = hash(key);
    int idx = h % store->capacity;
    int start_idx = idx;

    while (store->entries[idx].occupied) {
        if (strcmp(store->entries[idx].key, key) == 0) {
            free(store->entries[idx].val.data);
            if (store->entries[idx].key) free(store->entries[idx].key);
            break; 
        }
        idx = (idx + 1) % store->capacity;
        if (idx == start_idx) return; // Full
    }

    store->entries[idx].key = strdup(key);
    store->entries[idx].val.data = malloc(size);
    memcpy(store->entries[idx].val.data, data, size);
    store->entries[idx].val.size = size;
    store->entries[idx].val.type = type;
    store->entries[idx].occupied = 1;
    store->count++; 
}

void kv_put(KVStore *store, const char *key, void *data, size_t size, ValueType type) {
    pthread_mutex_lock(&store->lock);
    kv_put_internal(store, key, data, size, type);
    kv_append_log(store, 1, key, data, size, type);
    pthread_mutex_unlock(&store->lock);
}

Value* kv_get(KVStore *store, const char *key) {
    pthread_mutex_lock(&store->lock);
    unsigned long h = hash(key);
    int idx = h % store->capacity;
    int start_idx = idx;

    while (store->entries[idx].occupied) {
        if (strcmp(store->entries[idx].key, key) == 0) {
            pthread_mutex_unlock(&store->lock);
            return &store->entries[idx].val;
        }
        idx = (idx + 1) % store->capacity;
        if (idx == start_idx) {
             pthread_mutex_unlock(&store->lock);
             return NULL;
        }
    }
    pthread_mutex_unlock(&store->lock);
    return NULL;
}

void kv_delete(KVStore *store, const char *key) {
    pthread_mutex_lock(&store->lock);
    unsigned long h = hash(key);
    int idx = h % store->capacity;
    int start_idx = idx;

    while (store->entries[idx].occupied) {
        if (strcmp(store->entries[idx].key, key) == 0) {
            store->entries[idx].occupied = 0;
            kv_append_log(store, 2, key, NULL, 0, 0); // DELETE
            pthread_mutex_unlock(&store->lock);
            return;
        }
        idx = (idx + 1) % store->capacity;
        if (idx == start_idx) {
             pthread_mutex_unlock(&store->lock);
             return;
        }
    }
    pthread_mutex_unlock(&store->lock);
}

void kv_iterate(KVStore *store, KVIteratorCallback callback, void *ctx) {
    // Should lock iterate, but might be slow. Locking for safety.
    pthread_mutex_lock(&store->lock);
    for(int i=0; i<store->capacity; i++) {
        if (store->entries[i].occupied) {
            callback(store->entries[i].key, &store->entries[i].val, ctx);
        }
    }
    pthread_mutex_unlock(&store->lock);
}

void kv_recover(KVStore *store) {
    // 1. Load Snapshot
    FILE *snap = fopen(SNAP_FILE, "rb");
    if (snap) {
        printf("Loading snapshot...\n");
        int count;
        if (fread(&count, sizeof(int), 1, snap) > 0) {
            for(int i=0; i<count; i++) {
                int klen;
                fread(&klen, sizeof(int), 1, snap);
                char *key = malloc(klen + 1);
                fread(key, 1, klen, snap);
                key[klen] = 0;
                
                size_t size;
                fread(&size, sizeof(size_t), 1, snap);
                ValueType vtype;
                fread(&vtype, sizeof(ValueType), 1, snap);
                
                void *data = malloc(size);
                fread(data, 1, size, snap);
                
                kv_put_internal(store, key, data, size, vtype);
                
                free(key);
                free(data);
            }
        }
        fclose(snap);
    }

    // 2. Replay Log
    FILE *log = fopen(LOG_FILE, "rb");
    if (log) {
        printf("Replaying log...\n");
        while(1) {
            int op = fgetc(log);
            if (op == EOF) break;
            
            int klen;
            if (fread(&klen, sizeof(int), 1, log) < 1) break;
            char *key = malloc(klen + 1);
            fread(key, 1, klen, log);
            key[klen] = 0;
            
            if (op == 1) { // PUT
                size_t size;
                fread(&size, sizeof(size_t), 1, log);
                ValueType vtype;
                fread(&vtype, sizeof(ValueType), 1, log);
                void *data = malloc(size);
                fread(data, 1, size, log);
                
                kv_put_internal(store, key, data, size, vtype);
                free(data);
            } else if (op == 2) { // DELETE
                // Need internal delete logic separated or simple loop
                // We just mark occupied=0 manually for recovery
                unsigned long h = hash(key);
                int idx = h % store->capacity;
                int start_idx = idx;
                while (store->entries[idx].occupied) {
                    if (strcmp(store->entries[idx].key, key) == 0) {
                        store->entries[idx].occupied = 0;
                        break;
                    }
                    idx = (idx + 1) % store->capacity;
                    if (idx == start_idx) break;
                }
            }
            free(key);
        }
        fclose(log);
    }

    // Open log for appending
    store->log_fp = fopen(LOG_FILE, "ab");
}

void kv_snapshot(KVStore *store) {
    pthread_mutex_lock(&store->lock);
    if (store->log_fp) {
        fclose(store->log_fp);
        store->log_fp = NULL;
    }
    
    FILE *snap = fopen(SNAP_FILE, "wb");
    if (!snap) {
         pthread_mutex_unlock(&store->lock);
         return;
    }
    
    // Count occupied
    int valid_count = 0;
    for(int i=0; i<store->capacity; i++) {
        if (store->entries[i].occupied) valid_count++;
    }
    
    fwrite(&valid_count, sizeof(int), 1, snap);
    for(int i=0; i<store->capacity; i++) {
        if (store->entries[i].occupied) {
            int klen = strlen(store->entries[i].key);
            fwrite(&klen, sizeof(int), 1, snap);
            fwrite(store->entries[i].key, 1, klen, snap);
            
            size_t size = store->entries[i].val.size;
            fwrite(&size, sizeof(size_t), 1, snap);
            ValueType vtype = store->entries[i].val.type;
            fwrite(&vtype, sizeof(ValueType), 1, snap);
            fwrite(store->entries[i].val.data, 1, size, snap);
        }
    }
    fclose(snap);
    
    store->log_fp = fopen(LOG_FILE, "wb"); 
    fclose(store->log_fp);
    store->log_fp = fopen(LOG_FILE, "ab"); 
    pthread_mutex_unlock(&store->lock);
}

KVStore* kv_create() {
    KVStore *store = malloc(sizeof(KVStore));
    store->capacity = INITIAL_CAPACITY;
    store->count = 0;
    store->entries = calloc(store->capacity, sizeof(KVEntry));
    store->log_fp = NULL;
    pthread_mutex_init(&store->lock, NULL);
    
    kv_recover(store);
    return store;
}

void kv_destroy(KVStore *store) {
    if (store->log_fp) fclose(store->log_fp);
    pthread_mutex_destroy(&store->lock);
    
    for(int i=0; i<store->capacity; i++) {
        if (store->entries[i].occupied) {
            free(store->entries[i].key);
            free(store->entries[i].val.data);
        }
    }
    free(store->entries);
    free(store);
}

// Helpers
char* generate_key_table(const char *table, int id) {
    char *buf = malloc(256);
    sprintf(buf, "TBL:%s:%d", table, id);
    return buf;
}

char* generate_key_doc(const char *collection, const char *doc_id) {
    char *buf = malloc(256);
    sprintf(buf, "DOC:%s:%s", collection, doc_id);
    return buf;
}

char* generate_key_schema(const char *table) {
    char *buf = malloc(256);
    sprintf(buf, "META:TBL:%s", table);
    return buf;
}
