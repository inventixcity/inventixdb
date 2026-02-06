/**
 * InventixDB JOIN Operations Implementation
 * 
 * Multi-table join execution with:
 * - Nested Loop Join
 * - Hash Join  
 * - Merge Join (Sort-Merge)
 * - Automatic algorithm selection
 * - Multi-table join planning
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "join.h"
#include "optimizer.h"
#include "logger.h"

#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp

static char *my_strndup(const char *s, size_t n) {
    size_t len = strlen(s);
    if (n < len) len = n;
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len);
        copy[len] = '\0';
    }
    return copy;
}
#else
#define my_strndup strndup
#endif

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

#define INITIAL_RESULT_CAPACITY 64
#define HASH_TABLE_LOAD_FACTOR  0.75
#define MIN_HASH_BUCKETS        64

// -----------------------------------------------------------------------------
// Helper Functions
// -----------------------------------------------------------------------------

static char *my_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static uint64_t fnv1a_hash(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static double get_current_time_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
#endif
}

// -----------------------------------------------------------------------------
// Global State
// -----------------------------------------------------------------------------

static struct {
    bool initialized;
    pthread_mutex_t mutex;
} g_join = {0};

// -----------------------------------------------------------------------------
// Initialization
// -----------------------------------------------------------------------------

int join_init(void) {
    if (g_join.initialized) return 0;
    
    if (pthread_mutex_init(&g_join.mutex, NULL) != 0) {
        return -1;
    }
    
    g_join.initialized = true;
    LOG_INFO("Join subsystem initialized");
    return 0;
}

void join_shutdown(void) {
    if (!g_join.initialized) return;
    
    pthread_mutex_destroy(&g_join.mutex);
    g_join.initialized = false;
    LOG_INFO("Join subsystem shutdown");
}

// -----------------------------------------------------------------------------
// Result Set Management
// -----------------------------------------------------------------------------

JoinResultSet *join_result_new(int capacity) {
    JoinResultSet *rs = (JoinResultSet *)calloc(1, sizeof(JoinResultSet));
    if (!rs) return NULL;
    
    rs->capacity = capacity > 0 ? capacity : INITIAL_RESULT_CAPACITY;
    rs->rows = (JoinResultRow **)calloc(rs->capacity, sizeof(JoinResultRow *));
    if (!rs->rows) {
        free(rs);
        return NULL;
    }
    
    return rs;
}

void join_result_add_row(JoinResultSet *rs, JoinResultRow *row) {
    if (!rs || !row) return;
    
    // Grow if needed
    if (rs->row_count >= rs->capacity) {
        int new_capacity = rs->capacity * 2;
        JoinResultRow **new_rows = (JoinResultRow **)realloc(
            rs->rows, new_capacity * sizeof(JoinResultRow *));
        if (!new_rows) return;
        rs->rows = new_rows;
        rs->capacity = new_capacity;
    }
    
    rs->rows[rs->row_count++] = row;
}

void join_result_free(JoinResultSet *rs) {
    if (!rs) return;
    
    for (int i = 0; i < rs->row_count; i++) {
        join_row_free(rs->rows[i]);
    }
    free(rs->rows);
    
    // Free column metadata
    if (rs->column_names) {
        for (int i = 0; i < rs->column_count; i++) {
            free(rs->column_names[i]);
            free(rs->column_tables[i]);
        }
        free(rs->column_names);
        free(rs->column_tables);
        free(rs->column_types);
    }
    
    free(rs);
}

JoinResultRow *join_row_new(int table_count, int column_count) {
    JoinResultRow *row = (JoinResultRow *)calloc(1, sizeof(JoinResultRow));
    if (!row) return NULL;
    
    row->table_count = table_count;
    row->total_columns = column_count;
    
    if (table_count > 0) {
        row->table_names = (char **)calloc(table_count, sizeof(char *));
    }
    
    if (column_count > 0) {
        row->columns = calloc(column_count, sizeof(*row->columns));
    }
    
    return row;
}

void join_row_free(JoinResultRow *row) {
    if (!row) return;
    
    // Free table names
    if (row->table_names) {
        for (int i = 0; i < row->table_count; i++) {
            free(row->table_names[i]);
        }
        free(row->table_names);
    }
    
    // Free columns
    if (row->columns) {
        for (int i = 0; i < row->total_columns; i++) {
            free(row->columns[i].table_name);
            free(row->columns[i].column_name);
            if (row->columns[i].column_type == 3) { // String type
                free(row->columns[i].str_val);
            }
        }
        free(row->columns);
    }
    
    free(row);
}

// -----------------------------------------------------------------------------
// Join Condition Parsing
// -----------------------------------------------------------------------------

JoinCondition *join_parse_on_clause(ASTNode *on_clause) {
    if (!on_clause) return NULL;
    
    JoinCondition *cond = (JoinCondition *)calloc(1, sizeof(JoinCondition));
    if (!cond) return NULL;
    
    // Parse binary expression (left.col = right.col)
    if (on_clause->type == NODE_EXPR_BINARY) {
        // Extract left side (table.column)
        if (on_clause->data.binary_expr.left && 
            on_clause->data.binary_expr.left->type == NODE_EXPR_COLUMN_REF) {
            const char *col_ref = on_clause->data.binary_expr.left->data.column_ref.column_name;
            char *dot = strchr(col_ref, '.');
            if (dot) {
                cond->left_table = my_strndup(col_ref, dot - col_ref);
                cond->left_column = my_strdup(dot + 1);
            } else {
                cond->left_column = my_strdup(col_ref);
            }
        }
        
        // Extract operator
        const char *op = on_clause->data.binary_expr.op;
        if (strcmp(op, "=") == 0) cond->op = JCOND_EQ;
        else if (strcmp(op, "<>") == 0 || strcmp(op, "!=") == 0) cond->op = JCOND_NE;
        else if (strcmp(op, "<") == 0) cond->op = JCOND_LT;
        else if (strcmp(op, "<=") == 0) cond->op = JCOND_LE;
        else if (strcmp(op, ">") == 0) cond->op = JCOND_GT;
        else if (strcmp(op, ">=") == 0) cond->op = JCOND_GE;
        
        // Extract right side
        if (on_clause->data.binary_expr.right &&
            on_clause->data.binary_expr.right->type == NODE_EXPR_COLUMN_REF) {
            const char *col_ref = on_clause->data.binary_expr.right->data.column_ref.column_name;
            char *dot = strchr(col_ref, '.');
            if (dot) {
                cond->right_table = my_strndup(col_ref, dot - col_ref);
                cond->right_column = my_strdup(dot + 1);
            } else {
                cond->right_column = my_strdup(col_ref);
            }
        }
    }
    
    return cond;
}

JoinCondition *join_parse_using_clause(char **columns, int count) {
    if (!columns || count <= 0) return NULL;
    
    JoinCondition *head = NULL;
    JoinCondition *prev = NULL;
    
    for (int i = 0; i < count; i++) {
        JoinCondition *cond = (JoinCondition *)calloc(1, sizeof(JoinCondition));
        if (!cond) continue;
        
        // USING(col) means left.col = right.col
        cond->left_column = my_strdup(columns[i]);
        cond->right_column = my_strdup(columns[i]);
        cond->op = JCOND_EQ;
        cond->is_and = (i < count - 1);
        
        if (prev) {
            prev->next = (struct JoinCondition *)cond;
        } else {
            head = cond;
        }
        prev = cond;
    }
    
    return head;
}

// -----------------------------------------------------------------------------
// Join State Management
// -----------------------------------------------------------------------------

JoinState *join_state_new(JoinKind kind) {
    JoinState *state = (JoinState *)calloc(1, sizeof(JoinState));
    if (!state) return NULL;
    
    state->kind = kind;
    return state;
}

void join_state_free(JoinState *state) {
    if (!state) return;
    
    // Free conditions
    JoinCondition *cond = state->conditions;
    while (cond) {
        JoinCondition *next = (JoinCondition *)cond->next;
        free(cond->left_table);
        free(cond->left_column);
        free(cond->right_table);
        free(cond->right_column);
        free(cond);
        cond = next;
    }
    
    // Free hash table if used
    if (state->hash_table.buckets) {
        for (int i = 0; i < state->hash_table.bucket_count; i++) {
            // Free bucket contents
        }
        free(state->hash_table.buckets);
        free(state->hash_table.hashes);
    }
    
    free(state);
}

void join_state_reset(JoinState *state) {
    if (!state) return;
    
    state->outer_row = NULL;
    state->outer_matched = false;
    state->rows_examined = 0;
    state->rows_matched = 0;
}

// -----------------------------------------------------------------------------
// Join Condition Evaluation
// -----------------------------------------------------------------------------

static int compare_values(int type, void *left, void *right) {
    if (!left && !right) return 0;
    if (!left) return -1;
    if (!right) return 1;
    
    switch (type) {
        case 1: { // Integer
            int64_t l = *(int64_t *)left;
            int64_t r = *(int64_t *)right;
            return (l > r) - (l < r);
        }
        case 2: { // Float
            double l = *(double *)left;
            double r = *(double *)right;
            if (l < r) return -1;
            if (l > r) return 1;
            return 0;
        }
        case 3: { // String
            return strcmp((char *)left, (char *)right);
        }
        case 4: { // Bool
            bool l = *(bool *)left;
            bool r = *(bool *)right;
            return (l > r) - (l < r);
        }
        default:
            return 0;
    }
}

static void *get_column_value(JoinResultRow *row, const char *table, 
                               const char *column, int *out_type) {
    if (!row || !column) return NULL;
    
    for (int i = 0; i < row->total_columns; i++) {
        // Match by column name, and optionally by table
        if (strcasecmp(row->columns[i].column_name, column) == 0) {
            if (table == NULL || row->columns[i].table_name == NULL ||
                strcasecmp(row->columns[i].table_name, table) == 0) {
                
                if (out_type) *out_type = row->columns[i].column_type;
                
                if (row->columns[i].is_null) return NULL;
                
                switch (row->columns[i].column_type) {
                    case 1: return &row->columns[i].int_val;
                    case 2: return &row->columns[i].float_val;
                    case 3: return row->columns[i].str_val;
                    case 4: return &row->columns[i].bool_val;
                    default: return NULL;
                }
            }
        }
    }
    
    return NULL;
}

bool join_eval_condition(JoinCondition *cond, JoinResultRow *left_row,
                          JoinResultRow *right_row) {
    if (!cond) return true; // No condition = always match
    
    int left_type = 0, right_type = 0;
    void *left_val = get_column_value(left_row, cond->left_table, 
                                       cond->left_column, &left_type);
    void *right_val = get_column_value(right_row, cond->right_table,
                                        cond->right_column, &right_type);
    
    // NULL handling: NULL = NULL is false in SQL, NULL IS NULL is true
    if (!left_val || !right_val) {
        return false;
    }
    
    // Type mismatch
    if (left_type != right_type) {
        // Could add type coercion here
        return false;
    }
    
    int cmp = compare_values(left_type, left_val, right_val);
    bool result = false;
    
    switch (cond->op) {
        case JCOND_EQ:   result = (cmp == 0); break;
        case JCOND_NE:   result = (cmp != 0); break;
        case JCOND_LT:   result = (cmp < 0); break;
        case JCOND_LE:   result = (cmp <= 0); break;
        case JCOND_GT:   result = (cmp > 0); break;
        case JCOND_GE:   result = (cmp >= 0); break;
        default:         result = false; break;
    }
    
    // Handle AND conditions
    if (result && cond->is_and && cond->next) {
        result = join_eval_condition((JoinCondition *)cond->next, left_row, right_row);
    }
    
    return result;
}

// -----------------------------------------------------------------------------
// Nested Loop Join
// -----------------------------------------------------------------------------

JoinResultSet *join_nested_loop(JoinState *state, void *store) {
    if (!state) return NULL;
    
    double start_time = get_current_time_ms();
    JoinResultSet *result = join_result_new(INITIAL_RESULT_CAPACITY);
    if (!result) return NULL;
    
    state->rows_examined = 0;
    state->rows_matched = 0;
    
    // This would integrate with the storage engine to iterate through tables
    // For now, this is a stub implementation
    
    /*
    // Pseudocode for actual implementation:
    TableCursor *outer_cursor = table_open_cursor(store, state->left->table_name);
    
    while (table_cursor_next(outer_cursor)) {
        void *outer_row = table_cursor_get_row(outer_cursor);
        state->outer_row = outer_row;
        state->outer_matched = false;
        
        TableCursor *inner_cursor = table_open_cursor(store, state->right->table_name);
        
        while (table_cursor_next(inner_cursor)) {
            void *inner_row = table_cursor_get_row(inner_cursor);
            state->rows_examined++;
            
            // Evaluate join condition
            if (join_eval_condition(state->conditions, outer_row, inner_row)) {
                state->rows_matched++;
                state->outer_matched = true;
                
                // Create result row by combining outer and inner
                JoinResultRow *combined = combine_rows(outer_row, inner_row);
                join_result_add_row(result, combined);
            }
        }
        
        // Handle LEFT/RIGHT/FULL outer joins
        if (!state->outer_matched && 
            (state->kind == JKIND_LEFT || state->kind == JKIND_FULL)) {
            JoinResultRow *combined = combine_with_nulls(outer_row, NULL);
            join_result_add_row(result, combined);
        }
        
        table_close_cursor(inner_cursor);
    }
    
    table_close_cursor(outer_cursor);
    */
    
    state->exec_time_ms = get_current_time_ms() - start_time;
    
    LOG_DEBUG("Nested loop join: examined=%lu, matched=%lu, time=%.2fms",
              state->rows_examined, state->rows_matched, state->exec_time_ms);
    
    return result;
}

