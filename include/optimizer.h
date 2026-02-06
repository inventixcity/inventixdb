/**
 * InventixDB Query Optimizer
 * 
 * Features:
 * 1. Cost-based query optimization
 * 2. Query plan generation and caching
 * 3. Statistics-based cardinality estimation
 * 4. Index selection and access path optimization
 * 5. Join order optimization
 * 
 * Optimization Phases:
 * 1. Parse → AST
 * 2. Analyze → Annotated AST with type info
 * 3. Optimize → Query Plan with cost estimates
 * 4. Execute → Results
 */

#ifndef INVENTIX_OPTIMIZER_H
#define INVENTIX_OPTIMIZER_H

#include <stdint.h>
#include <stdbool.h>
#include "parser.h"

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define MAX_JOIN_TABLES         8       // Max tables in a single query
#define MAX_PREDICATES          32      // Max WHERE predicates
#define PLAN_CACHE_SIZE         256     // Query plan cache size
#define DEFAULT_ROWS_ESTIMATE   1000    // Default table row estimate
#define INDEX_SCAN_COST         1.0     // Cost multiplier for index scan
#define SEQ_SCAN_COST           4.0     // Cost multiplier for sequential scan
#define JOIN_COST_FACTOR        2.0     // Cost multiplier for joins

// -----------------------------------------------------------------------------
// Statistics
// -----------------------------------------------------------------------------

typedef struct {
    char *table_name;
    uint64_t row_count;
    uint64_t page_count;
    double avg_row_size;
    
    // Column statistics
    struct {
        char *column_name;
        uint64_t distinct_values;
        double null_fraction;
        char *min_value;
        char *max_value;
        double avg_width;
    } *column_stats;
    int column_count;
    
    uint64_t last_analyzed;
} TableStats;

// -----------------------------------------------------------------------------
// Access Path Types
// -----------------------------------------------------------------------------

typedef enum {
    ACCESS_SEQ_SCAN,            // Full table scan
    ACCESS_INDEX_SCAN,          // Index scan with row lookup
    ACCESS_INDEX_ONLY_SCAN,     // Covering index scan
    ACCESS_BITMAP_SCAN,         // Bitmap index scan
    ACCESS_TID_SCAN             // Direct row ID scan
} AccessPathType;

typedef struct {
    AccessPathType type;
    char *table_name;
    char *index_name;           // NULL for seq scan
    
    // Cost estimates
    double startup_cost;        // Cost before first row
    double total_cost;          // Total cost to get all rows
    double rows_estimate;       // Estimated rows returned
    
    // Selectivity
    double selectivity;         // 0.0 to 1.0
    
    // For index scans
    char **scan_keys;           // Column names used as scan keys
    int scan_key_count;
} AccessPath;

// -----------------------------------------------------------------------------
// Join Types
// -----------------------------------------------------------------------------

typedef enum {
    JOIN_INNER,
    JOIN_LEFT,
    JOIN_RIGHT,
    JOIN_FULL,
    JOIN_CROSS
} JoinType;

typedef enum {
    JOIN_METHOD_NESTED_LOOP,    // Simple nested loop
    JOIN_METHOD_HASH,           // Hash join
    JOIN_METHOD_MERGE,          // Sort-merge join
    JOIN_METHOD_INDEX_NL        // Index nested loop
} JoinMethod;

typedef struct {
    JoinType type;
    JoinMethod method;
    
    char *left_table;
    char *right_table;
    char *join_column_left;
    char *join_column_right;
    
    // Cost estimates
    double cost;
    double rows_estimate;
} JoinPath;

// -----------------------------------------------------------------------------
// Query Plan Node Types
// -----------------------------------------------------------------------------

typedef enum {
    PLAN_SEQ_SCAN,
    PLAN_INDEX_SCAN,
    PLAN_INDEX_ONLY_SCAN,
    PLAN_NESTED_LOOP,
    PLAN_HASH_JOIN,
    PLAN_MERGE_JOIN,
    PLAN_SORT,
    PLAN_AGGREGATE,
    PLAN_GROUP,
    PLAN_FILTER,
    PLAN_PROJECT,
    PLAN_LIMIT,
    PLAN_RESULT              // Single row result
} PlanNodeType;

// -----------------------------------------------------------------------------
// Optimized Query Plan
// -----------------------------------------------------------------------------

typedef struct OptPlanNode {
    PlanNodeType type;
    
    // Cost info
    double startup_cost;
    double total_cost;
    double rows_estimate;
    int width;                  // Avg row width in bytes
    
    // Execution info
    double actual_rows;         // Filled after execution
    double actual_time_ms;      // Filled after execution
    int loops;                  // Number of iterations
    
    // Node-specific data
    union {
        struct {
            char *table_name;
            char *filter;       // WHERE clause for this scan
        } scan;
        
        struct {
            char *table_name;
            char *index_name;
            char *index_cond;   // Index condition
            char *filter;       // Additional filter
        } index_scan;
        
        struct {
            JoinType join_type;
            JoinMethod join_method;
            char *join_cond;
        } join;
        
        struct {
            char **sort_keys;
            bool *sort_desc;
            int key_count;
        } sort;
        
        struct {
            char **group_keys;
            int key_count;
            char **agg_funcs;   // "COUNT", "SUM", etc.
            char **agg_cols;
            int agg_count;
        } aggregate;
        
        struct {
            char **output_cols;
            int col_count;
        } project;
        
        struct {
            int64_t limit_count;
            int64_t offset;
        } limit;
    } data;
    
    // Tree structure
    struct OptPlanNode *left;   // Outer/left child
    struct OptPlanNode *right;  // Inner/right child
} OptPlanNode;

