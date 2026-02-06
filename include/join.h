/**
 * InventixDB JOIN Operations
 * 
 * Features:
 * 1. INNER JOIN, LEFT JOIN, RIGHT JOIN, FULL JOIN, CROSS JOIN
 * 2. Multiple join algorithms (Nested Loop, Hash, Merge)
 * 3. Multi-table joins with optimization
 * 4. Join predicate evaluation
 * 
 * Supported SQL Syntax:
 * - SELECT ... FROM t1 JOIN t2 ON t1.col = t2.col
 * - SELECT ... FROM t1, t2 WHERE t1.col = t2.col
 * - SELECT ... FROM t1 LEFT JOIN t2 ON condition
 * - SELECT ... FROM t1 CROSS JOIN t2
 */

#ifndef INVENTIX_JOIN_H
#define INVENTIX_JOIN_H

#include <stdint.h>
#include <stdbool.h>
#include "parser.h"

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define MAX_JOIN_TABLES         8
#define MAX_JOIN_COLUMNS        16
#define HASH_JOIN_BUCKETS       1024
#define MERGE_SORT_BUFFER       65536   // 64KB sort buffer

// -----------------------------------------------------------------------------
// Join Types (also in optimizer.h)
// -----------------------------------------------------------------------------

typedef enum JoinKind {
    JKIND_INNER,        // INNER JOIN - matching rows only
    JKIND_LEFT,         // LEFT [OUTER] JOIN
    JKIND_RIGHT,        // RIGHT [OUTER] JOIN
    JKIND_FULL,         // FULL [OUTER] JOIN
    JKIND_CROSS,        // CROSS JOIN (Cartesian product)
    JKIND_NATURAL       // NATURAL JOIN (auto-match columns)
} JoinKind;

// -----------------------------------------------------------------------------
// Join Condition
// -----------------------------------------------------------------------------

typedef enum JoinCondOp {
    JCOND_EQ,           // =
    JCOND_NE,           // <>, !=
    JCOND_LT,           // <
    JCOND_LE,           // <=
    JCOND_GT,           // >
    JCOND_GE,           // >=
    JCOND_LIKE,         // LIKE pattern matching
    JCOND_IN            // IN clause
} JoinCondOp;

typedef struct {
    char *left_table;       // Left table alias or name
    char *left_column;      // Left column name
    JoinCondOp op;          // Comparison operator
    char *right_table;      // Right table alias or name
    char *right_column;     // Right column name
    
    // For complex conditions
    bool is_and;            // AND with next condition
    struct JoinCondition *next;
} JoinCondition;

// -----------------------------------------------------------------------------
// Table Reference for JOINs
// -----------------------------------------------------------------------------

typedef struct JoinTable {
    char *table_name;       // Original table name
    char *alias;            // Optional alias (AS name)
    
    // Derived table info
    bool is_subquery;
    struct ASTNode *subquery;
    
    // For result iteration
    void *scan_handle;      // Table scan handle
    void *current_row;      // Current row data
    bool at_eof;            // End of table
} JoinTable;

// -----------------------------------------------------------------------------
// Join Execution State
// -----------------------------------------------------------------------------

typedef struct {
    JoinKind kind;
    JoinCondition *conditions;
    int condition_count;
    
    JoinTable *left;
    JoinTable *right;
    
    // For nested loop
    void *outer_row;
    bool outer_matched;
    
    // For hash join
    struct {
        void **buckets;
        int bucket_count;
        uint64_t *hashes;
        int row_count;
    } hash_table;
    
    // For merge join
    struct {
        bool left_sorted;
        bool right_sorted;
        void *left_buffer;
        void *right_buffer;
        int left_pos;
        int right_pos;
    } merge_state;
    
    // Statistics
    uint64_t rows_examined;
    uint64_t rows_matched;
    double exec_time_ms;
} JoinState;

// -----------------------------------------------------------------------------
// Multi-table Join Plan
// -----------------------------------------------------------------------------

typedef struct JoinPlanNode {
    JoinKind kind;
    JoinCondition *cond;
    
    // Either a table or another join
    bool left_is_join;
    union {
        JoinTable *left_table;
        struct JoinPlanNode *left_join;
    };
    
    bool right_is_join;
    union {
        JoinTable *right_table;
        struct JoinPlanNode *right_join;
    };
    
    // Cost estimation
    double estimated_rows;
    double estimated_cost;
} JoinPlanNode;