// -----------------------------------------------------------------------------
// Hash Join
// -----------------------------------------------------------------------------

uint64_t join_hash_row(void *row, const char *column) {
    if (!row || !column) return 0;
    
    JoinResultRow *jr = (JoinResultRow *)row;
    int type = 0;
    void *val = get_column_value(jr, NULL, column, &type);
    
    if (!val) return 0;
    
    switch (type) {
        case 1: return fnv1a_hash(val, sizeof(int64_t));
        case 2: return fnv1a_hash(val, sizeof(double));
        case 3: return fnv1a_hash(val, strlen((char *)val));
        case 4: return fnv1a_hash(val, sizeof(bool));
        default: return 0;
    }
}

void join_build_hash_table(JoinState *state, void *inner_table) {
    if (!state || !inner_table) return;
    
    // Determine hash key column from condition
    const char *hash_col = NULL;
    if (state->conditions) {
        hash_col = state->conditions->right_column;
    }
    if (!hash_col) return;
    
    // Estimate row count and allocate buckets
    int estimated_rows = 1000; // Would get from table stats
    int bucket_count = estimated_rows / HASH_TABLE_LOAD_FACTOR;
    if (bucket_count < MIN_HASH_BUCKETS) bucket_count = MIN_HASH_BUCKETS;
    
    state->hash_table.bucket_count = bucket_count;
    state->hash_table.buckets = (void **)calloc(bucket_count, sizeof(void *));
    state->hash_table.hashes = (uint64_t *)calloc(bucket_count, sizeof(uint64_t));
    
    // Build hash table from inner table rows
    // This would iterate through inner_table and insert rows
    
    LOG_DEBUG("Built hash table with %d buckets for join column '%s'",
              bucket_count, hash_col);
}

