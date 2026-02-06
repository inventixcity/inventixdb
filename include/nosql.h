/**
 * InventixDB NoSQL Module - MongoDB-like Operations
 * 
 * Features:
 * - Document CRUD operations
 * - Aggregation pipeline
 * - Query operators ($eq, $gt, $lt, $in, etc.)
 * - Projection
 * - Indexes on document fields
 * 
 * Hinglish Commands:
 * - SANGRAH BANAO users;           -- Create collection
 * - DASTAVEZ DAL users {...};      -- Insert document
 * - DASTAVEZ KHOJO users {...};    -- Find documents
 * - DASTAVEZ BADLO users {...};    -- Update documents
 * - DASTAVEZ HATAO users {...};    -- Delete documents
 * - IKATHA users [...];            -- Aggregate pipeline
 */

#ifndef INVENTIX_NOSQL_H
#define INVENTIX_NOSQL_H

#include <stdint.h>
#include <stdbool.h>
#include "storage.h"

// -----------------------------------------------------------------------------
// Document Types
// -----------------------------------------------------------------------------

typedef struct {
    char *_id;              // Document ID (auto-generated if not provided)
    char *json_data;        // Raw JSON string
    uint64_t version;       // Document version (for MVCC)
    uint64_t created_at;
    uint64_t updated_at;
} Document;

typedef struct DocumentList {
    Document *doc;
    struct DocumentList *next;
} DocumentList;

// -----------------------------------------------------------------------------
// Query Operators (MongoDB-style)
// -----------------------------------------------------------------------------

typedef enum {
    OP_EQ,          // $eq - equals
    OP_NE,          // $ne - not equals
    OP_GT,          // $gt - greater than
    OP_GTE,         // $gte - greater than or equal
    OP_LT,          // $lt - less than
    OP_LTE,         // $lte - less than or equal
    OP_IN,          // $in - in array
    OP_NIN,         // $nin - not in array
    OP_EXISTS,      // $exists - field exists
    OP_TYPE,        // $type - field type
    OP_REGEX,       // $regex - regex match
    OP_AND,         // $and - logical AND
    OP_OR,          // $or - logical OR
    OP_NOT,         // $not - logical NOT
    OP_NOR          // $nor - logical NOR
} QueryOperator;

typedef struct QueryCondition {
    char *field;
    QueryOperator op;
    char *value;            // For simple comparisons
    char **values;          // For $in, $nin
    int value_count;
    struct QueryCondition *sub_conditions;  // For logical operators
    int sub_count;
    struct QueryCondition *next;
} QueryCondition;

// -----------------------------------------------------------------------------
// Aggregation Pipeline Stages (MongoDB-style)
// -----------------------------------------------------------------------------

typedef enum {
    STAGE_MATCH,        // $match - filter documents
    STAGE_PROJECT,      // $project - reshape documents
    STAGE_GROUP,        // $group - group by field
    STAGE_SORT,         // $sort - sort documents
    STAGE_LIMIT,        // $limit - limit results
    STAGE_SKIP,         // $skip - skip documents
    STAGE_UNWIND,       // $unwind - deconstruct array
    STAGE_LOOKUP,       // $lookup - left outer join
    STAGE_COUNT,        // $count - count documents
    STAGE_ADD_FIELDS,   // $addFields - add computed fields
    STAGE_OUT           // $out - write to collection
} PipelineStageType;

typedef struct {
    PipelineStageType type;
    char *json_spec;        // Stage specification in JSON
} PipelineStage;

typedef struct {
    PipelineStage *stages;
    int stage_count;
    int capacity;
} AggregationPipeline;

// -----------------------------------------------------------------------------
// Collection Operations
// -----------------------------------------------------------------------------

/**
 * Create a new collection
 * 
 * Hinglish: SANGRAH BANAO <name>
 * SQL: CREATE COLLECTION <name>
 */
int nosql_create_collection(KVStore *store, const char *db, const char *name);

/**
 * Drop a collection
 * 
 * Hinglish: SANGRAH GIRAO <name>
 * SQL: DROP COLLECTION <name>
 */
int nosql_drop_collection(KVStore *store, const char *db, const char *name);

/**
 * List all collections in database
 */
char **nosql_list_collections(KVStore *store, const char *db, int *count);

// -----------------------------------------------------------------------------
// Document CRUD Operations
// -----------------------------------------------------------------------------

