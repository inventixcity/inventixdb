/**
 * InventixDB Prepared Statements
 * 
 * Features:
 * 1. Parameterized Queries - SQL injection prevention
 * 2. Query Caching - Parse once, execute many times
 * 3. Query Plans - Cached execution plans
 * 4. Statement Handle Management - Server-side statement storage
 * 
 * Usage:
 *   PREPARE stmt1 AS SELECT * FROM users WHERE id = ?
 *   EXECUTE stmt1 USING (1)
 *   EXECUTE stmt1 USING (2)
 *   DEALLOCATE stmt1
 */

#ifndef INVENTIX_PREPARED_H
#define INVENTIX_PREPARED_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "parser.h"

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define MAX_PREPARED_STMTS      256     // Max prepared statements per session
#define MAX_STMT_NAME_LEN       64      // Max statement name length
#define MAX_PARAMETERS          32      // Max parameters per statement
#define MAX_PARAM_VALUE_LEN     4096    // Max parameter value length
#define STMT_CACHE_SIZE         1024    // Global statement cache size
#define QUERY_PLAN_CACHE_SIZE   512     // Query plan cache size

// -----------------------------------------------------------------------------
// Parameter Types
// -----------------------------------------------------------------------------

typedef enum {
    PARAM_TYPE_NULL,
    PARAM_TYPE_INT,
    PARAM_TYPE_FLOAT,
    PARAM_TYPE_STRING,
    PARAM_TYPE_BOOL,
    PARAM_TYPE_BLOB
} ParamType;

typedef struct {
    ParamType type;
    union {
        int64_t int_val;
        double float_val;
        bool bool_val;
        struct {
            char *data;
            size_t len;
        } string_val;
        struct {
            uint8_t *data;
            size_t len;
        } blob_val;
    } value;
    bool is_null;
} Parameter;

// -----------------------------------------------------------------------------
// Prepared Statement
// -----------------------------------------------------------------------------

typedef enum {
    STMT_STATE_INVALID,
    STMT_STATE_PREPARED,
    STMT_STATE_EXECUTING,
    STMT_STATE_COMPLETE
} StmtState;

typedef struct {
    char name[MAX_STMT_NAME_LEN];       // Statement name
    char *query_template;                // Original query with ? placeholders
    ASTNode *parsed_ast;                 // Pre-parsed AST
    
    // Parameter metadata
    int param_count;                     // Number of ? parameters
    int param_positions[MAX_PARAMETERS]; // Position of each ? in query
    ParamType param_types[MAX_PARAMETERS]; // Expected types (can be inferred)
    
    // Execution state
    StmtState state;
    uint32_t stmt_id;                    // Unique statement ID
    uint64_t prepare_time;               // When prepared (epoch ms)
    uint64_t last_execute_time;          // Last execution time
    uint64_t execution_count;            // Number of executions
    uint64_t total_exec_time_us;         // Total execution time (microseconds)
    
    // Security
    bool is_validated;                   // Has been security-validated
    char *prepared_by;                   // Username who prepared
} PreparedStatement;

// -----------------------------------------------------------------------------
// Query Plan (for Query Optimizer integration)
// -----------------------------------------------------------------------------

typedef enum {
    PLAN_OP_SEQ_SCAN,           // Sequential table scan
    PLAN_OP_INDEX_SCAN,         // Index scan
    PLAN_OP_INDEX_ONLY_SCAN,    // Covering index scan
    PLAN_OP_NESTED_LOOP,        // Nested loop join
    PLAN_OP_HASH_JOIN,          // Hash join
    PLAN_OP_MERGE_JOIN,         // Merge join
    PLAN_OP_SORT,               // Sort operation
    PLAN_OP_AGGREGATE,          // Aggregation
    PLAN_OP_FILTER,             // Filter (WHERE)
    PLAN_OP_PROJECT,            // Projection (SELECT cols)
    PLAN_OP_LIMIT,              // LIMIT clause
    PLAN_OP_HASH_AGGREGATE      // Hash-based grouping
} PlanOpType;

typedef struct QueryPlanNode {
    PlanOpType op_type;
    char *table_name;           // For scan operations
    char *index_name;           // For index operations
    
    // Cost estimates
    double estimated_rows;
    double estimated_cost;
    double actual_rows;         // Filled after execution
    double actual_time_ms;      // Filled after execution
    
    // Operation details
    char *filter_expr;          // For FILTER ops
    char **output_cols;         // Columns output by this node
    int output_col_count;
    
    // Tree structure
    struct QueryPlanNode *left;
    struct QueryPlanNode *right;
    struct QueryPlanNode *next; // For sequential ops
} QueryPlanNode;