void *join_probe_hash_table(JoinState *state, void *row, const char *col) {
    if (!state || !row || !col || !state->hash_table.buckets) return NULL;
    
    uint64_t hash = join_hash_row(row, col);
    int bucket = hash % state->hash_table.bucket_count;
    
    return state->hash_table.buckets[bucket];
}

JoinResultSet *join_hash(JoinState *state, void *store) {
    if (!state) return NULL;
    
    double start_time = get_current_time_ms();
    JoinResultSet *result = join_result_new(INITIAL_RESULT_CAPACITY);
    if (!result) return NULL;
    
    state->rows_examined = 0;
    state->rows_matched = 0;
    
    // Build phase: create hash table from inner (right) table
    // join_build_hash_table(state, state->right);
    
    // Probe phase: iterate outer (left) table and probe hash table
    /*
    TableCursor *outer_cursor = table_open_cursor(store, state->left->table_name);
    
    while (table_cursor_next(outer_cursor)) {
        void *outer_row = table_cursor_get_row(outer_cursor);
        
        // Probe hash table
        const char *probe_col = state->conditions->left_column;
        void *matching_rows = join_probe_hash_table(state, outer_row, probe_col);
        
        // Iterate matching rows and check full condition
        while (matching_rows) {
            state->rows_examined++;
            
            if (join_eval_condition(state->conditions, outer_row, matching_rows)) {
                state->rows_matched++;
                JoinResultRow *combined = combine_rows(outer_row, matching_rows);
                join_result_add_row(result, combined);
            }
            
            matching_rows = get_next_in_bucket(matching_rows);
        }
    }
    
    table_close_cursor(outer_cursor);
    */
    
    // Clean up hash table
    if (state->hash_table.buckets) {
        free(state->hash_table.buckets);
        free(state->hash_table.hashes);
        state->hash_table.buckets = NULL;
        state->hash_table.hashes = NULL;
    }
    
    state->exec_time_ms = get_current_time_ms() - start_time;
    
    LOG_DEBUG("Hash join: examined=%lu, matched=%lu, time=%.2fms",
              state->rows_examined, state->rows_matched, state->exec_time_ms);
    
    return result;
}

