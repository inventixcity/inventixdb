#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "storage.h"
#include "btree.h"  // Integration: Include B+ Tree

#define INITIAL_CAPACITY 1024
#define LOG_FILE "inventix.log"
#define SNAP_FILE "inventix.snap"
#define CHECKPOINT_THRESHOLD (2 * 1024 * 1024) // 2MB

typedef struct {
    char *key;
    Value val;
    int occupied; // 0 or 1
} KVEntry;

// Forward Declaration
typedef struct Index Index;
typedef struct SkipNode SkipNode;

struct KVStore {
    KVEntry *entries;
    int capacity;
    int count;
    FILE *log_fp;
    long log_size; // Track AOF size for auto-checkpoint
    pthread_mutex_t lock;
    Index *indexes; // Linked list of active indexes
    Table *btree_table; // Integration: B+ Tree
};

// ---------------------------------------------------------
// STORAGE ENGINE WITH SKIP LIST INDEX
// ---------------------------------------------------------

#define MAX_LEVEL 6

typedef struct SkipNode {
    char *value; // Indexed Value (e.g. "Ali")
    char *pk;    // Primary Key (e.g. "TBL:users:1")
    struct SkipNode **forward;
} SkipNode;

typedef struct Index {
    char *table;
    char *column;
    int level;
    SkipNode *header;
    struct Index *next;
} Index;

struct IndexBuilderCtx {
    Index *index;
};

// Forward Declarations
void kv_add_to_index(Index *idx, const char *val, const char *pk);
SkipNode* create_skip_node(int level, const char *value, const char *pk);
void kv_snapshot_internal(KVStore *store);
void kv_recover(KVStore *store);
void kv_put_internal(KVStore *store, const char *key, void *data, size_t size, ValueType type);