typedef struct {
    OptPlanNode *root;
    char *query_text;
    char *plan_hash;
    
    // Total costs
    double total_cost;
    double total_rows;
    
    // Execution stats
    double planning_time_ms;
    double execution_time_ms;
    
    // Cache info
    bool is_cached;
    uint64_t cache_hits;
    time_t created_at;
} OptQueryPlan;

// -----------------------------------------------------------------------------
// Optimizer Context
// -----------------------------------------------------------------------------

typedef struct {
    // Table catalog access
    TableStats **table_stats;
    int table_count;
    
    // Current query info
    char **tables_in_query;
    int num_tables;
    
    // Predicate info
    struct {
        char *column;
        char *op;
        char *value;
        double selectivity;
    } predicates[MAX_PREDICATES];
    int predicate_count;
    
    // Join info
    JoinPath joins[MAX_JOIN_TABLES - 1];
    int join_count;
    
    // Cost parameters
    double seq_page_cost;
    double random_page_cost;
    double cpu_tuple_cost;
    double cpu_index_tuple_cost;
    double cpu_operator_cost;
    
    // Enable/disable optimizations
    bool enable_indexscan;
    bool enable_hashjoin;
    bool enable_mergejoin;
    bool enable_nestloop;
} OptimizerContext;

// -----------------------------------------------------------------------------
// Optimizer API
// -----------------------------------------------------------------------------

// Initialize/shutdown
int optimizer_init(void);
void optimizer_shutdown(void);

// Main optimization entry point
OptQueryPlan *optimizer_optimize(ASTNode *ast, void *catalog);

// Plan execution (wrapper that uses optimized plan)
int optimizer_execute(OptQueryPlan *plan, void *store, void *ctx, FILE *out);

// Plan management
void optimizer_plan_free(OptQueryPlan *plan);
char *optimizer_plan_explain(OptQueryPlan *plan, bool analyze);
void optimizer_plan_print(OptQueryPlan *plan, FILE *out);

// Statistics management
TableStats *optimizer_get_table_stats(const char *table_name);
int optimizer_analyze_table(const char *table_name, void *store);
void optimizer_update_stats(const char *table_name, TableStats *stats);

// Cost estimation
double optimizer_estimate_scan_cost(const char *table_name, 
                                    const char *filter,
                                    bool use_index,
                                    const char *index_name);

double optimizer_estimate_join_cost(JoinMethod method,
                                    double left_rows,
                                    double right_rows,
                                    double selectivity);

// Selectivity estimation
double optimizer_estimate_selectivity(const char *table_name,
                                       const char *column,
                                       const char *op,
                                       const char *value);

// Access path selection
AccessPath *optimizer_choose_access_path(const char *table_name,
                                          const char *filter,
                                          char **available_indexes,
                                          int index_count);

// Join optimization
JoinPath *optimizer_choose_join_method(const char *left_table,
                                        const char *right_table,
                                        const char *join_cond,
                                        JoinType join_type);

int optimizer_find_best_join_order(char **tables, int table_count,
                                    JoinPath *result_joins);

// Plan caching
OptQueryPlan *optimizer_cache_get(const char *query_hash);
void optimizer_cache_put(const char *query_hash, OptQueryPlan *plan);
void optimizer_cache_invalidate(const char *table_name);

// -----------------------------------------------------------------------------
// Plan Node Construction
// -----------------------------------------------------------------------------

OptPlanNode *plan_node_new(PlanNodeType type);
void plan_node_free(OptPlanNode *node);

OptPlanNode *plan_make_seq_scan(const char *table, const char *filter);
OptPlanNode *plan_make_index_scan(const char *table, const char *index,
                                   const char *index_cond, const char *filter);
OptPlanNode *plan_make_nested_loop(OptPlanNode *outer, OptPlanNode *inner,
                                    JoinType type, const char *cond);
OptPlanNode *plan_make_hash_join(OptPlanNode *outer, OptPlanNode *inner,
                                  JoinType type, const char *cond);
OptPlanNode *plan_make_sort(OptPlanNode *child, char **keys, 
                             bool *desc, int count);
OptPlanNode *plan_make_aggregate(OptPlanNode *child, char **group_keys,
                                  int group_count, char **agg_funcs,
                                  char **agg_cols, int agg_count);
OptPlanNode *plan_make_project(OptPlanNode *child, char **cols, int count);
OptPlanNode *plan_make_limit(OptPlanNode *child, int64_t limit, int64_t offset);

// -----------------------------------------------------------------------------
// EXPLAIN Output Formatting
// -----------------------------------------------------------------------------

#define EXPLAIN_FORMAT_TEXT     0
#define EXPLAIN_FORMAT_JSON     1
#define EXPLAIN_FORMAT_XML      2

char *optimizer_explain_format(OptQueryPlan *plan, int format, bool analyze);

#endif // INVENTIX_OPTIMIZER_H