// -----------------------------------------------------------------------------
// Result Row (combined from multiple tables)
// -----------------------------------------------------------------------------

typedef struct {
    int table_count;
    char **table_names;
    
    // Columns
    int total_columns;
    struct {
        char *table_name;
        char *column_name;
        int column_type;
        union {
            int64_t int_val;
            double float_val;
            char *str_val;
            bool bool_val;
        };
        bool is_null;
    } *columns;
} JoinResultRow;

typedef struct {
    JoinResultRow **rows;
    int row_count;
    int capacity;
    
    // Column metadata
    int column_count;
    char **column_names;
    char **column_tables;
    int *column_types;
} JoinResultSet;

// -----------------------------------------------------------------------------
// Join Executor API
// -----------------------------------------------------------------------------

// Initialize join subsystem
int join_init(void);
void join_shutdown(void);

// Parse JOIN clauses from AST
JoinPlanNode *join_parse_from_clause(ASTNode *from_clause);
JoinCondition *join_parse_on_clause(ASTNode *on_clause);
JoinCondition *join_parse_using_clause(char **columns, int count);

// Execute joins
JoinResultSet *join_execute(JoinPlanNode *plan, void *catalog, void *store);

// Individual join algorithms
JoinResultSet *join_nested_loop(JoinState *state, void *store);
JoinResultSet *join_hash(JoinState *state, void *store);
JoinResultSet *join_merge(JoinState *state, void *store);

// Automatic algorithm selection
int join_choose_algorithm(JoinState *state, void *stats);

// Join result management
JoinResultSet *join_result_new(int capacity);
void join_result_add_row(JoinResultSet *rs, JoinResultRow *row);
void join_result_free(JoinResultSet *rs);
JoinResultRow *join_row_new(int table_count, int column_count);
void join_row_free(JoinResultRow *row);

// Join condition evaluation
bool join_eval_condition(JoinCondition *cond, 
                          JoinResultRow *left_row,
                          JoinResultRow *right_row);

// Hash join helpers
uint64_t join_hash_row(void *row, const char *column);
void join_build_hash_table(JoinState *state, void *inner_table);
void *join_probe_hash_table(JoinState *state, void *row, const char *col);

// Merge join helpers
int join_compare_rows(void *row1, void *row2, const char *column);
void join_sort_table(JoinTable *table, const char *column, bool desc);

// NATURAL JOIN helpers
char **join_find_common_columns(JoinTable *left, JoinTable *right, int *count);

// Plan optimization
JoinPlanNode *join_optimize_plan(JoinPlanNode *plan, void *stats);
double join_estimate_cost(JoinPlanNode *plan, void *stats);

// Debug/Explain
void join_print_plan(JoinPlanNode *plan, FILE *out, int indent);
char *join_explain(JoinPlanNode *plan);

// -----------------------------------------------------------------------------
// Join State Management
// -----------------------------------------------------------------------------

JoinState *join_state_new(JoinKind kind);
void join_state_free(JoinState *state);
void join_state_reset(JoinState *state);

// Iterator interface for large result sets
typedef struct {
    JoinState *state;
    JoinPlanNode *plan;
    void *store;
    
    bool started;
    bool finished;
    JoinResultRow *current;
} JoinIterator;

JoinIterator *join_iterator_new(JoinPlanNode *plan, void *store);
JoinResultRow *join_iterator_next(JoinIterator *iter);
void join_iterator_reset(JoinIterator *iter);
void join_iterator_free(JoinIterator *iter);

// -----------------------------------------------------------------------------
// Column Resolution
// -----------------------------------------------------------------------------

typedef struct {
    char *original;         // As written in query (table.column or column)
    char *table;            // Resolved table name
    char *column;           // Column name
    int table_index;        // Index in join's table list
    int column_index;       // Column index in table
    int type;               // Data type
} ResolvedColumn;

ResolvedColumn *join_resolve_column(const char *col_ref,
                                     JoinTable **tables,
                                     int table_count);
void join_resolved_free(ResolvedColumn *col);

// Validate column references in join conditions
bool join_validate_columns(JoinCondition *cond,
                            JoinTable **tables,
                            int table_count,
                            char **error);

#endif // INVENTIX_JOIN_H