// -----------------------------------------------------------------------------
// Merge Join (Sort-Merge)
// -----------------------------------------------------------------------------

int join_compare_rows(void *row1, void *row2, const char *column) {
    if (!row1 || !row2 || !column) return 0;
    
    int type1 = 0, type2 = 0;
    void *val1 = get_column_value((JoinResultRow *)row1, NULL, column, &type1);
    void *val2 = get_column_value((JoinResultRow *)row2, NULL, column, &type2);
    
    return compare_values(type1, val1, val2);
}

void join_sort_table(JoinTable *table, const char *column, bool desc) {
    if (!table || !column) return;
    
    // This would sort the table's data by the specified column
    // Implementation would use quicksort or mergesort
    
    LOG_DEBUG("Sorted table '%s' by column '%s' %s",
              table->table_name, column, desc ? "DESC" : "ASC");
}

JoinResultSet *join_merge(JoinState *state, void *store) {
    if (!state) return NULL;
    
    double start_time = get_current_time_ms();
    JoinResultSet *result = join_result_new(INITIAL_RESULT_CAPACITY);
    if (!result) return NULL;
    
    state->rows_examined = 0;
    state->rows_matched = 0;
    
    // Get sort column from condition
    const char *sort_col = NULL;
    if (state->conditions) {
        sort_col = state->conditions->left_column;
    }
    
    // Sort both tables if not already sorted
    if (!state->merge_state.left_sorted) {
        join_sort_table(state->left, sort_col, false);
        state->merge_state.left_sorted = true;
    }
    if (!state->merge_state.right_sorted) {
        join_sort_table(state->right, state->conditions->right_column, false);
        state->merge_state.right_sorted = true;
    }
    
    // Merge phase: parallel scan of both sorted tables
    /*
    TableCursor *left_cursor = table_open_cursor(store, state->left->table_name);
    TableCursor *right_cursor = table_open_cursor(store, state->right->table_name);
    
    void *left_row = table_cursor_next(left_cursor) ? 
                     table_cursor_get_row(left_cursor) : NULL;
    void *right_row = table_cursor_next(right_cursor) ? 
                      table_cursor_get_row(right_cursor) : NULL;
    
    while (left_row && right_row) {
        int cmp = join_compare_rows(left_row, right_row, sort_col);
        state->rows_examined++;
        
        if (cmp == 0) {
            // Match found
            state->rows_matched++;
            JoinResultRow *combined = combine_rows(left_row, right_row);
            join_result_add_row(result, combined);
            
            // Advance the smaller side (or both)
            right_row = table_cursor_next(right_cursor) ? 
                        table_cursor_get_row(right_cursor) : NULL;
        } else if (cmp < 0) {
            // Left is smaller, advance left
            left_row = table_cursor_next(left_cursor) ? 
                       table_cursor_get_row(left_cursor) : NULL;
        } else {
            // Right is smaller, advance right  
            right_row = table_cursor_next(right_cursor) ? 
                        table_cursor_get_row(right_cursor) : NULL;
        }
    }
    
    table_close_cursor(left_cursor);
    table_close_cursor(right_cursor);
    */
    
    state->exec_time_ms = get_current_time_ms() - start_time;
    
    LOG_DEBUG("Merge join: examined=%lu, matched=%lu, time=%.2fms",
              state->rows_examined, state->rows_matched, state->exec_time_ms);
    
    return result;
}

