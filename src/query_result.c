/**
 * InventixDB Query Result Processing Implementation
 * 
 * Provides:
 * - Result set management
 * - ORDER BY sorting
 * - LIMIT / OFFSET pagination
 * - Query result caching
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <ctype.h>

#include "query_result.h"
#include "logger.h"

#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp
// Windows compatibility for strcasestr (case-insensitive strstr)
static char *strcasestr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++; n++;
        }
        if (!*n) return (char *)haystack;
    }
    return NULL;
}
#endif

// -----------------------------------------------------------------------------
// Global Query Cache
// -----------------------------------------------------------------------------

static QueryCache g_cache = {0};
static pthread_mutex_t g_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_cache_initialized = false;

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

static int is_numeric_string(const char *s) {
    if (!s || !*s) return 0;
    if (*s == '-' || *s == '+') s++;
    int has_dot = 0;
    while (*s) {
        if (*s == '.') {
            if (has_dot) return 0;
            has_dot = 1;
        } else if (!isdigit((unsigned char)*s)) {
            return 0;
        }
        s++;
    }
    return 1;
}

// -----------------------------------------------------------------------------
// Result Set Creation / Destruction
// -----------------------------------------------------------------------------

ResultSet *result_set_create(int column_count) {
    ResultSet *rs = calloc(1, sizeof(ResultSet));
    if (!rs) return NULL;
    
    if (column_count > 0 && column_count <= RESULT_MAX_COLUMNS) {
        rs->column_names = calloc(column_count, sizeof(char*));
        rs->column_types = calloc(column_count, sizeof(char*));
        rs->column_count = column_count;
    }
    
    rs->rows = NULL;
    rs->rows_tail = NULL;
    rs->row_count = 0;
    rs->memory_used = sizeof(ResultSet);
    
    return rs;
}

void result_set_free(ResultSet *rs) {
    if (!rs) return;
    
    // Free column info
    for (int i = 0; i < rs->column_count; i++) {
        free(rs->column_names[i]);
        free(rs->column_types[i]);
    }
    free(rs->column_names);
    free(rs->column_types);
    
    // Free rows
    ResultRow *row = rs->rows;
    while (row) {
        ResultRow *next = row->next;
        for (int i = 0; i < row->column_count; i++) {
            free(row->values[i]);
        }
        free(row->values);
        free(row);
        row = next;
    }
    
    free(rs);
}

void result_set_clear(ResultSet *rs) {
    if (!rs) return;
    
    ResultRow *row = rs->rows;
    while (row) {
        ResultRow *next = row->next;
        for (int i = 0; i < row->column_count; i++) {
            free(row->values[i]);
        }
        free(row->values);
        free(row);
        row = next;
    }
    
    rs->rows = NULL;
    rs->rows_tail = NULL;
    rs->row_count = 0;
}

// -----------------------------------------------------------------------------
// Column Management
// -----------------------------------------------------------------------------

int result_set_add_column(ResultSet *rs, const char *name, const char *type) {
    if (!rs || rs->column_count >= RESULT_MAX_COLUMNS) return -1;
    
    int idx = rs->column_count;
    
    // Expand arrays if needed
    rs->column_names = realloc(rs->column_names, (idx + 1) * sizeof(char*));
    rs->column_types = realloc(rs->column_types, (idx + 1) * sizeof(char*));
    
    rs->column_names[idx] = my_strdup(name);
    rs->column_types[idx] = my_strdup(type);
    rs->column_count++;
    
    return idx;
}

int result_set_set_columns(ResultSet *rs, char **names, char **types, int count) {
    if (!rs || count > RESULT_MAX_COLUMNS) return -1;
    
    // Free existing
    for (int i = 0; i < rs->column_count; i++) {
        free(rs->column_names[i]);
        free(rs->column_types[i]);
    }
    
    rs->column_names = realloc(rs->column_names, count * sizeof(char*));
    rs->column_types = realloc(rs->column_types, count * sizeof(char*));
    
    for (int i = 0; i < count; i++) {
        rs->column_names[i] = my_strdup(names[i]);
        rs->column_types[i] = types ? my_strdup(types[i]) : my_strdup("TEXT");
    }
    rs->column_count = count;
    
    return 0;
}

// -----------------------------------------------------------------------------
// Row Management
// -----------------------------------------------------------------------------

int result_set_add_row(ResultSet *rs, char **values, int count) {
    if (!rs || count <= 0) return -1;
    
    ResultRow *row = calloc(1, sizeof(ResultRow));
    if (!row) return -1;
    
    row->values = calloc(count, sizeof(char*));
    row->column_count = count;
    
    for (int i = 0; i < count; i++) {
        row->values[i] = my_strdup(values[i]);
    }
    
    row->next = NULL;
    
    // Append to list
    if (!rs->rows) {
        rs->rows = row;
        rs->rows_tail = row;
    } else {
        rs->rows_tail->next = row;
        rs->rows_tail = row;
    }
    
    rs->row_count++;
    rs->memory_used += sizeof(ResultRow) + count * sizeof(char*);
    
    return 0;
}

ResultRow *result_set_new_row(ResultSet *rs) {
    if (!rs) return NULL;
    
    ResultRow *row = calloc(1, sizeof(ResultRow));
    if (!row) return NULL;
    
    row->values = calloc(rs->column_count, sizeof(char*));
    row->column_count = rs->column_count;
    row->next = NULL;
    
    if (!rs->rows) {
        rs->rows = row;
        rs->rows_tail = row;
    } else {
        rs->rows_tail->next = row;
        rs->rows_tail = row;
    }
    
    rs->row_count++;
    return row;
}

// -----------------------------------------------------------------------------
// Access Functions
// -----------------------------------------------------------------------------

int result_set_row_count(ResultSet *rs) {
    return rs ? rs->row_count : 0;
}

ResultRow *result_set_get_row(ResultSet *rs, int index) {
    if (!rs || index < 0 || index >= rs->row_count) return NULL;
    
    ResultRow *row = rs->rows;
    for (int i = 0; i < index && row; i++) {
        row = row->next;
    }
    return row;
}

char *result_set_get_value(ResultSet *rs, int row_idx, int col) {
    ResultRow *row = result_set_get_row(rs, row_idx);
    if (!row || col < 0 || col >= row->column_count) return NULL;
    return row->values[col];
}

// -----------------------------------------------------------------------------
// ORDER BY Implementation
// -----------------------------------------------------------------------------

typedef struct {
    ResultRow *row;
    int index;
} SortableRow;

static SortSpec *g_sort_specs = NULL;
static int g_sort_spec_count = 0;

static int compare_rows(const void *a, const void *b) {
    SortableRow *ra = (SortableRow *)a;
    SortableRow *rb = (SortableRow *)b;
    
    for (int i = 0; i < g_sort_spec_count; i++) {
        SortSpec *spec = &g_sort_specs[i];
        int col = spec->column_index;
        
        if (col >= ra->row->column_count || col >= rb->row->column_count) {
            continue;
        }
        
        char *va = ra->row->values[col];
        char *vb = rb->row->values[col];
        
        int cmp = 0;
        if (spec->is_numeric && is_numeric_string(va) && is_numeric_string(vb)) {
            double da = atof(va);
            double db = atof(vb);
            cmp = (da > db) - (da < db);
        } else {
            cmp = strcmp(va ? va : "", vb ? vb : "");
        }
        
        if (cmp != 0) {
            return spec->descending ? -cmp : cmp;
        }
    }
    
    return 0;
}

int result_set_sort(ResultSet *rs, const char *column, int descending) {
    if (!rs || !column || rs->row_count < 2) return 0;
    
    // Find column index
    int col_idx = -1;
    for (int i = 0; i < rs->column_count; i++) {
        if (strcasecmp(rs->column_names[i], column) == 0) {
            col_idx = i;
            break;
        }
    }
    
    if (col_idx < 0) {
        LOG_WARN("ORDER BY column '%s' not found", column);
        return -1;
    }
    
    SortSpec spec = {
        .column_index = col_idx,
        .column_name = (char*)column,
        .descending = descending,
        .is_numeric = (rs->column_types[col_idx] && 
                      (strcasecmp(rs->column_types[col_idx], "INT") == 0 ||
                       strcasecmp(rs->column_types[col_idx], "FLOAT") == 0))
    };
    
    return result_set_sort_multi(rs, &spec, 1);
}

int result_set_sort_multi(ResultSet *rs, SortSpec *specs, int spec_count) {
    if (!rs || !specs || spec_count <= 0 || rs->row_count < 2) return 0;
    
    // Create sortable array
    SortableRow *sortable = malloc(rs->row_count * sizeof(SortableRow));
    if (!sortable) return -1;
    
    ResultRow *row = rs->rows;
    for (int i = 0; i < rs->row_count; i++) {
        sortable[i].row = row;
        sortable[i].index = i;
        row = row->next;
    }
    
    // Set global sort specs for comparison
    g_sort_specs = specs;
    g_sort_spec_count = spec_count;
    
    // Sort
    qsort(sortable, rs->row_count, sizeof(SortableRow), compare_rows);
    
    // Rebuild linked list
    rs->rows = sortable[0].row;
    for (int i = 0; i < rs->row_count - 1; i++) {
        sortable[i].row->next = sortable[i + 1].row;
    }
    sortable[rs->row_count - 1].row->next = NULL;
    rs->rows_tail = sortable[rs->row_count - 1].row;
    
    free(sortable);
    g_sort_specs = NULL;
    g_sort_spec_count = 0;
    
    return 0;
}

SortSpec *parse_order_by(char **columns, int *desc_flags, int count,
                         char **col_names, char **col_types, int col_count) {
    if (!columns || count <= 0) return NULL;
    
    SortSpec *specs = calloc(count, sizeof(SortSpec));
    if (!specs) return NULL;
    
    for (int i = 0; i < count; i++) {
        specs[i].column_name = my_strdup(columns[i]);
        specs[i].descending = desc_flags ? desc_flags[i] : 0;
        specs[i].column_index = -1;
        
        // Find column index
        for (int j = 0; j < col_count; j++) {
            if (strcasecmp(columns[i], col_names[j]) == 0) {
                specs[i].column_index = j;
                specs[i].is_numeric = (col_types && col_types[j] &&
                    (strcasecmp(col_types[j], "INT") == 0 ||
                     strcasecmp(col_types[j], "FLOAT") == 0));
                break;
            }
        }
    }
    
    return specs;
}

// -----------------------------------------------------------------------------
// LIMIT / OFFSET Implementation
// -----------------------------------------------------------------------------

int result_set_limit(ResultSet *rs, int limit, int offset) {
    if (!rs) return -1;
    if (limit <= 0 && offset <= 0) return 0;
    
    // Skip offset rows
    int skipped = 0;
    ResultRow *row = rs->rows;
    ResultRow *prev = NULL;
    
    while (row && skipped < offset) {
        ResultRow *next = row->next;
        for (int i = 0; i < row->column_count; i++) {
            free(row->values[i]);
        }
        free(row->values);
        free(row);
        skipped++;
        rs->row_count--;
        row = next;
    }
    rs->rows = row;
    
    // Apply limit
    if (limit > 0) {
        int kept = 0;
        prev = NULL;
        while (row && kept < limit) {
            prev = row;
            row = row->next;
            kept++;
        }
        
        // Free remaining rows
        while (row) {
            ResultRow *next = row->next;
            for (int i = 0; i < row->column_count; i++) {
                free(row->values[i]);
            }
            free(row->values);
            free(row);
            rs->row_count--;
            row = next;
        }
        
        if (prev) {
            prev->next = NULL;
            rs->rows_tail = prev;
        }
    }
    
    return 0;
}

ResultSet *result_set_slice(ResultSet *rs, int limit, int offset) {
    if (!rs) return NULL;
    
    ResultSet *slice = result_set_create(rs->column_count);
    result_set_set_columns(slice, rs->column_names, rs->column_types, rs->column_count);
    
    ResultRow *row = rs->rows;
    int index = 0;
    int added = 0;
    
    while (row) {
        if (index >= offset) {
            if (limit <= 0 || added < limit) {
                result_set_add_row(slice, row->values, row->column_count);
                added++;
            } else {
                break;
            }
        }
        index++;
        row = row->next;
    }
    
    return slice;
}

// -----------------------------------------------------------------------------
// Result Printing
// -----------------------------------------------------------------------------

void result_set_print_table(ResultSet *rs, FILE *out) {
    if (!rs || !out) return;
    
    // Calculate column widths
    int *widths = calloc(rs->column_count, sizeof(int));
    
    for (int i = 0; i < rs->column_count; i++) {
        widths[i] = strlen(rs->column_names[i]);
    }
    
    ResultRow *row = rs->rows;
    while (row) {
        for (int i = 0; i < row->column_count && i < rs->column_count; i++) {
            int len = row->values[i] ? strlen(row->values[i]) : 4;
            if (len > widths[i]) widths[i] = len;
        }
        row = row->next;
    }
    
    // Cap widths
    for (int i = 0; i < rs->column_count; i++) {
        if (widths[i] > 40) widths[i] = 40;
    }
    
    // Print header separator
    fprintf(out, "+");
    for (int i = 0; i < rs->column_count; i++) {
        for (int j = 0; j < widths[i] + 2; j++) fprintf(out, "-");
        fprintf(out, "+");
    }
    fprintf(out, "\n");
    
    // Print header
    fprintf(out, "|");
    for (int i = 0; i < rs->column_count; i++) {
        fprintf(out, " %-*s |", widths[i], rs->column_names[i]);
    }
    fprintf(out, "\n");
    
    // Print header separator
    fprintf(out, "+");
    for (int i = 0; i < rs->column_count; i++) {
        for (int j = 0; j < widths[i] + 2; j++) fprintf(out, "-");
        fprintf(out, "+");
    }
    fprintf(out, "\n");
    
    // Print rows
    row = rs->rows;
    while (row) {
        fprintf(out, "|");
        for (int i = 0; i < rs->column_count; i++) {
            char *val = (i < row->column_count && row->values[i]) ? row->values[i] : "NULL";
            fprintf(out, " %-*.*s |", widths[i], widths[i], val);
        }
        fprintf(out, "\n");
        row = row->next;
    }
    
    // Print footer
    fprintf(out, "+");
    for (int i = 0; i < rs->column_count; i++) {
        for (int j = 0; j < widths[i] + 2; j++) fprintf(out, "-");
        fprintf(out, "+");
    }
    fprintf(out, "\n");
    
    fprintf(out, "%d row(s) in set\n", rs->row_count);
    
    free(widths);
}

void result_set_print_json(ResultSet *rs, FILE *out) {
    if (!rs || !out) return;
    
    fprintf(out, "[\n");
    
    ResultRow *row = rs->rows;
    while (row) {
        fprintf(out, "  {");
        for (int i = 0; i < rs->column_count; i++) {
            char *val = (i < row->column_count && row->values[i]) ? row->values[i] : "null";
            fprintf(out, "\"%s\": \"%s\"", rs->column_names[i], val);
            if (i < rs->column_count - 1) fprintf(out, ", ");
        }
        fprintf(out, "}%s\n", row->next ? "," : "");
        row = row->next;
    }
    
    fprintf(out, "]\n");
}

void result_set_print_csv(ResultSet *rs, FILE *out) {
    if (!rs || !out) return;
    
    // Header
    for (int i = 0; i < rs->column_count; i++) {
        fprintf(out, "%s%s", rs->column_names[i], 
                i < rs->column_count - 1 ? "," : "\n");
    }
    
    // Rows
    ResultRow *row = rs->rows;
    while (row) {
        for (int i = 0; i < rs->column_count; i++) {
            char *val = (i < row->column_count && row->values[i]) ? row->values[i] : "";
            // Escape quotes in CSV
            if (strchr(val, ',') || strchr(val, '"') || strchr(val, '\n')) {
                fprintf(out, "\"");
                for (char *p = val; *p; p++) {
                    if (*p == '"') fprintf(out, "\"\"");
                    else fputc(*p, out);
                }
                fprintf(out, "\"");
            } else {
                fprintf(out, "%s", val);
            }
            fprintf(out, "%s", i < rs->column_count - 1 ? "," : "\n");
        }
        row = row->next;
    }
}

// -----------------------------------------------------------------------------
// Query Cache Implementation
// -----------------------------------------------------------------------------

uint32_t result_hash_query(const char *query) {
    uint32_t hash = 5381;
    int c;
    while ((c = *query++)) {
        hash = ((hash << 5) + hash) + tolower(c);
    }
    return hash;
}

char *result_hash_query_str(const char *query) {
    uint32_t hash = result_hash_query(query);
    char *str = malloc(16);
    snprintf(str, 16, "%08x", hash);
    return str;
}

int query_cache_init(size_t max_memory) {
    if (g_cache_initialized) return 0;
    
    pthread_mutex_lock(&g_cache_lock);
    
    g_cache.capacity = RESULT_CACHE_SIZE;
    g_cache.entries = calloc(g_cache.capacity, sizeof(QueryCacheEntry));
    g_cache.count = 0;
    g_cache.max_memory = max_memory > 0 ? max_memory : (64 * 1024 * 1024); // 64MB default
    g_cache.total_memory = 0;
    g_cache.hits = 0;
    g_cache.misses = 0;
    g_cache.evictions = 0;
    
    g_cache_initialized = true;
    
    pthread_mutex_unlock(&g_cache_lock);
    
    LOG_INFO("Query cache initialized (max_memory=%zu)", g_cache.max_memory);
    return 0;
}

void query_cache_shutdown(void) {
    if (!g_cache_initialized) return;
    
    pthread_mutex_lock(&g_cache_lock);
    
    for (int i = 0; i < g_cache.count; i++) {
        free(g_cache.entries[i].query_hash);
        free(g_cache.entries[i].query_text);
        result_set_free(g_cache.entries[i].result);
    }
    free(g_cache.entries);
    
    memset(&g_cache, 0, sizeof(g_cache));
    g_cache_initialized = false;
    
    pthread_mutex_unlock(&g_cache_lock);
    
    LOG_INFO("Query cache shutdown");
}

static void evict_oldest_entry(void) {
    if (g_cache.count == 0) return;
    
    int oldest_idx = 0;
    time_t oldest_time = g_cache.entries[0].created_at;
    
    for (int i = 1; i < g_cache.count; i++) {
        if (g_cache.entries[i].created_at < oldest_time) {
            oldest_time = g_cache.entries[i].created_at;
            oldest_idx = i;
        }
    }
    
    // Free oldest entry
    g_cache.total_memory -= g_cache.entries[oldest_idx].result->memory_used;
    free(g_cache.entries[oldest_idx].query_hash);
    free(g_cache.entries[oldest_idx].query_text);
    result_set_free(g_cache.entries[oldest_idx].result);
    
    // Shift entries
    memmove(&g_cache.entries[oldest_idx], &g_cache.entries[oldest_idx + 1],
            (g_cache.count - oldest_idx - 1) * sizeof(QueryCacheEntry));
    g_cache.count--;
    g_cache.evictions++;
}

int query_cache_put(const char *query, ResultSet *result) {
    if (!g_cache_initialized || !query || !result) return -1;
    
    pthread_mutex_lock(&g_cache_lock);
    
    // Check if already exists
    char *hash = result_hash_query_str(query);
    for (int i = 0; i < g_cache.count; i++) {
        if (strcmp(g_cache.entries[i].query_hash, hash) == 0) {
            // Update existing
            result_set_free(g_cache.entries[i].result);
            g_cache.entries[i].result = result;
            g_cache.entries[i].created_at = time(NULL);
            free(hash);
            pthread_mutex_unlock(&g_cache_lock);
            return 0;
        }
    }
    
    // Evict if needed
    while (g_cache.count >= g_cache.capacity ||
           g_cache.total_memory + result->memory_used > g_cache.max_memory) {
        evict_oldest_entry();
    }
    
    // Add new entry
    int idx = g_cache.count++;
    g_cache.entries[idx].query_hash = hash;
    g_cache.entries[idx].query_text = my_strdup(query);
    g_cache.entries[idx].result = result;
    g_cache.entries[idx].created_at = time(NULL);
    g_cache.entries[idx].hit_count = 0;
    g_cache.entries[idx].valid = true;
    
    g_cache.total_memory += result->memory_used;
    
    pthread_mutex_unlock(&g_cache_lock);
    return 0;
}

ResultSet *query_cache_get(const char *query) {
    if (!g_cache_initialized || !query) return NULL;
    
    pthread_mutex_lock(&g_cache_lock);
    
    char *hash = result_hash_query_str(query);
    time_t now = time(NULL);
    
    for (int i = 0; i < g_cache.count; i++) {
        if (strcmp(g_cache.entries[i].query_hash, hash) == 0) {
            // Check TTL
            if (now - g_cache.entries[i].created_at > RESULT_CACHE_TTL) {
                // Expired
                g_cache.entries[i].valid = false;
                free(hash);
                g_cache.misses++;
                pthread_mutex_unlock(&g_cache_lock);
                return NULL;
            }
            
            if (g_cache.entries[i].valid) {
                g_cache.entries[i].hit_count++;
                g_cache.hits++;
                free(hash);
                pthread_mutex_unlock(&g_cache_lock);
                return g_cache.entries[i].result;
            }
        }
    }
    
    free(hash);
    g_cache.misses++;
    pthread_mutex_unlock(&g_cache_lock);
    return NULL;
}

void query_cache_invalidate(const char *table_name) {
    if (!g_cache_initialized || !table_name) return;
    
    pthread_mutex_lock(&g_cache_lock);
    
    // Simple invalidation: mark all entries containing table name as invalid
    for (int i = 0; i < g_cache.count; i++) {
        if (g_cache.entries[i].query_text &&
            strcasestr(g_cache.entries[i].query_text, table_name)) {
            g_cache.entries[i].valid = false;
        }
    }
    
    pthread_mutex_unlock(&g_cache_lock);
}

void query_cache_clear(void) {
    if (!g_cache_initialized) return;
    
    pthread_mutex_lock(&g_cache_lock);
    
    for (int i = 0; i < g_cache.count; i++) {
        free(g_cache.entries[i].query_hash);
        free(g_cache.entries[i].query_text);
        result_set_free(g_cache.entries[i].result);
    }
    
    g_cache.count = 0;
    g_cache.total_memory = 0;
    
    pthread_mutex_unlock(&g_cache_lock);
}

void query_cache_stats(uint64_t *hits, uint64_t *misses, uint64_t *evictions) {
    if (hits) *hits = g_cache.hits;
    if (misses) *misses = g_cache.misses;
    if (evictions) *evictions = g_cache.evictions;
}