/**
 * Insert one document
 * 
 * Hinglish: DASTAVEZ DAL <collection> <json>
 * SQL: INSERT INTO <collection> DOCUMENT <json>
 */
Document *nosql_insert_one(KVStore *store, const char *db, 
                            const char *collection, const char *json);

/**
 * Insert multiple documents
 */
int nosql_insert_many(KVStore *store, const char *db, 
                       const char *collection, char **json_docs, int count);

/**
 * Find documents matching query
 * 
 * Hinglish: DASTAVEZ KHOJO <collection> <query>
 * SQL: FIND <collection> WHERE <query>
 */
DocumentList *nosql_find(KVStore *store, const char *db, 
                          const char *collection, const char *query_json);

/**
 * Find one document by ID or query
 * 
 * Hinglish: DASTAVEZ MANGWAO <collection> <id>
 */
Document *nosql_find_one(KVStore *store, const char *db, 
                          const char *collection, const char *query_json);

/**
 * Update documents matching query
 * 
 * Hinglish: DASTAVEZ BADLO <collection> <query> <update>
 * SQL: UPDATE <collection> SET <update> WHERE <query>
 */
int nosql_update_one(KVStore *store, const char *db, 
                      const char *collection, const char *query_json,
                      const char *update_json);

int nosql_update_many(KVStore *store, const char *db, 
                       const char *collection, const char *query_json,
                       const char *update_json);

/**
 * Upsert - Update or insert
 * 
 * Hinglish: DASTAVEZ DAL_YA_BADLO <collection> <query> <doc>
 */
Document *nosql_upsert(KVStore *store, const char *db, 
                        const char *collection, const char *query_json,
                        const char *doc_json);

/**
 * Delete documents
 * 
 * Hinglish: DASTAVEZ HATAO <collection> <query>
 * SQL: DELETE FROM <collection> WHERE <query>
 */
int nosql_delete_one(KVStore *store, const char *db, 
                      const char *collection, const char *query_json);

int nosql_delete_many(KVStore *store, const char *db, 
                       const char *collection, const char *query_json);

// -----------------------------------------------------------------------------
// Aggregation Pipeline
// -----------------------------------------------------------------------------

/**
 * Create aggregation pipeline
 */
AggregationPipeline *nosql_pipeline_create(void);

/**
 * Add stage to pipeline
 */
int nosql_pipeline_add_stage(AggregationPipeline *pipeline, 
                              PipelineStageType type, const char *spec_json);

/**
 * Execute aggregation pipeline
 * 
 * Hinglish: IKATHA <collection> [...]
 * SQL: AGGREGATE <collection> PIPELINE [...]
 */
DocumentList *nosql_aggregate(KVStore *store, const char *db, 
                               const char *collection, AggregationPipeline *pipeline);

/**
 * Free pipeline
 */
void nosql_pipeline_free(AggregationPipeline *pipeline);

// -----------------------------------------------------------------------------
// Document Helpers
// -----------------------------------------------------------------------------

/**
 * Create document from JSON
 */
Document *nosql_doc_create(const char *json);

/**
 * Free document
 */
void nosql_doc_free(Document *doc);

/**
 * Free document list
 */
void nosql_doclist_free(DocumentList *list);

/**
 * Get field value from document JSON
 */
char *nosql_doc_get_field(Document *doc, const char *field);

/**
 * Set field value in document
 */
int nosql_doc_set_field(Document *doc, const char *field, const char *value);

/**
 * Generate unique document ID
 */
char *nosql_generate_id(void);

// -----------------------------------------------------------------------------
// Query Parsing
// -----------------------------------------------------------------------------

/**
 * Parse MongoDB-style query JSON into conditions
 */
QueryCondition *nosql_parse_query(const char *query_json);

/**
 * Evaluate query against document
 */
bool nosql_query_match(Document *doc, QueryCondition *query);

/**
 * Free query conditions
 */
void nosql_query_free(QueryCondition *query);

// -----------------------------------------------------------------------------
// Index Operations
// -----------------------------------------------------------------------------

/**
 * Create index on document field
 */
int nosql_create_index(KVStore *store, const char *db, 
                        const char *collection, const char *field);

/**
 * Drop index
 */
int nosql_drop_index(KVStore *store, const char *db, 
                      const char *collection, const char *field);

#endif // INVENTIX_NOSQL_H