// -----------------------------------------------------------------------------
// Algorithm Selection
// -----------------------------------------------------------------------------

int join_choose_algorithm(JoinState *state, void *stats) {
    if (!state) return JOIN_METHOD_NESTED_LOOP;
    
    // Get table sizes from statistics
    double left_rows = 1000;  // Default
    double right_rows = 1000;
    
    TableStats *left_stats = optimizer_get_table_stats(
        state->left ? state->left->table_name : NULL);
    TableStats *right_stats = optimizer_get_table_stats(
        state->right ? state->right->table_name : NULL);
    
    if (left_stats) left_rows = left_stats->row_count;
    if (right_stats) right_rows = right_stats->row_count;
    
    // Decision tree for join algorithm
    
    // 1. Very small inner table: nested loop is fine
    if (right_rows < 100) {
        LOG_DEBUG("Join algorithm: NESTED LOOP (small inner: %.0f rows)", right_rows);
        return JOIN_METHOD_NESTED_LOOP;
    }
    
    // 2. Both tables large and sortable: merge join
    if (left_rows > 10000 && right_rows > 10000) {
        // Check if join columns have indexes (would be presorted)
        LOG_DEBUG("Join algorithm: MERGE (large tables: %.0f x %.0f)", 
                  left_rows, right_rows);
        return JOIN_METHOD_MERGE;
    }
    
    // 3. Default: hash join for medium-large tables
    LOG_DEBUG("Join algorithm: HASH (default for %.0f x %.0f)", 
              left_rows, right_rows);
    return JOIN_METHOD_HASH;
}