// Helper Hash
unsigned long hash(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

SkipNode* create_skip_node(int level, const char *value, const char *pk) {
    SkipNode *n = malloc(sizeof(SkipNode));
    n->value = strdup(value);
    n->pk = strdup(pk);
    n->forward = malloc(sizeof(SkipNode*) * (level + 1));
    memset(n->forward, 0, sizeof(SkipNode*) * (level + 1));
    return n;
}

void kv_create_index(KVStore *store, const char *table, const char *col) {
    Index *idx = malloc(sizeof(Index));
    idx->table = strdup(table);
    idx->column = strdup(col);
    idx->level = 0;
    idx->header = create_skip_node(MAX_LEVEL, "", ""); // Sentinel
    idx->next = store->indexes;
    store->indexes = idx;
    
    // Future Note: Backfill logic removed to avoid dependency loop with executor.
    // Indexes will only contain new data inserted from this point on.
}

void kv_add_to_index(Index *idx, const char *val, const char *pk) {
    if (!idx || !val || !pk) return;
    
    SkipNode *update[MAX_LEVEL + 1];
    SkipNode *x = idx->header;
    
    for (int i = idx->level; i >= 0; i--) {
        while (x->forward[i] && strcmp(x->forward[i]->value, val) < 0) {
            x = x->forward[i];
        }
        update[i] = x;
    }
    
    int lvl = 0;
    while (rand() % 2 && lvl < MAX_LEVEL) lvl++;
    if (lvl > idx->level) {
        for (int i = idx->level + 1; i <= lvl; i++) {
            update[i] = idx->header;
        }
        idx->level = lvl;
    }
    
    x = create_skip_node(lvl, val, pk);
    for (int i = 0; i <= lvl; i++) {
        x->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = x;
    }
}

// Search Index returns PK or NULL
char* kv_search_index(KVStore *store, const char *table, const char *col, const char *val) {
    Index *idx = store->indexes;
    while(idx) {
        if (strcmp(idx->table, table) == 0 && strcmp(idx->column, col) == 0) break;
        idx = idx->next;
    }
    if (!idx) return NULL; // No index
    
    SkipNode *x = idx->header;
    for (int i = idx->level; i >= 0; i--) {
        while (x->forward[i] && strcmp(x->forward[i]->value, val) < 0) {
            x = x->forward[i];
        }
    }
    x = x->forward[0];
    
    if (x && strcmp(x->value, val) == 0) {
        return x->pk; // Found
    }
    return NULL;
}

int kv_has_index(KVStore *store, const char *table, const char *col) {
    Index *idx = store->indexes;
    while(idx) {
        if (strcmp(idx->table, table) == 0 && strcmp(idx->column, col) == 0) return 1;
        idx = idx->next;
    }
    return 0;
}

// Range Search (scan start point)
SkipNode* kv_search_index_range(KVStore *store, const char *table, const char *col, const char *val) {
    Index *idx = store->indexes;
    while(idx) {
        if (strcmp(idx->table, table) == 0 && strcmp(idx->column, col) == 0) break;
        idx = idx->next;
    }
    if (!idx) return NULL;
    
    SkipNode *x = idx->header;
    for (int i = idx->level; i >= 0; i--) {
        while (x->forward[i] && strcmp(x->forward[i]->value, val) < 0) {
            x = x->forward[i];
        }
    }
    return x->forward[0];
}

void kv_scan_index_range(KVStore *store, const char *table, const char *col, const char *start_val, int inclusive, IndexRangeCallback cb, void *ctx) {
    SkipNode *node = kv_search_index_range(store, table, col, start_val);
    while (node) {
        if (!inclusive && strcmp(node->value, start_val) == 0) {
            node = node->forward[0];
            continue;
        }
        if (!cb(node->pk, node->value, ctx)) break; // Stop if cb returns 0
        node = node->forward[0];
    }
}

void kv_update_indexes(KVStore *store, const char *table, const char *pk, const char *row_str) {
    Index *idx = store->indexes;
    while(idx) {
        if (strcmp(idx->table, table) == 0) {
            // Safe parsing
            char *copy = strdup(row_str);
            char *tok = strtok(copy, ";");
            while(tok) {
                 char *colon = strchr(tok, ':');
                 if (colon) {
                     *colon = 0;
                     if (strcmp(tok, idx->column) == 0) {
                         // Found col
                         char *val = colon + 1;
                         kv_add_to_index(idx, val, pk);
                     }
                 }
                 tok = strtok(NULL, ";");
            }
            free(copy);
        }
        idx = idx->next;
    }
}

void kv_append_log(KVStore *store, int op_type, const char *key, void *data, size_t size, ValueType vtype) {
    if (!store->log_fp) return;
    
    // Op: 1=PUT, 2=DELETE
    fputc(op_type, store->log_fp);
    store->log_size += 1;
    
    int klen = strlen(key);
    fwrite(&klen, sizeof(int), 1, store->log_fp);
    fwrite(key, 1, klen, store->log_fp);
    store->log_size += sizeof(int) + klen;

    if (op_type == 1) { // PUT
        fwrite(&size, sizeof(size_t), 1, store->log_fp);
        fwrite(&vtype, sizeof(ValueType), 1, store->log_fp);
        fwrite(data, 1, size, store->log_fp);
        store->log_size += sizeof(size_t) + sizeof(ValueType) + size;
    }
    
    fflush(store->log_fp);

    // Auto Checkpoint logic
    if (store->log_size > CHECKPOINT_THRESHOLD) {
        printf("[Storage] Auto-Checkpoint triggered (Log Size: %ld)\n", store->log_size);
        kv_snapshot_internal(store);
    }
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
    // ---------------------------------------------------------
    // INTEGRATION: Write User Data to B+ Tree
    // Check if key is "TBL:<table>:<id>" AND type is ROW
    // ---------------------------------------------------------
    if (type == VAL_TYPE_ROW && strncmp(key, "TBL:", 4) == 0) {
        const char *last_colon = strrchr(key, ':');
        if (last_colon) {
            uint32_t id = (uint32_t)atoi(last_colon + 1);
            if (id > 0) { 
                // Copy data to Row buffer (serialize)
                Row btree_row;
                memset(&btree_row, 0, sizeof(Row));
                // Cap size
                size_t copy_size = (size <= sizeof(Row)) ? size : sizeof(Row);
                memcpy(btree_row.data, data, copy_size);
                
                // New Engine Insert
                Cursor* cursor = table_find(store->btree_table, id);
                leaf_node_insert(cursor, id, &btree_row);
                free(cursor);
            }
        }
    }

    pthread_mutex_lock(&store->lock);
    kv_put_internal(store, key, data, size, type);
    kv_append_log(store, 1, key, data, size, type);
    pthread_mutex_unlock(&store->lock);
}

Value* kv_get(KVStore *store, const char *key) {
    // ---------------------------------------------------------
    // INTEGRATION: Read from B+ Tree first if applicable
    // ---------------------------------------------------------
    if (strncmp(key, "TBL:", 4) == 0) {
        const char *last_colon = strrchr(key, ':');
        if (last_colon) {
            uint32_t id = (uint32_t)atoi(last_colon + 1);
            if (id > 0) {
                // Here we could perform a read from the B+ Tree.
                // Currently, we mostly rely on the in-memory cache for speed and simplicity.
                // However, the B+ Tree is being updated on writes.
                // In a future full-disk mode, we would return data from here.
            }
        }
    }

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

void kv_snapshot_internal(KVStore *store) {
    // Internal function - assumes lock is already held
    if (store->log_fp) {
        fclose(store->log_fp);
        store->log_fp = NULL;
    }
    
    // Write tmp file first for atomic rename (Production Style)
    FILE *snap = fopen(SNAP_FILE ".tmp", "wb");
    if (!snap) {
         return;
    }
    
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
    fflush(snap);
    fclose(snap);
    
    // Atomic Swap
    remove(SNAP_FILE);
    rename(SNAP_FILE ".tmp", SNAP_FILE);
    
    // Reset Log
    store->log_fp = fopen(LOG_FILE, "wb"); 
    fclose(store->log_fp);
    store->log_fp = fopen(LOG_FILE, "ab"); 
    store->log_size = 0; // Reset Auto-Checkpoint counter
}

void kv_snapshot(KVStore *store) {
    pthread_mutex_lock(&store->lock);
    kv_snapshot_internal(store);
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
}

KVStore* kv_create() {
    KVStore *store = malloc(sizeof(KVStore));
    store->capacity = INITIAL_CAPACITY;
    store->count = 0;
    store->entries = calloc(store->capacity, sizeof(KVEntry));
    store->indexes = NULL;
    pthread_mutex_init(&store->lock, NULL);
    
    // Initialize New Engine
    store->btree_table = db_open("data.db");
    
    kv_recover(store);
    
    // Open AOF Log (AFTER recover, or reuse logic inside recover?)
    // kv_recover opens log for appending at the end. 
    // If kv_recover didn't run (fresh), we need to handle it.
    if (!store->log_fp) {
        store->log_fp = fopen(LOG_FILE, "a"); // Ensure open
        store->log_size = 0;
    }

    return store;
}

void kv_destroy(KVStore *store) {
    if (store->log_fp) fclose(store->log_fp);
    
    // Close B+ Tree
    if (store->btree_table) {
        db_close(store->btree_table);
    }
    
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