typedef struct {
    QueryPlanNode *root;
    char *query_hash;           // Hash of original query (for caching)
    double total_cost;
    bool is_cacheable;
    time_t created_at;
    uint64_t hit_count;         // Cache hit counter
} QueryPlan;

// -----------------------------------------------------------------------------
// Statement Cache (Global)
// -----------------------------------------------------------------------------

typedef struct {
    char *query_hash;           // MD5/SHA1 hash of normalized query
    char *normalized_query;     // Normalized query string
    ASTNode *cached_ast;        // Cached AST
    QueryPlan *cached_plan;     // Cached query plan
    
    uint64_t hit_count;
    uint64_t last_access;
    size_t memory_size;         // Memory used by this entry
} CacheEntry;

typedef struct {
    CacheEntry *entries;
    int capacity;
    int count;
    uint64_t total_hits;
    uint64_t total_misses;
    size_t total_memory;
    pthread_mutex_t lock;
} StatementCache;

// -----------------------------------------------------------------------------
// Session Statement Store
// -----------------------------------------------------------------------------

typedef struct {
    PreparedStatement **statements;
    int capacity;
    int count;
    uint32_t next_stmt_id;
    
    // Statistics
    uint64_t total_prepares;
    uint64_t total_executes;
    uint64_t cache_hits;
} SessionStatementStore;

// -----------------------------------------------------------------------------
// Prepared Statement API
// -----------------------------------------------------------------------------

// Initialize/shutdown
int prepared_init(void);
void prepared_shutdown(void);

// Session-level statement management
SessionStatementStore *stmt_store_create(void);
void stmt_store_destroy(SessionStatementStore *store);

// Statement lifecycle
PreparedStatement *stmt_prepare(SessionStatementStore *store, 
                                const char *name, 
                                const char *query,
                                const char *username);
                                
int stmt_execute(SessionStatementStore *store,
                 const char *name,
                 Parameter *params,
                 int param_count,
                 void *executor_ctx,     // Execution context
                 char **result,          // Output result
                 size_t *result_len);
                 
int stmt_deallocate(SessionStatementStore *store, const char *name);
int stmt_deallocate_all(SessionStatementStore *store);

// Statement info
PreparedStatement *stmt_find(SessionStatementStore *store, const char *name);
PreparedStatement *stmt_find_by_id(SessionStatementStore *store, uint32_t stmt_id);

// Parameter binding
int stmt_bind_int(Parameter *param, int64_t value);
int stmt_bind_float(Parameter *param, double value);
int stmt_bind_string(Parameter *param, const char *value, size_t len);
int stmt_bind_bool(Parameter *param, bool value);
int stmt_bind_null(Parameter *param);
int stmt_bind_blob(Parameter *param, const uint8_t *data, size_t len);

// Query normalization (for caching)
char *stmt_normalize_query(const char *query);
char *stmt_compute_hash(const char *normalized);

// Global cache operations
int cache_get(const char *query_hash, CacheEntry **entry);
int cache_put(const char *query_hash, const char *normalized, 
              ASTNode *ast, QueryPlan *plan);
void cache_invalidate(const char *table_name);  // Invalidate on DDL
void cache_stats(uint64_t *hits, uint64_t *misses, size_t *memory);

// Query plan operations
QueryPlan *plan_create(ASTNode *ast, void *catalog);  // Forward to optimizer
void plan_destroy(QueryPlan *plan);
QueryPlanNode *plan_node_create(PlanOpType type);
void plan_node_destroy(QueryPlanNode *node);
void plan_print(QueryPlan *plan, char *buffer, size_t buflen);

// SQL Injection Prevention
bool stmt_validate_param(const Parameter *param);
char *stmt_escape_string(const char *str, size_t len);
bool stmt_is_safe_identifier(const char *identifier);

// -----------------------------------------------------------------------------
// Utility Functions
// -----------------------------------------------------------------------------

// Get parameter value as string (for substitution in legacy mode)
char *param_to_string(const Parameter *param);

// Parse USING clause: "(val1, val2, ...)"
int parse_using_clause(const char *using_str, Parameter *params, int max_params);

// Statistics
typedef struct {
    uint64_t total_prepares;
    uint64_t total_executes;
    uint64_t avg_exec_time_us;
    uint64_t cache_hits;
    uint64_t cache_misses;
    double cache_hit_ratio;
    size_t cache_memory_bytes;
    int active_statements;
} PreparedStmtStats;

void stmt_get_stats(SessionStatementStore *store, PreparedStmtStats *stats);

#endif // INVENTIX_PREPARED_H