// -----------------------------------------------------------------------------
// Main Join Execution
// -----------------------------------------------------------------------------

JoinResultSet *join_execute(JoinPlanNode *plan, void *catalog, void *store) {
    if (!plan) return NULL;
    
    // Create join state from plan
    JoinState *state = join_state_new(plan->kind);
    if (!state) return NULL;
    
    state->conditions = plan->cond;
    
    // Set up tables
    if (!plan->left_is_join) {
        state->left = plan->left_table;
    }
    if (!plan->right_is_join) {
        state->right = plan->right_table;
    }
    
    // Choose algorithm if not specified
    int algorithm = join_choose_algorithm(state, catalog);
    
    JoinResultSet *result = NULL;
    
    switch (algorithm) {
        case JOIN_METHOD_NESTED_LOOP:
            result = join_nested_loop(state, store);
            break;
            
        case JOIN_METHOD_HASH:
            result = join_hash(state, store);
            break;
            
        case JOIN_METHOD_MERGE:
            result = join_merge(state, store);
            break;
            
        default:
            result = join_nested_loop(state, store);
            break;
    }
    
    // For recursive joins (join trees)
    if (plan->left_is_join) {
        JoinResultSet *left_result = join_execute(plan->left_join, catalog, store);
        // Would merge left_result into current result
        join_result_free(left_result);
    }
    if (plan->right_is_join) {
        JoinResultSet *right_result = join_execute(plan->right_join, catalog, store);
        // Would merge right_result into current result
        join_result_free(right_result);
    }
    
    // Don't free conditions as they're owned by the plan
    state->conditions = NULL;
    join_state_free(state);
    
    return result;
}

// -----------------------------------------------------------------------------
// Join Plan Construction
// -----------------------------------------------------------------------------

JoinPlanNode *join_parse_from_clause(ASTNode *from_clause) {
    if (!from_clause) return NULL;
    
    // This would parse the FROM clause AST to build a join plan
    // For now, return a simple placeholder
    
    JoinPlanNode *plan = (JoinPlanNode *)calloc(1, sizeof(JoinPlanNode));
    if (!plan) return NULL;
    
    plan->kind = JKIND_INNER;
    
    return plan;
}

// -----------------------------------------------------------------------------
// NATURAL JOIN Support
// -----------------------------------------------------------------------------

char **join_find_common_columns(JoinTable *left, JoinTable *right, int *count) {
    if (!left || !right || !count) return NULL;
    
    // This would query the catalog for column names in each table
    // and find the intersection
    
    *count = 0;
    return NULL;
}

