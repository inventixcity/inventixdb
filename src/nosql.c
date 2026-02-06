/**
 * InventixDB NoSQL Module Implementation
 * 
 * MongoDB-like document database operations with full Hinglish support
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "nosql.h"
#include "storage.h"
#include "logger.h"

#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp
#endif

// -----------------------------------------------------------------------------
// ID Generation
// -----------------------------------------------------------------------------

static uint64_t g_id_counter = 0;

char *nosql_generate_id(void) {
    char *id = malloc(32);
    uint64_t timestamp = (uint64_t)time(NULL);
    uint32_t random = (uint32_t)rand();
    g_id_counter++;
    
    snprintf(id, 32, "%08llx%08x%04llx", 
             (unsigned long long)(timestamp & 0xFFFFFFFF),
             random,
             (unsigned long long)(g_id_counter & 0xFFFF));
    return id;
}

// -----------------------------------------------------------------------------
// Simple JSON Parser Helpers
// -----------------------------------------------------------------------------

// Extract string value for a key from JSON (very basic parser)
static char *json_get_string(const char *json, const char *key) {
    if (!json || !key) return NULL;
    
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    
    char *found = strstr(json, search);
    if (!found) return NULL;
    
    // Find the colon
    found += strlen(search);
    while (*found && (*found == ' ' || *found == ':')) found++;
    
    if (*found != '"') return NULL;
    found++; // Skip opening quote
    
    // Find closing quote
    char *end = found;
    while (*end && *end != '"') {
        if (*end == '\\' && *(end + 1)) end++; // Skip escaped char
        end++;
    }
    
    size_t len = end - found;
    char *result = malloc(len + 1);
    strncpy(result, found, len);
    result[len] = '\0';
    return result;
}

// Check if JSON has a key
static bool json_has_key(const char *json, const char *key) {
    if (!json || !key) return false;
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    return strstr(json, search) != NULL;
}

// Remove outer braces from JSON
static char *json_unwrap(const char *json) {
    if (!json) return NULL;
    
    while (*json && isspace(*json)) json++;
    if (*json == '{') json++;
    
    char *copy = strdup(json);
    size_t len = strlen(copy);
    while (len > 0 && (isspace(copy[len-1]) || copy[len-1] == '}')) {
        copy[--len] = '\0';
    }
    return copy;
}

// -----------------------------------------------------------------------------
// Document Creation/Destruction
// -----------------------------------------------------------------------------

Document *nosql_doc_create(const char *json) {
    Document *doc = calloc(1, sizeof(Document));
    if (!doc) return NULL;
    
    // Try to extract _id from JSON
    doc->_id = json_get_string(json, "_id");
    if (!doc->_id) {
        doc->_id = nosql_generate_id();
    }
    
    doc->json_data = strdup(json ? json : "{}");
    doc->version = 1;
    doc->created_at = (uint64_t)time(NULL);
    doc->updated_at = doc->created_at;
    
    return doc;
}

void nosql_doc_free(Document *doc) {
    if (!doc) return;
    free(doc->_id);
    free(doc->json_data);
    free(doc);
}

void nosql_doclist_free(DocumentList *list) {
    while (list) {
        DocumentList *next = list->next;
        nosql_doc_free(list->doc);
        free(list);
        list = next;
    }
}

char *nosql_doc_get_field(Document *doc, const char *field) {
    if (!doc || !doc->json_data) return NULL;
    return json_get_string(doc->json_data, field);
}

// -----------------------------------------------------------------------------
// Key Generation Helpers
// -----------------------------------------------------------------------------

static char *make_collection_key(const char *db, const char *collection) {
    char *key = malloc(256);
    snprintf(key, 256, "DB:%s:NOSQL_COL:%s", db, collection);
    return key;
}

static char *make_doc_key(const char *db, const char *collection, const char *doc_id) {
    char *key = malloc(256);
    snprintf(key, 256, "DB:%s:DOC:%s:%s", db, collection, doc_id);
    return key;
}

// -----------------------------------------------------------------------------
// Collection Operations
// -----------------------------------------------------------------------------

int nosql_create_collection(KVStore *store, const char *db, const char *name) {
    char *key = make_collection_key(db, name);
    
    // Check if exists
    if (kv_get(store, key)) {
        free(key);
        return -1; // Already exists
    }
    
    // Store collection metadata
    char meta[256];
    snprintf(meta, sizeof(meta), "{\"name\":\"%s\",\"created\":%llu}", 
             name, (unsigned long long)time(NULL));
    
    kv_put(store, key, meta, strlen(meta) + 1, VAL_TYPE_JSON);
    free(key);
    
    LOG_INFO("Created collection: %s.%s", db, name);
    return 0;
}

int nosql_drop_collection(KVStore *store, const char *db, const char *name) {
    char *key = make_collection_key(db, name);
    
    if (!kv_get(store, key)) {
        free(key);
        return -1; // Doesn't exist
    }
    
    kv_delete(store, key);
    free(key);
    
    // TODO: Delete all documents in collection
    LOG_INFO("Dropped collection: %s.%s", db, name);
    return 0;
}

// Callback context for listing collections
typedef struct {
    const char *db;
    char **names;
    int count;
    int capacity;
} ListCollectionsCtx;

static void list_collections_callback(const char *key, Value *val, void *ctx) {
    (void)val;
    ListCollectionsCtx *lctx = (ListCollectionsCtx *)ctx;
    
    char prefix[128];
    snprintf(prefix, sizeof(prefix), "DB:%s:NOSQL_COL:", lctx->db);
    
    if (strncmp(key, prefix, strlen(prefix)) == 0) {
        const char *name = key + strlen(prefix);
        
        if (lctx->count >= lctx->capacity) {
            lctx->capacity = lctx->capacity ? lctx->capacity * 2 : 8;
            lctx->names = realloc(lctx->names, lctx->capacity * sizeof(char*));
        }
        lctx->names[lctx->count++] = strdup(name);
    }
}

char **nosql_list_collections(KVStore *store, const char *db, int *count) {
    ListCollectionsCtx ctx = {db, NULL, 0, 0};
    kv_iterate(store, list_collections_callback, &ctx);
    *count = ctx.count;
    return ctx.names;
}

// -----------------------------------------------------------------------------
// Document CRUD Operations
// -----------------------------------------------------------------------------

Document *nosql_insert_one(KVStore *store, const char *db, 
                            const char *collection, const char *json) {
    Document *doc = nosql_doc_create(json);
    if (!doc) return NULL;
    
    // Ensure collection exists
    char *col_key = make_collection_key(db, collection);
    if (!kv_get(store, col_key)) {
        // Auto-create collection
        nosql_create_collection(store, db, collection);
    }
    free(col_key);
    
    // Build full document JSON with metadata
    char full_json[4096];
    snprintf(full_json, sizeof(full_json),
             "{\"_id\":\"%s\",\"_version\":%llu,\"_created\":%llu,\"_updated\":%llu,%s}",
             doc->_id,
             (unsigned long long)doc->version,
             (unsigned long long)doc->created_at,
             (unsigned long long)doc->updated_at,
             json_unwrap(json) ?: "");
    
    free(doc->json_data);
    doc->json_data = strdup(full_json);
    
    // Store document
    char *key = make_doc_key(db, collection, doc->_id);
    kv_put(store, key, doc->json_data, strlen(doc->json_data) + 1, VAL_TYPE_JSON);
    free(key);
    
    LOG_DEBUG("Inserted document %s into %s.%s", doc->_id, db, collection);
    return doc;
}

int nosql_insert_many(KVStore *store, const char *db, 
                       const char *collection, char **json_docs, int count) {
    int inserted = 0;
    for (int i = 0; i < count; i++) {
        Document *doc = nosql_insert_one(store, db, collection, json_docs[i]);
        if (doc) {
            inserted++;
            nosql_doc_free(doc);
        }
    }
    return inserted;
}

// Callback context for finding documents
typedef struct {
    const char *db;
    const char *collection;
    QueryCondition *query;
    DocumentList **result_head;
    DocumentList **result_tail;
    int count;
    int limit;
    int skip;
    int skipped;
} FindDocsCtx;

static bool simple_match(const char *json, const char *query_json) {
    // Very simple matching: check if all query fields match
    if (!query_json || strcmp(query_json, "{}") == 0) return true;
    
    // Parse query fields and check each
    char *query_copy = strdup(query_json);
    char *unwrapped = json_unwrap(query_copy);
    free(query_copy);
    
    if (!unwrapped || strlen(unwrapped) == 0) {
        free(unwrapped);
        return true;
    }
    
    // Simple field:value matching
    char *token = strtok(unwrapped, ",");
    bool match = true;
    
    while (token && match) {
        // Skip whitespace
        while (*token && isspace(*token)) token++;
        
        // Parse "field": "value" or "field": value
        char *colon = strchr(token, ':');
        if (colon) {
            *colon = '\0';
            char *field = token;
            char *value = colon + 1;
            
            // Clean up field name (remove quotes)
            while (*field && (*field == '"' || isspace(*field))) field++;
            char *end = field + strlen(field) - 1;
            while (end > field && (*end == '"' || isspace(*end))) *end-- = '\0';
            
            // Clean up value
            while (*value && isspace(*value)) value++;
            bool is_string = (*value == '"');
            if (is_string) value++;
            end = value + strlen(value) - 1;
            while (end > value && (*end == '"' || isspace(*end))) *end-- = '\0';
            
            // Get actual value from document
            char *doc_val = json_get_string(json, field);
            if (!doc_val) {
                match = false;
            } else if (strcmp(doc_val, value) != 0) {
                match = false;
            }
            free(doc_val);
        }
        token = strtok(NULL, ",");
    }
    
    free(unwrapped);
    return match;
}

static void find_docs_callback(const char *key, Value *val, void *ctx) {
    FindDocsCtx *fctx = (FindDocsCtx *)ctx;
    
    char prefix[256];
    snprintf(prefix, sizeof(prefix), "DB:%s:DOC:%s:", fctx->db, fctx->collection);
    
    if (strncmp(key, prefix, strlen(prefix)) != 0) return;
    if (val->type != VAL_TYPE_JSON) return;
    
    // Check limit
    if (fctx->limit > 0 && fctx->count >= fctx->limit) return;
    
    char *json = (char *)val->data;
    
    // TODO: Use proper query matching
    // For now, simple field matching
    if (!simple_match(json, NULL)) return;
    
    // Handle skip
    if (fctx->skipped < fctx->skip) {
        fctx->skipped++;
        return;
    }
    
    // Create document
    Document *doc = nosql_doc_create(json);
    
    // Add to result list
    DocumentList *node = malloc(sizeof(DocumentList));
    node->doc = doc;
    node->next = NULL;
    
    if (!*fctx->result_head) {
        *fctx->result_head = node;
        *fctx->result_tail = node;
    } else {
        (*fctx->result_tail)->next = node;
        *fctx->result_tail = node;
    }
    
    fctx->count++;
}

DocumentList *nosql_find(KVStore *store, const char *db, 
                          const char *collection, const char *query_json) {
    DocumentList *head = NULL;
    DocumentList *tail = NULL;
    
    FindDocsCtx ctx = {
        .db = db,
        .collection = collection,
        .query = NULL, // TODO: Parse query
        .result_head = &head,
        .result_tail = &tail,
        .count = 0,
        .limit = 0,
        .skip = 0,
        .skipped = 0
    };
    
    (void)query_json; // TODO: Use query for filtering
    
    kv_iterate(store, find_docs_callback, &ctx);
    
    LOG_DEBUG("Found %d documents in %s.%s", ctx.count, db, collection);
    return head;
}

Document *nosql_find_one(KVStore *store, const char *db, 
                          const char *collection, const char *query_json) {
    // Check if query is just an ID
    char *id = json_get_string(query_json, "_id");
    if (id) {
        char *key = make_doc_key(db, collection, id);
        Value *val = kv_get(store, key);
        free(key);
        free(id);
        
        if (val && val->type == VAL_TYPE_JSON) {
            return nosql_doc_create((char *)val->data);
        }
        return NULL;
    }
    
    // Otherwise, find first matching
    DocumentList *list = nosql_find(store, db, collection, query_json);
    if (!list) return NULL;
    
    Document *result = list->doc;
    list->doc = NULL; // Prevent free
    nosql_doclist_free(list);
    
    return result;
}

int nosql_update_one(KVStore *store, const char *db, 
                      const char *collection, const char *query_json,
                      const char *update_json) {
    Document *doc = nosql_find_one(store, db, collection, query_json);
    if (!doc) return 0;
    
    // Apply update (simple field merge for now)
    // TODO: Support $set, $unset, $inc, etc.
    
    doc->version++;
    doc->updated_at = (uint64_t)time(NULL);
    
    // Merge update into document
    char merged[4096];
    char *unwrapped_update = json_unwrap(update_json);
    char *unwrapped_doc = json_unwrap(doc->json_data);
    
    snprintf(merged, sizeof(merged), "{%s%s%s}",
             unwrapped_doc ? unwrapped_doc : "",
             (unwrapped_doc && unwrapped_update) ? "," : "",
             unwrapped_update ? unwrapped_update : "");
    
    free(unwrapped_update);
    free(unwrapped_doc);
    
    free(doc->json_data);
    doc->json_data = strdup(merged);
    
    // Store updated document
    char *key = make_doc_key(db, collection, doc->_id);
    kv_put(store, key, doc->json_data, strlen(doc->json_data) + 1, VAL_TYPE_JSON);
    free(key);
    
    nosql_doc_free(doc);
    return 1;
}

int nosql_update_many(KVStore *store, const char *db, 
                       const char *collection, const char *query_json,
                       const char *update_json) {
    DocumentList *list = nosql_find(store, db, collection, query_json);
    int updated = 0;
    
    DocumentList *curr = list;
    while (curr) {
        char id_query[128];
        snprintf(id_query, sizeof(id_query), "{\"_id\":\"%s\"}", curr->doc->_id);
        updated += nosql_update_one(store, db, collection, id_query, update_json);
        curr = curr->next;
    }
    
    nosql_doclist_free(list);
    return updated;
}

Document *nosql_upsert(KVStore *store, const char *db, 
                        const char *collection, const char *query_json,
                        const char *doc_json) {
    Document *existing = nosql_find_one(store, db, collection, query_json);
    
    if (existing) {
        nosql_update_one(store, db, collection, query_json, doc_json);
        nosql_doc_free(existing);
        return nosql_find_one(store, db, collection, query_json);
    } else {
        return nosql_insert_one(store, db, collection, doc_json);
    }
}

int nosql_delete_one(KVStore *store, const char *db, 
                      const char *collection, const char *query_json) {
    Document *doc = nosql_find_one(store, db, collection, query_json);
    if (!doc) return 0;
    
    char *key = make_doc_key(db, collection, doc->_id);
    kv_delete(store, key);
    free(key);
    
    nosql_doc_free(doc);
    return 1;
}

int nosql_delete_many(KVStore *store, const char *db, 
                       const char *collection, const char *query_json) {
    DocumentList *list = nosql_find(store, db, collection, query_json);
    int deleted = 0;
    
    DocumentList *curr = list;
    while (curr) {
        char id_query[128];
        snprintf(id_query, sizeof(id_query), "{\"_id\":\"%s\"}", curr->doc->_id);
        deleted += nosql_delete_one(store, db, collection, id_query);
        curr = curr->next;
    }
    
    nosql_doclist_free(list);
    return deleted;
}

// -----------------------------------------------------------------------------
// Aggregation Pipeline
// -----------------------------------------------------------------------------

AggregationPipeline *nosql_pipeline_create(void) {
    AggregationPipeline *pipeline = calloc(1, sizeof(AggregationPipeline));
    pipeline->capacity = 8;
    pipeline->stages = calloc(pipeline->capacity, sizeof(PipelineStage));
    return pipeline;
}

int nosql_pipeline_add_stage(AggregationPipeline *pipeline, 
                              PipelineStageType type, const char *spec_json) {
    if (pipeline->stage_count >= pipeline->capacity) {
        pipeline->capacity *= 2;
        pipeline->stages = realloc(pipeline->stages, 
                                   pipeline->capacity * sizeof(PipelineStage));
    }
    
    pipeline->stages[pipeline->stage_count].type = type;
    pipeline->stages[pipeline->stage_count].json_spec = strdup(spec_json);
    pipeline->stage_count++;
    
    return 0;
}

DocumentList *nosql_aggregate(KVStore *store, const char *db, 
                               const char *collection, AggregationPipeline *pipeline) {
    // Start with all documents
    DocumentList *docs = nosql_find(store, db, collection, "{}");
    
    // Apply each pipeline stage
    for (int i = 0; i < pipeline->stage_count && docs; i++) {
        PipelineStage *stage = &pipeline->stages[i];
        
        switch (stage->type) {
            case STAGE_MATCH:
                // Filter documents
                // TODO: Implement $match
                break;
                
            case STAGE_LIMIT:
                // Limit results
                {
                    int limit = atoi(stage->json_spec);
                    int count = 0;
                    DocumentList *curr = docs;
                    DocumentList *prev = NULL;
                    
                    while (curr) {
                        count++;
                        if (count > limit) {
                            if (prev) prev->next = NULL;
                            nosql_doclist_free(curr);
                            break;
                        }
                        prev = curr;
                        curr = curr->next;
                    }
                }
                break;
                
            case STAGE_SKIP:
                // Skip documents
                {
                    int skip = atoi(stage->json_spec);
                    while (skip > 0 && docs) {
                        DocumentList *next = docs->next;
                        nosql_doc_free(docs->doc);
                        free(docs);
                        docs = next;
                        skip--;
                    }
                }
                break;
                
            case STAGE_SORT:
                // Sort documents
                // TODO: Implement $sort
                break;
                
            case STAGE_COUNT:
                // Count documents
                {
                    int count = 0;
                    DocumentList *curr = docs;
                    while (curr) {
                        count++;
                        curr = curr->next;
                    }
                    
                    // Replace with single count document
                    nosql_doclist_free(docs);
                    
                    char count_json[64];
                    snprintf(count_json, sizeof(count_json), "{\"count\":%d}", count);
                    Document *count_doc = nosql_doc_create(count_json);
                    
                    docs = malloc(sizeof(DocumentList));
                    docs->doc = count_doc;
                    docs->next = NULL;
                }
                break;
                
            default:
                LOG_WARN("Unimplemented pipeline stage: %d", stage->type);
                break;
        }
    }
    
    return docs;
}

void nosql_pipeline_free(AggregationPipeline *pipeline) {
    if (!pipeline) return;
    
    for (int i = 0; i < pipeline->stage_count; i++) {
        free(pipeline->stages[i].json_spec);
    }
    free(pipeline->stages);
    free(pipeline);
}

// -----------------------------------------------------------------------------
// Query Parsing (Basic Implementation)
// -----------------------------------------------------------------------------

QueryCondition *nosql_parse_query(const char *query_json) {
    // TODO: Implement full MongoDB query parsing
    (void)query_json;
    return NULL;
}

bool nosql_query_match(Document *doc, QueryCondition *query) {
    // TODO: Implement query evaluation
    (void)doc;
    (void)query;
    return true;
}

void nosql_query_free(QueryCondition *query) {
    while (query) {
        QueryCondition *next = query->next;
        free(query->field);
        free(query->value);
        if (query->values) {
            for (int i = 0; i < query->value_count; i++) {
                free(query->values[i]);
            }
            free(query->values);
        }
        nosql_query_free(query->sub_conditions);
        free(query);
        query = next;
    }
}

// -----------------------------------------------------------------------------
// Index Operations
// -----------------------------------------------------------------------------

int nosql_create_index(KVStore *store, const char *db, 
                        const char *collection, const char *field) {
    char key[256];
    snprintf(key, sizeof(key), "DB:%s:NOSQL_IDX:%s:%s", db, collection, field);
    
    char meta[128];
    snprintf(meta, sizeof(meta), "{\"field\":\"%s\",\"created\":%llu}", 
             field, (unsigned long long)time(NULL));
    
    kv_put(store, key, meta, strlen(meta) + 1, VAL_TYPE_JSON);
    
    LOG_INFO("Created index on %s.%s.%s", db, collection, field);
    return 0;
}

int nosql_drop_index(KVStore *store, const char *db, 
                      const char *collection, const char *field) {
    char key[256];
    snprintf(key, sizeof(key), "DB:%s:NOSQL_IDX:%s:%s", db, collection, field);
    kv_delete(store, key);
    return 0;
}
