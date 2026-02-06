/**
 * InventixDB Query Result Processing
 * 
 * Features:
 * 1. Result set collection and buffering
 * 2. ORDER BY sorting (single and multi-column)
 * 3. LIMIT / OFFSET pagination
 * 4. Result caching for repeated queries
 * 5. Streaming result support for large datasets
 */

#ifndef INVENTIX_QUERY_RESULT_H
#define INVENTIX_QUERY_RESULT_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define RESULT_MAX_COLUMNS      64
#define RESULT_MAX_ROWS         100000
#define RESULT_CACHE_SIZE       256
#define RESULT_CACHE_TTL        300     // 5 minutes TTL
#define RESULT_SORT_BUFFER      65536   // 64KB sort buffer

// -----------------------------------------------------------------------------
// Result Row
// -----------------------------------------------------------------------------

typedef struct ResultRow {
    char **values;              // Column values (strings)
    int column_count;
    struct ResultRow *next;     // For linked list
} ResultRow;

// -----------------------------------------------------------------------------
// Result Set
// -----------------------------------------------------------------------------

typedef struct {
    char **column_names;        // Column names
    char **column_types;        // Column types
    int column_count;
    
    ResultRow *rows;            // Linked list of rows
    ResultRow *rows_tail;       // Tail for O(1) append
    int row_count;
    
    // Execution info
    double exec_time_ms;
    int rows_scanned;
    int rows_filtered;
    
    // Memory management
    size_t memory_used;
} ResultSet;

// -----------------------------------------------------------------------------
// Sort Specification
// -----------------------------------------------------------------------------

typedef struct {
    int column_index;           // Column to sort by
    char *column_name;          // Column name (for lookup)
    int descending;             // 0 = ASC, 1 = DESC
    int is_numeric;             // 1 = numeric compare, 0 = string compare
} SortSpec;

// -----------------------------------------------------------------------------
// Query Cache Entry
// -----------------------------------------------------------------------------

typedef struct {
    char *query_hash;           // Hash of query text
    char *query_text;           // Original query
    ResultSet *result;          // Cached result
    time_t created_at;          // Cache timestamp
    int hit_count;              // Access count
    bool valid;                 // Is cache valid
} QueryCacheEntry;

// -----------------------------------------------------------------------------
// Query Cache
// -----------------------------------------------------------------------------

typedef struct {
    QueryCacheEntry *entries;
    int count;
    int capacity;
    size_t total_memory;
    size_t max_memory;
    
    // Statistics
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
} QueryCache;

// -----------------------------------------------------------------------------
// Result Set Functions
// -----------------------------------------------------------------------------

// Creation and destruction
ResultSet *result_set_create(int column_count);
void result_set_free(ResultSet *rs);
void result_set_clear(ResultSet *rs);

// Add columns
int result_set_add_column(ResultSet *rs, const char *name, const char *type);
int result_set_set_columns(ResultSet *rs, char **names, char **types, int count);

// Add rows
int result_set_add_row(ResultSet *rs, char **values, int count);
ResultRow *result_set_new_row(ResultSet *rs);

// Access
int result_set_row_count(ResultSet *rs);
ResultRow *result_set_get_row(ResultSet *rs, int index);
char *result_set_get_value(ResultSet *rs, int row, int col);

// -----------------------------------------------------------------------------
// ORDER BY Functions
// -----------------------------------------------------------------------------

// Sort result set by single column
int result_set_sort(ResultSet *rs, const char *column, int descending);

// Sort result set by multiple columns
int result_set_sort_multi(ResultSet *rs, SortSpec *specs, int spec_count);

// Parse ORDER BY clause
SortSpec *parse_order_by(char **columns, int *desc_flags, int count, 
                         char **col_names, char **col_types, int col_count);

// -----------------------------------------------------------------------------
// LIMIT / OFFSET Functions
// -----------------------------------------------------------------------------

// Apply LIMIT and OFFSET to result set
int result_set_limit(ResultSet *rs, int limit, int offset);

// Create a new result set with limit/offset applied (non-destructive)
ResultSet *result_set_slice(ResultSet *rs, int limit, int offset);

// -----------------------------------------------------------------------------
// Result Printing
// -----------------------------------------------------------------------------

// Print result set as table
void result_set_print_table(ResultSet *rs, FILE *out);

// Print result set as JSON
void result_set_print_json(ResultSet *rs, FILE *out);

// Print result set as CSV
void result_set_print_csv(ResultSet *rs, FILE *out);

// -----------------------------------------------------------------------------
// Query Cache Functions
// -----------------------------------------------------------------------------

// Initialize cache
int query_cache_init(size_t max_memory);
void query_cache_shutdown(void);

// Cache operations
int query_cache_put(const char *query, ResultSet *result);
ResultSet *query_cache_get(const char *query);
void query_cache_invalidate(const char *table_name);
void query_cache_clear(void);

// Cache statistics
void query_cache_stats(uint64_t *hits, uint64_t *misses, uint64_t *evictions);

// -----------------------------------------------------------------------------
// Hash Functions
// -----------------------------------------------------------------------------

uint32_t result_hash_query(const char *query);
char *result_hash_query_str(const char *query);

#endif // INVENTIX_QUERY_RESULT_H