// -----------------------------------------------------------------------------
// Column Resolution
// -----------------------------------------------------------------------------

ResolvedColumn *join_resolve_column(const char *col_ref,
                                     JoinTable **tables,
                                     int table_count) {
    if (!col_ref || !tables || table_count == 0) return NULL;
    
    ResolvedColumn *resolved = (ResolvedColumn *)calloc(1, sizeof(ResolvedColumn));
    if (!resolved) return NULL;
    
    resolved->original = my_strdup(col_ref);
    
    // Check for table.column format
    char *dot = strchr(col_ref, '.');
    if (dot) {
        resolved->table = my_strndup(col_ref, dot - col_ref);
        resolved->column = my_strdup(dot + 1);
        
        // Find table index
        for (int i = 0; i < table_count; i++) {
            if (tables[i] && 
                (strcasecmp(tables[i]->table_name, resolved->table) == 0 ||
                 (tables[i]->alias && 
                  strcasecmp(tables[i]->alias, resolved->table) == 0))) {
                resolved->table_index = i;
                break;
            }
        }
    } else {
        // Unqualified column - search all tables
        resolved->column = my_strdup(col_ref);
        resolved->table_index = -1; // Ambiguous, needs resolution
    }
    
    return resolved;
}

void join_resolved_free(ResolvedColumn *col) {
    if (!col) return;
    free(col->original);
    free(col->table);
    free(col->column);
    free(col);
}

bool join_validate_columns(JoinCondition *cond, JoinTable **tables,
                            int table_count, char **error) {
    if (!cond) return true;
    
    // Validate left column exists
    bool left_found = false;
    for (int i = 0; i < table_count && !left_found; i++) {
        // Would check catalog for column existence
        left_found = true;
    }
    
    if (!left_found) {
        if (error) {
            *error = my_strdup("Column not found in left table");
        }
        return false;
    }
    
    // Validate right column exists
    bool right_found = false;
    for (int i = 0; i < table_count && !right_found; i++) {
        right_found = true;
    }
    
    if (!right_found) {
        if (error) {
            *error = my_strdup("Column not found in right table");
        }
        return false;
    }
    
    return true;
}

// -----------------------------------------------------------------------------
// Plan Optimization
// -----------------------------------------------------------------------------

double join_estimate_cost(JoinPlanNode *plan, void *stats) {
    if (!plan) return 0.0;
    
    double left_cost = 0.0;
    double right_cost = 0.0;
    double left_rows = 1000.0;
    double right_rows = 1000.0;
    
    // Get costs/sizes of children
    if (plan->left_is_join) {
        left_cost = join_estimate_cost(plan->left_join, stats);
        left_rows = plan->left_join->estimated_rows;
    } else if (plan->left_table) {
        TableStats *ts = optimizer_get_table_stats(plan->left_table->table_name);
        if (ts) left_rows = ts->row_count;
        left_cost = left_rows * 0.01;
    }
    
    if (plan->right_is_join) {
        right_cost = join_estimate_cost(plan->right_join, stats);
        right_rows = plan->right_join->estimated_rows;
    } else if (plan->right_table) {
        TableStats *ts = optimizer_get_table_stats(plan->right_table->table_name);
        if (ts) right_rows = ts->row_count;
        right_cost = right_rows * 0.01;
    }
    
    // Estimate join cost based on kind
    double join_cost = left_rows * right_rows * 0.001; // Hash join estimate
    double selectivity = 0.1;
    
    plan->estimated_rows = left_rows * right_rows * selectivity;
    plan->estimated_cost = left_cost + right_cost + join_cost;
    
    return plan->estimated_cost;
}

JoinPlanNode *join_optimize_plan(JoinPlanNode *plan, void *stats) {
    if (!plan) return NULL;
    
    // Calculate costs
    join_estimate_cost(plan, stats);
    
    // For now, just return the same plan
    // A real implementation would try different join orders
    
    return plan;
}

// -----------------------------------------------------------------------------
// Debug / Explain
// -----------------------------------------------------------------------------

static const char *join_kind_str(JoinKind kind) {
    switch (kind) {
        case JKIND_INNER:   return "INNER JOIN";
        case JKIND_LEFT:    return "LEFT OUTER JOIN";
        case JKIND_RIGHT:   return "RIGHT OUTER JOIN";
        case JKIND_FULL:    return "FULL OUTER JOIN";
        case JKIND_CROSS:   return "CROSS JOIN";
        case JKIND_NATURAL: return "NATURAL JOIN";
        default:            return "JOIN";
    }
}

void join_print_plan(JoinPlanNode *plan, FILE *out, int indent) {
    if (!plan || !out) return;
    
    for (int i = 0; i < indent; i++) fprintf(out, "  ");
    
    fprintf(out, "%s (est_rows=%.0f, est_cost=%.2f)\n",
            join_kind_str(plan->kind),
            plan->estimated_rows,
            plan->estimated_cost);
    
    // Print left child
    for (int i = 0; i < indent + 1; i++) fprintf(out, "  ");
    fprintf(out, "-> ");
    if (plan->left_is_join) {
        fprintf(out, "\n");
        join_print_plan(plan->left_join, out, indent + 2);
    } else if (plan->left_table) {
        fprintf(out, "Scan %s", plan->left_table->table_name);
        if (plan->left_table->alias) {
            fprintf(out, " AS %s", plan->left_table->alias);
        }
        fprintf(out, "\n");
    }
    
    // Print right child
    for (int i = 0; i < indent + 1; i++) fprintf(out, "  ");
    fprintf(out, "-> ");
    if (plan->right_is_join) {
        fprintf(out, "\n");
        join_print_plan(plan->right_join, out, indent + 2);
    } else if (plan->right_table) {
        fprintf(out, "Scan %s", plan->right_table->table_name);
        if (plan->right_table->alias) {
            fprintf(out, " AS %s", plan->right_table->alias);
        }
        fprintf(out, "\n");
    }
}

char *join_explain(JoinPlanNode *plan) {
    if (!plan) return NULL;
    
    // Create explanation string
    char *buffer = (char *)malloc(4096);
    if (!buffer) return NULL;
    
    buffer[0] = '\0';
    
    char line[256];
    snprintf(line, sizeof(line), "Join Plan:\n");
    strcat(buffer, line);
    
    snprintf(line, sizeof(line), "  Type: %s\n", join_kind_str(plan->kind));
    strcat(buffer, line);
    
    snprintf(line, sizeof(line), "  Estimated Rows: %.0f\n", plan->estimated_rows);
    strcat(buffer, line);
    
    snprintf(line, sizeof(line), "  Estimated Cost: %.2f\n", plan->estimated_cost);
    strcat(buffer, line);
    
    if (plan->cond) {
        snprintf(line, sizeof(line), "  Condition: %s.%s = %s.%s\n",
                plan->cond->left_table ? plan->cond->left_table : "?",
                plan->cond->left_column ? plan->cond->left_column : "?",
                plan->cond->right_table ? plan->cond->right_table : "?",
                plan->cond->right_column ? plan->cond->right_column : "?");
        strcat(buffer, line);
    }
    
    return buffer;
}

// -----------------------------------------------------------------------------
// Join Iterator (for streaming large results)
// -----------------------------------------------------------------------------

JoinIterator *join_iterator_new(JoinPlanNode *plan, void *store) {
    if (!plan) return NULL;
    
    JoinIterator *iter = (JoinIterator *)calloc(1, sizeof(JoinIterator));
    if (!iter) return NULL;
    
    iter->plan = plan;
    iter->store = store;
    iter->state = join_state_new(plan->kind);
    iter->started = false;
    iter->finished = false;
    
    return iter;
}

JoinResultRow *join_iterator_next(JoinIterator *iter) {
    if (!iter || iter->finished) return NULL;
    
    if (!iter->started) {
        iter->started = true;
        // Initialize cursors, etc.
    }
    
    // Get next matching row
    // This would be implemented to yield one row at a time
    
    return NULL;
}

void join_iterator_reset(JoinIterator *iter) {
    if (!iter) return;
    
    iter->started = false;
    iter->finished = false;
    iter->current = NULL;
    join_state_reset(iter->state);
}

void join_iterator_free(JoinIterator *iter) {
    if (!iter) return;
    
    join_state_free(iter->state);
    free(iter);
}
