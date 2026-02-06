/**
 * InventixDB Query Optimizer Implementation
 * 
 * Cost-based query optimization with:
 * - Table statistics collection and management
 * - Access path selection (sequential vs index scan)
 * - Join order optimization using dynamic programming
 * - Query plan caching
 * - EXPLAIN output generation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>

#include "optimizer.h"
#include "index.h"
#include "logger.h"

#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

#define STATS_HASH_SIZE     128
#define PLAN_CACHE_MAX      256
#define MIN_ROWS_FOR_HASH   100
#define MIN_ROWS_FOR_MERGE  1000

// Default costs
#define DEFAULT_SEQ_PAGE_COST       1.0
#define DEFAULT_RANDOM_PAGE_COST    4.0
#define DEFAULT_CPU_TUPLE_COST      0.01
#define DEFAULT_CPU_INDEX_COST      0.005
#define DEFAULT_CPU_OPERATOR_COST   0.0025

// -----------------------------------------------------------------------------
// Internal Structures
// -----------------------------------------------------------------------------

typedef struct StatsEntry {
    char *table_name;
    TableStats *stats;
    struct StatsEntry *next;
} StatsEntry;

typedef struct PlanCacheEntry {
    char *query_hash;
    OptQueryPlan *plan;
    uint64_t hits;
    time_t last_used;
    struct PlanCacheEntry *next;
} PlanCacheEntry;

// Global optimizer state
static struct {
    bool initialized;
    
    // Statistics storage
    StatsEntry *stats_table[STATS_HASH_SIZE];
    pthread_rwlock_t stats_lock;
    
    // Plan cache
    PlanCacheEntry *plan_cache[PLAN_CACHE_MAX];
    pthread_rwlock_t cache_lock;
    int cache_count;
    
    // Default optimizer context
    OptimizerContext default_ctx;
    
} g_optimizer = {0};

// -----------------------------------------------------------------------------
// Helper Functions
// -----------------------------------------------------------------------------

static uint32_t hash_string(const char *str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static char *my_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
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
// Initialization
// -----------------------------------------------------------------------------

int optimizer_init(void) {
    if (g_optimizer.initialized) {
        return 0;
    }
    
    // Initialize stats hash table
    memset(g_optimizer.stats_table, 0, sizeof(g_optimizer.stats_table));
    if (pthread_rwlock_init(&g_optimizer.stats_lock, NULL) != 0) {
        return -1;
    }
    
    // Initialize plan cache
    memset(g_optimizer.plan_cache, 0, sizeof(g_optimizer.plan_cache));
    if (pthread_rwlock_init(&g_optimizer.cache_lock, NULL) != 0) {
        pthread_rwlock_destroy(&g_optimizer.stats_lock);
        return -1;
    }
    g_optimizer.cache_count = 0;
    
    // Set default cost parameters
    g_optimizer.default_ctx.seq_page_cost = DEFAULT_SEQ_PAGE_COST;
    g_optimizer.default_ctx.random_page_cost = DEFAULT_RANDOM_PAGE_COST;
    g_optimizer.default_ctx.cpu_tuple_cost = DEFAULT_CPU_TUPLE_COST;
    g_optimizer.default_ctx.cpu_index_tuple_cost = DEFAULT_CPU_INDEX_COST;
    g_optimizer.default_ctx.cpu_operator_cost = DEFAULT_CPU_OPERATOR_COST;
    
    // Enable all optimizations by default
    g_optimizer.default_ctx.enable_indexscan = true;
    g_optimizer.default_ctx.enable_hashjoin = true;
    g_optimizer.default_ctx.enable_mergejoin = true;
    g_optimizer.default_ctx.enable_nestloop = true;
    
    g_optimizer.initialized = true;
    
    LOG_INFO("Query optimizer initialized");
    return 0;
}

void optimizer_shutdown(void) {
    if (!g_optimizer.initialized) return;
    
    // Free statistics
    pthread_rwlock_wrlock(&g_optimizer.stats_lock);
    for (int i = 0; i < STATS_HASH_SIZE; i++) {
        StatsEntry *entry = g_optimizer.stats_table[i];
        while (entry) {
            StatsEntry *next = entry->next;
            if (entry->stats) {
                if (entry->stats->column_stats) {
                    for (int j = 0; j < entry->stats->column_count; j++) {
                        free(entry->stats->column_stats[j].column_name);
                        free(entry->stats->column_stats[j].min_value);
                        free(entry->stats->column_stats[j].max_value);
                    }
                    free(entry->stats->column_stats);
                }
                free(entry->stats->table_name);
                free(entry->stats);
            }
            free(entry->table_name);
            free(entry);
            entry = next;
        }
        g_optimizer.stats_table[i] = NULL;
    }
    pthread_rwlock_unlock(&g_optimizer.stats_lock);
    pthread_rwlock_destroy(&g_optimizer.stats_lock);
    
    // Free plan cache
    pthread_rwlock_wrlock(&g_optimizer.cache_lock);
    for (int i = 0; i < PLAN_CACHE_MAX; i++) {
        PlanCacheEntry *entry = g_optimizer.plan_cache[i];
        while (entry) {
            PlanCacheEntry *next = entry->next;
            free(entry->query_hash);
            optimizer_plan_free(entry->plan);
            free(entry);
            entry = next;
        }
        g_optimizer.plan_cache[i] = NULL;
    }
    pthread_rwlock_unlock(&g_optimizer.cache_lock);
    pthread_rwlock_destroy(&g_optimizer.cache_lock);
    
    g_optimizer.initialized = false;
    LOG_INFO("Query optimizer shutdown");
}

// -----------------------------------------------------------------------------
// Plan Node Construction
// -----------------------------------------------------------------------------

OptPlanNode *plan_node_new(PlanNodeType type) {
    OptPlanNode *node = (OptPlanNode *)calloc(1, sizeof(OptPlanNode));
    if (!node) return NULL;
    node->type = type;
    node->loops = 1;
    return node;
}

void plan_node_free(OptPlanNode *node) {
    if (!node) return;
    
    // Free children first
    plan_node_free(node->left);
    plan_node_free(node->right);
    
    // Free node-specific data
    switch (node->type) {
        case PLAN_SEQ_SCAN:
            free(node->data.scan.table_name);
            free(node->data.scan.filter);
            break;
            
        case PLAN_INDEX_SCAN:
        case PLAN_INDEX_ONLY_SCAN:
            free(node->data.index_scan.table_name);
            free(node->data.index_scan.index_name);
            free(node->data.index_scan.index_cond);
            free(node->data.index_scan.filter);
            break;
            
        case PLAN_NESTED_LOOP:
        case PLAN_HASH_JOIN:
        case PLAN_MERGE_JOIN:
            free(node->data.join.join_cond);
            break;
            
        case PLAN_SORT:
            if (node->data.sort.sort_keys) {
                for (int i = 0; i < node->data.sort.key_count; i++) {
                    free(node->data.sort.sort_keys[i]);
                }
                free(node->data.sort.sort_keys);
                free(node->data.sort.sort_desc);
            }
            break;
            
        case PLAN_AGGREGATE:
            if (node->data.aggregate.group_keys) {
                for (int i = 0; i < node->data.aggregate.key_count; i++) {
                    free(node->data.aggregate.group_keys[i]);
                }
                free(node->data.aggregate.group_keys);
            }
            if (node->data.aggregate.agg_funcs) {
                for (int i = 0; i < node->data.aggregate.agg_count; i++) {
                    free(node->data.aggregate.agg_funcs[i]);
                    free(node->data.aggregate.agg_cols[i]);
                }
                free(node->data.aggregate.agg_funcs);
                free(node->data.aggregate.agg_cols);
            }
            break;
            
        case PLAN_PROJECT:
            if (node->data.project.output_cols) {
                for (int i = 0; i < node->data.project.col_count; i++) {
                    free(node->data.project.output_cols[i]);
                }
                free(node->data.project.output_cols);
            }
            break;
            
        default:
            break;
    }
    
    free(node);
}

OptPlanNode *plan_make_seq_scan(const char *table, const char *filter) {
    OptPlanNode *node = plan_node_new(PLAN_SEQ_SCAN);
    if (!node) return NULL;
    
    node->data.scan.table_name = my_strdup(table);
    node->data.scan.filter = filter ? my_strdup(filter) : NULL;
    
    // Get table stats for cost estimation
    TableStats *stats = optimizer_get_table_stats(table);
    if (stats) {
        node->rows_estimate = stats->row_count;
        node->total_cost = stats->page_count * g_optimizer.default_ctx.seq_page_cost +
                           stats->row_count * g_optimizer.default_ctx.cpu_tuple_cost;
    } else {
        node->rows_estimate = DEFAULT_ROWS_ESTIMATE;
        node->total_cost = DEFAULT_ROWS_ESTIMATE * SEQ_SCAN_COST;
    }
    
    return node;
}

OptPlanNode *plan_make_index_scan(const char *table, const char *index,
                                   const char *index_cond, const char *filter) {
    OptPlanNode *node = plan_node_new(PLAN_INDEX_SCAN);
    if (!node) return NULL;
    
    node->data.index_scan.table_name = my_strdup(table);
    node->data.index_scan.index_name = my_strdup(index);
    node->data.index_scan.index_cond = index_cond ? my_strdup(index_cond) : NULL;
    node->data.index_scan.filter = filter ? my_strdup(filter) : NULL;
    
    // Cost estimation with selectivity
    TableStats *stats = optimizer_get_table_stats(table);
    double selectivity = 0.1; // Default 10% selectivity
    
    if (stats) {
        node->rows_estimate = stats->row_count * selectivity;
        // Index scan cost: random I/O for index + sequential for pages
        node->startup_cost = g_optimizer.default_ctx.random_page_cost;
        node->total_cost = node->startup_cost +
                           node->rows_estimate * g_optimizer.default_ctx.cpu_index_tuple_cost +
                           node->rows_estimate * g_optimizer.default_ctx.random_page_cost * 0.1;
    } else {
        node->rows_estimate = DEFAULT_ROWS_ESTIMATE * selectivity;
        node->total_cost = node->rows_estimate * INDEX_SCAN_COST;
    }
    
    return node;
}

OptPlanNode *plan_make_nested_loop(OptPlanNode *outer, OptPlanNode *inner,
                                    JoinType type, const char *cond) {
    OptPlanNode *node = plan_node_new(PLAN_NESTED_LOOP);
    if (!node) return NULL;
    
    node->left = outer;
    node->right = inner;
    node->data.join.join_type = type;
    node->data.join.join_method = JOIN_METHOD_NESTED_LOOP;
    node->data.join.join_cond = cond ? my_strdup(cond) : NULL;
    
    // Cost: outer_rows * inner_cost
    if (outer && inner) {
        node->startup_cost = outer->startup_cost;
        node->total_cost = outer->total_cost + 
                           outer->rows_estimate * inner->total_cost;
        node->rows_estimate = outer->rows_estimate * inner->rows_estimate * 0.1;
    }
    
    return node;
}

OptPlanNode *plan_make_hash_join(OptPlanNode *outer, OptPlanNode *inner,
                                  JoinType type, const char *cond) {
    OptPlanNode *node = plan_node_new(PLAN_HASH_JOIN);
    if (!node) return NULL;
    
    node->left = outer;
    node->right = inner;
    node->data.join.join_type = type;
    node->data.join.join_method = JOIN_METHOD_HASH;
    node->data.join.join_cond = cond ? my_strdup(cond) : NULL;
    
    // Cost: build hash table on inner + probe with outer
    if (outer && inner) {
        double build_cost = inner->total_cost + 
                           inner->rows_estimate * g_optimizer.default_ctx.cpu_operator_cost;
        double probe_cost = outer->total_cost +
                           outer->rows_estimate * g_optimizer.default_ctx.cpu_operator_cost;
        
        node->startup_cost = inner->total_cost + build_cost;
        node->total_cost = node->startup_cost + probe_cost;
        node->rows_estimate = outer->rows_estimate * inner->rows_estimate * 0.1;
    }
    
    return node;
}

OptPlanNode *plan_make_sort(OptPlanNode *child, char **keys, bool *desc, int count) {
    OptPlanNode *node = plan_node_new(PLAN_SORT);
    if (!node) return NULL;
    
    node->left = child;
    node->data.sort.key_count = count;
    node->data.sort.sort_keys = (char **)malloc(count * sizeof(char *));
    node->data.sort.sort_desc = (bool *)malloc(count * sizeof(bool));
    
    for (int i = 0; i < count; i++) {
        node->data.sort.sort_keys[i] = my_strdup(keys[i]);
        node->data.sort.sort_desc[i] = desc ? desc[i] : false;
    }
    
    // Cost: O(n log n) comparison cost
    if (child) {
        double n = child->rows_estimate;
        double log_n = n > 1 ? log2(n) : 1;
        node->startup_cost = child->total_cost + 
                            n * log_n * g_optimizer.default_ctx.cpu_operator_cost;
        node->total_cost = node->startup_cost;
        node->rows_estimate = child->rows_estimate;
    }
    
    return node;
}

OptPlanNode *plan_make_aggregate(OptPlanNode *child, char **group_keys,
                                  int group_count, char **agg_funcs,
                                  char **agg_cols, int agg_count) {
    OptPlanNode *node = plan_node_new(PLAN_AGGREGATE);
    if (!node) return NULL;
    
    node->left = child;
    
    // Copy group keys
    node->data.aggregate.key_count = group_count;
    if (group_count > 0) {
        node->data.aggregate.group_keys = (char **)malloc(group_count * sizeof(char *));
        for (int i = 0; i < group_count; i++) {
            node->data.aggregate.group_keys[i] = my_strdup(group_keys[i]);
        }
    }
    
    // Copy aggregations
    node->data.aggregate.agg_count = agg_count;
    if (agg_count > 0) {
        node->data.aggregate.agg_funcs = (char **)malloc(agg_count * sizeof(char *));
        node->data.aggregate.agg_cols = (char **)malloc(agg_count * sizeof(char *));
        for (int i = 0; i < agg_count; i++) {
            node->data.aggregate.agg_funcs[i] = my_strdup(agg_funcs[i]);
            node->data.aggregate.agg_cols[i] = my_strdup(agg_cols[i]);
        }
    }
    
    if (child) {
        node->startup_cost = child->total_cost;
        node->total_cost = child->total_cost + 
                          child->rows_estimate * g_optimizer.default_ctx.cpu_operator_cost;
        // Estimate distinct groups
        node->rows_estimate = group_count > 0 ? child->rows_estimate * 0.1 : 1;
    }
    
    return node;
}

OptPlanNode *plan_make_project(OptPlanNode *child, char **cols, int count) {
    OptPlanNode *node = plan_node_new(PLAN_PROJECT);
    if (!node) return NULL;
    
    node->left = child;
    node->data.project.col_count = count;
    node->data.project.output_cols = (char **)malloc(count * sizeof(char *));
    
    for (int i = 0; i < count; i++) {
        node->data.project.output_cols[i] = my_strdup(cols[i]);
    }
    
    if (child) {
        node->startup_cost = child->startup_cost;
        node->total_cost = child->total_cost;
        node->rows_estimate = child->rows_estimate;
    }
    
    return node;
}

OptPlanNode *plan_make_limit(OptPlanNode *child, int64_t limit, int64_t offset) {
    OptPlanNode *node = plan_node_new(PLAN_LIMIT);
    if (!node) return NULL;
    
    node->left = child;
    node->data.limit.limit_count = limit;
    node->data.limit.offset = offset;
    
    if (child) {
        node->startup_cost = child->startup_cost;
        // Cost scales with offset + limit
        double fraction = (double)(offset + limit) / child->rows_estimate;
        if (fraction > 1.0) fraction = 1.0;
        node->total_cost = child->total_cost * fraction;
        node->rows_estimate = limit;
    }
    
    return node;
}

// -----------------------------------------------------------------------------
// Statistics Management
// -----------------------------------------------------------------------------

TableStats *optimizer_get_table_stats(const char *table_name) {
    if (!table_name) return NULL;
    
    uint32_t hash = hash_string(table_name) % STATS_HASH_SIZE;
    
    pthread_rwlock_rdlock(&g_optimizer.stats_lock);
    StatsEntry *entry = g_optimizer.stats_table[hash];
    while (entry) {
        if (strcasecmp(entry->table_name, table_name) == 0) {
            pthread_rwlock_unlock(&g_optimizer.stats_lock);
            return entry->stats;
        }
        entry = entry->next;
    }
    pthread_rwlock_unlock(&g_optimizer.stats_lock);
    
    return NULL;
}

void optimizer_update_stats(const char *table_name, TableStats *stats) {
    if (!table_name || !stats) return;
    
    uint32_t hash = hash_string(table_name) % STATS_HASH_SIZE;
    
    pthread_rwlock_wrlock(&g_optimizer.stats_lock);
    
    // Look for existing entry
    StatsEntry *entry = g_optimizer.stats_table[hash];
    StatsEntry *prev = NULL;
    
    while (entry) {
        if (strcasecmp(entry->table_name, table_name) == 0) {
            // Update existing
            if (entry->stats != stats) {
                // Free old stats if different pointer
                if (entry->stats->column_stats) {
                    for (int j = 0; j < entry->stats->column_count; j++) {
                        free(entry->stats->column_stats[j].column_name);
                        free(entry->stats->column_stats[j].min_value);
                        free(entry->stats->column_stats[j].max_value);
                    }
                    free(entry->stats->column_stats);
                }
                free(entry->stats->table_name);
                free(entry->stats);
                entry->stats = stats;
            }
            pthread_rwlock_unlock(&g_optimizer.stats_lock);
            return;
        }
        prev = entry;
        entry = entry->next;
    }
    
    // Create new entry
    StatsEntry *new_entry = (StatsEntry *)calloc(1, sizeof(StatsEntry));
    if (!new_entry) {
        pthread_rwlock_unlock(&g_optimizer.stats_lock);
        return;
    }
    
    new_entry->table_name = my_strdup(table_name);
    new_entry->stats = stats;
    new_entry->next = g_optimizer.stats_table[hash];
    g_optimizer.stats_table[hash] = new_entry;
    
    pthread_rwlock_unlock(&g_optimizer.stats_lock);
}

int optimizer_analyze_table(const char *table_name, void *store) {
    // This would integrate with the storage engine to gather real statistics
    // For now, create basic statistics
    
    TableStats *stats = (TableStats *)calloc(1, sizeof(TableStats));
    if (!stats) return -1;
    
    stats->table_name = my_strdup(table_name);
    stats->row_count = DEFAULT_ROWS_ESTIMATE;
    stats->page_count = (stats->row_count * 100) / 4096 + 1; // Assume 100 bytes/row
    stats->avg_row_size = 100.0;
    stats->last_analyzed = time(NULL);
    
    optimizer_update_stats(table_name, stats);
    
    LOG_INFO("Analyzed table '%s': %lu rows, %lu pages", 
             table_name, stats->row_count, stats->page_count);
    
    return 0;
}

// -----------------------------------------------------------------------------
// Cost Estimation
// -----------------------------------------------------------------------------

double optimizer_estimate_selectivity(const char *table_name,
                                       const char *column,
                                       const char *op,
                                       const char *value) {
    // Default selectivity estimates based on operator
    if (!op) return 1.0;
    
    if (strcasecmp(op, "=") == 0) {
        // Equality: assume 1/distinct_values or 10%
        TableStats *stats = optimizer_get_table_stats(table_name);
        if (stats && stats->column_stats) {
            for (int i = 0; i < stats->column_count; i++) {
                if (strcasecmp(stats->column_stats[i].column_name, column) == 0) {
                    if (stats->column_stats[i].distinct_values > 0) {
                        return 1.0 / stats->column_stats[i].distinct_values;
                    }
                }
            }
        }
        return 0.1;
    }
    else if (strcasecmp(op, "<") == 0 || strcasecmp(op, ">") == 0 ||
             strcasecmp(op, "<=") == 0 || strcasecmp(op, ">=") == 0) {
        // Range: assume 33%
        return 0.33;
    }
    else if (strcasecmp(op, "<>") == 0 || strcasecmp(op, "!=") == 0) {
        // Not equal: assume 90%
        return 0.9;
    }
    else if (strcasecmp(op, "LIKE") == 0) {
        // LIKE with prefix: 10%, with wildcard start: 50%
        if (value && value[0] != '%') {
            return 0.1;
        }
        return 0.5;
    }
    else if (strcasecmp(op, "IS NULL") == 0) {
        // NULL check: use null_fraction if available
        TableStats *stats = optimizer_get_table_stats(table_name);
        if (stats && stats->column_stats) {
            for (int i = 0; i < stats->column_count; i++) {
                if (strcasecmp(stats->column_stats[i].column_name, column) == 0) {
                    return stats->column_stats[i].null_fraction;
                }
            }
        }
        return 0.01;
    }
    else if (strcasecmp(op, "IS NOT NULL") == 0) {
        return 0.99;
    }
    else if (strcasecmp(op, "IN") == 0) {
        // IN clause: 10% per value, max 50%
        return 0.3;
    }
    else if (strcasecmp(op, "BETWEEN") == 0) {
        // BETWEEN: typically 10-30%
        return 0.25;
    }
    
    return 0.5; // Default 50%
}

double optimizer_estimate_scan_cost(const char *table_name, 
                                    const char *filter,
                                    bool use_index,
                                    const char *index_name) {
    TableStats *stats = optimizer_get_table_stats(table_name);
    double rows = stats ? stats->row_count : DEFAULT_ROWS_ESTIMATE;
    double pages = stats ? stats->page_count : rows / 40;
    
    if (use_index && index_name) {
        // Index scan cost
        double selectivity = 0.1; // Would parse filter to get actual selectivity
        double matched_rows = rows * selectivity;
        
        return g_optimizer.default_ctx.random_page_cost + // Index root access
               matched_rows * g_optimizer.default_ctx.cpu_index_tuple_cost +
               matched_rows * g_optimizer.default_ctx.random_page_cost * 0.01;
    } else {
        // Sequential scan cost
        return pages * g_optimizer.default_ctx.seq_page_cost +
               rows * g_optimizer.default_ctx.cpu_tuple_cost;
    }
}

double optimizer_estimate_join_cost(JoinMethod method,
                                    double left_rows,
                                    double right_rows,
                                    double selectivity) {
    double output_rows = left_rows * right_rows * selectivity;
    
    switch (method) {
        case JOIN_METHOD_NESTED_LOOP:
            // O(n*m) comparisons
            return left_rows * right_rows * g_optimizer.default_ctx.cpu_operator_cost;
            
        case JOIN_METHOD_HASH:
            // Build: O(m), Probe: O(n)
            return right_rows * g_optimizer.default_ctx.cpu_operator_cost * 2 + // Build
                   left_rows * g_optimizer.default_ctx.cpu_operator_cost;        // Probe
            
        case JOIN_METHOD_MERGE:
            // Sort both: O(n log n + m log m), Merge: O(n + m)
            return left_rows * log2(left_rows > 1 ? left_rows : 2) * 
                       g_optimizer.default_ctx.cpu_operator_cost +
                   right_rows * log2(right_rows > 1 ? right_rows : 2) * 
                       g_optimizer.default_ctx.cpu_operator_cost +
                   (left_rows + right_rows) * g_optimizer.default_ctx.cpu_operator_cost;
            
        case JOIN_METHOD_INDEX_NL:
            // Outer scan + index lookup per row
            return left_rows * (g_optimizer.default_ctx.cpu_tuple_cost +
                               g_optimizer.default_ctx.random_page_cost * 0.1);
            
        default:
            return left_rows * right_rows * g_optimizer.default_ctx.cpu_operator_cost;
    }
}

// -----------------------------------------------------------------------------
// Access Path Selection
// -----------------------------------------------------------------------------

AccessPath *optimizer_choose_access_path(const char *table_name,
                                          const char *filter,
                                          char **available_indexes,
                                          int index_count) {
    AccessPath *best = (AccessPath *)calloc(1, sizeof(AccessPath));
    if (!best) return NULL;
    
    // Calculate sequential scan cost
    double seq_cost = optimizer_estimate_scan_cost(table_name, filter, false, NULL);
    
    best->type = ACCESS_SEQ_SCAN;
    best->table_name = my_strdup(table_name);
    best->total_cost = seq_cost;
    best->selectivity = 1.0;
    
    TableStats *stats = optimizer_get_table_stats(table_name);
    best->rows_estimate = stats ? stats->row_count : DEFAULT_ROWS_ESTIMATE;
    
    // Check each available index
    for (int i = 0; i < index_count; i++) {
        double idx_cost = optimizer_estimate_scan_cost(table_name, filter, 
                                                        true, available_indexes[i]);
        
        if (idx_cost < best->total_cost) {
            best->type = ACCESS_INDEX_SCAN;
            free(best->index_name);
            best->index_name = my_strdup(available_indexes[i]);
            best->total_cost = idx_cost;
            best->selectivity = 0.1; // Would calculate from filter
            best->rows_estimate = (stats ? stats->row_count : DEFAULT_ROWS_ESTIMATE) * 0.1;
        }
    }
    
    return best;
}

// -----------------------------------------------------------------------------
// Join Optimization
// -----------------------------------------------------------------------------

JoinPath *optimizer_choose_join_method(const char *left_table,
                                        const char *right_table,
                                        const char *join_cond,
                                        JoinType join_type) {
    JoinPath *path = (JoinPath *)calloc(1, sizeof(JoinPath));
    if (!path) return NULL;
    
    path->type = join_type;
    path->left_table = my_strdup(left_table);
    path->right_table = my_strdup(right_table);
    
    // Get table sizes
    TableStats *left_stats = optimizer_get_table_stats(left_table);
    TableStats *right_stats = optimizer_get_table_stats(right_table);
    
    double left_rows = left_stats ? left_stats->row_count : DEFAULT_ROWS_ESTIMATE;
    double right_rows = right_stats ? right_stats->row_count : DEFAULT_ROWS_ESTIMATE;
    
    double selectivity = 0.1; // Default join selectivity
    
    // Choose join method based on table sizes
    if (right_rows < MIN_ROWS_FOR_HASH) {
        // Small inner table: nested loop is efficient
        path->method = JOIN_METHOD_NESTED_LOOP;
        path->cost = optimizer_estimate_join_cost(JOIN_METHOD_NESTED_LOOP,
                                                   left_rows, right_rows, selectivity);
    }
    else if (left_rows >= MIN_ROWS_FOR_MERGE && right_rows >= MIN_ROWS_FOR_MERGE) {
        // Both large: consider merge join
        double hash_cost = optimizer_estimate_join_cost(JOIN_METHOD_HASH,
                                                         left_rows, right_rows, selectivity);
        double merge_cost = optimizer_estimate_join_cost(JOIN_METHOD_MERGE,
                                                          left_rows, right_rows, selectivity);
        
        if (merge_cost < hash_cost && g_optimizer.default_ctx.enable_mergejoin) {
            path->method = JOIN_METHOD_MERGE;
            path->cost = merge_cost;
        } else {
            path->method = JOIN_METHOD_HASH;
            path->cost = hash_cost;
        }
    }
    else {
        // Default to hash join
        path->method = JOIN_METHOD_HASH;
        path->cost = optimizer_estimate_join_cost(JOIN_METHOD_HASH,
                                                   left_rows, right_rows, selectivity);
    }
    
    path->rows_estimate = left_rows * right_rows * selectivity;
    
    return path;
}

// Dynamic programming for join order optimization
int optimizer_find_best_join_order(char **tables, int table_count,
                                    JoinPath *result_joins) {
    if (table_count < 2) return 0;
    
    // For small number of tables, try all permutations
    // For larger, use greedy or dynamic programming
    
    if (table_count == 2) {
        JoinPath *path = optimizer_choose_join_method(tables[0], tables[1], 
                                                       NULL, JOIN_INNER);
        if (path) {
            result_joins[0] = *path;
            free(path);
            return 1;
        }
        return 0;
    }
    
    // Greedy: always join smallest tables first
    double *sizes = (double *)malloc(table_count * sizeof(double));
    int *order = (int *)malloc(table_count * sizeof(int));
    
    for (int i = 0; i < table_count; i++) {
        TableStats *stats = optimizer_get_table_stats(tables[i]);
        sizes[i] = stats ? stats->row_count : DEFAULT_ROWS_ESTIMATE;
        order[i] = i;
    }
    
    // Sort by size (bubble sort for simplicity)
    for (int i = 0; i < table_count - 1; i++) {
        for (int j = 0; j < table_count - i - 1; j++) {
            if (sizes[order[j]] > sizes[order[j+1]]) {
                int tmp = order[j];
                order[j] = order[j+1];
                order[j+1] = tmp;
            }
        }
    }
    
    // Build join paths
    int join_count = 0;
    for (int i = 1; i < table_count; i++) {
        const char *left = (i == 1) ? tables[order[0]] : "<<prev>>";
        const char *right = tables[order[i]];
        
        JoinPath *path = optimizer_choose_join_method(left, right, NULL, JOIN_INNER);
        if (path) {
            result_joins[join_count++] = *path;
            free(path);
        }
    }
    
    free(sizes);
    free(order);
    
    return join_count;
}

// -----------------------------------------------------------------------------
// Plan Caching
// -----------------------------------------------------------------------------

static char *compute_query_hash(const char *query) {
    // Simple hash for plan caching
    uint32_t hash = hash_string(query);
    char *result = (char *)malloc(20);
    if (result) {
        snprintf(result, 20, "%08x", hash);
    }
    return result;
}

OptQueryPlan *optimizer_cache_get(const char *query_hash) {
    if (!query_hash) return NULL;
    
    uint32_t idx = hash_string(query_hash) % PLAN_CACHE_MAX;
    
    pthread_rwlock_rdlock(&g_optimizer.cache_lock);
    PlanCacheEntry *entry = g_optimizer.plan_cache[idx];
    
    while (entry) {
        if (strcmp(entry->query_hash, query_hash) == 0) {
            entry->hits++;
            entry->last_used = time(NULL);
            OptQueryPlan *plan = entry->plan;
            plan->is_cached = true;
            plan->cache_hits = entry->hits;
            pthread_rwlock_unlock(&g_optimizer.cache_lock);
            return plan;
        }
        entry = entry->next;
    }
    
    pthread_rwlock_unlock(&g_optimizer.cache_lock);
    return NULL;
}

void optimizer_cache_put(const char *query_hash, OptQueryPlan *plan) {
    if (!query_hash || !plan) return;
    
    uint32_t idx = hash_string(query_hash) % PLAN_CACHE_MAX;
    
    pthread_rwlock_wrlock(&g_optimizer.cache_lock);
    
    // Check if already cached
    PlanCacheEntry *entry = g_optimizer.plan_cache[idx];
    while (entry) {
        if (strcmp(entry->query_hash, query_hash) == 0) {
            // Already cached
            pthread_rwlock_unlock(&g_optimizer.cache_lock);
            return;
        }
        entry = entry->next;
    }
    
    // Add new entry
    PlanCacheEntry *new_entry = (PlanCacheEntry *)calloc(1, sizeof(PlanCacheEntry));
    if (!new_entry) {
        pthread_rwlock_unlock(&g_optimizer.cache_lock);
        return;
    }
    
    new_entry->query_hash = my_strdup(query_hash);
    new_entry->plan = plan;
    new_entry->hits = 0;
    new_entry->last_used = time(NULL);
    new_entry->next = g_optimizer.plan_cache[idx];
    g_optimizer.plan_cache[idx] = new_entry;
    g_optimizer.cache_count++;
    
    pthread_rwlock_unlock(&g_optimizer.cache_lock);
}

void optimizer_cache_invalidate(const char *table_name) {
    // Invalidate all plans that reference this table
    // For simplicity, just clear the entire cache
    
    pthread_rwlock_wrlock(&g_optimizer.cache_lock);
    
    for (int i = 0; i < PLAN_CACHE_MAX; i++) {
        PlanCacheEntry *entry = g_optimizer.plan_cache[i];
        while (entry) {
            PlanCacheEntry *next = entry->next;
            free(entry->query_hash);
            optimizer_plan_free(entry->plan);
            free(entry);
            entry = next;
        }
        g_optimizer.plan_cache[i] = NULL;
    }
    g_optimizer.cache_count = 0;
    
    pthread_rwlock_unlock(&g_optimizer.cache_lock);
    
    LOG_DEBUG("Plan cache invalidated due to table '%s' modification", 
              table_name ? table_name : "(all)");
}

// -----------------------------------------------------------------------------
// Main Optimization Entry Point
// -----------------------------------------------------------------------------

OptQueryPlan *optimizer_optimize(ASTNode *ast, void *catalog) {
    if (!ast) return NULL;
    
    double start_time = get_current_time_ms();
    
    OptQueryPlan *plan = (OptQueryPlan *)calloc(1, sizeof(OptQueryPlan));
    if (!plan) return NULL;
    
    plan->created_at = time(NULL);
    
    // Build query text for caching
    // (In a real implementation, this would serialize the AST)
    plan->query_text = my_strdup("SELECT ...");
    plan->plan_hash = compute_query_hash(plan->query_text);
    
    // Check cache first
    OptQueryPlan *cached = optimizer_cache_get(plan->plan_hash);
    if (cached) {
        free(plan->query_text);
        free(plan->plan_hash);
        free(plan);
        return cached;
    }
    
    // Build plan based on AST node type
    switch (ast->type) {
        case NODE_CMD_SELECT: {
            // Extract table from FROM clause
            const char *table_name = ast->data.select.table_name;
            
            // Create base scan node
            OptPlanNode *scan = NULL;
            
            // Check for available indexes
            // For now, use sequential scan
            scan = plan_make_seq_scan(table_name, NULL);
            
            // Add filter if WHERE clause exists
            if (ast->data.select.where_clause) {
                // Would apply filter to scan or add Filter node
            }
            
            // Add projection for selected columns
            if (ast->data.select.columns != NULL) {
                // Columns are in a NodeList, count them
                int col_count = 0;
                NodeList *col = ast->data.select.columns;
                while (col) { col_count++; col = col->next; }
                
                if (col_count > 0 && ast->data.select.columns->value &&
                    strcmp(ast->data.select.columns->value, "*") != 0) {
                    // Extract column names into array
                    char **cols = (char **)malloc(col_count * sizeof(char *));
                    col = ast->data.select.columns;
                    for (int i = 0; i < col_count && col; i++) {
                        cols[i] = col->value;
                        col = col->next;
                    }
                    scan = plan_make_project(scan, cols, col_count);
                    free(cols);
                }
            }
            
            // Add sort for ORDER BY
            if (ast->data.select.order_by_count > 0) {
                bool *desc = NULL;
                if (ast->data.select.order_desc) {
                    desc = (bool *)malloc(ast->data.select.order_by_count * sizeof(bool));
                    for (int i = 0; i < ast->data.select.order_by_count; i++) {
                        desc[i] = ast->data.select.order_desc[i] != 0;
                    }
                }
                scan = plan_make_sort(scan, ast->data.select.order_columns,
                                       desc, 
                                       ast->data.select.order_by_count);
                free(desc);
            }
            
            // Add limit
            if (ast->data.select.limit > 0) {
                scan = plan_make_limit(scan, ast->data.select.limit, 0);
            }
            
            plan->root = scan;
            break;
        }
        
        default:
            // Non-SELECT queries don't need optimization
            plan->root = plan_node_new(PLAN_RESULT);
            break;
    }
    
    // Calculate total cost
    if (plan->root) {
        plan->total_cost = plan->root->total_cost;
        plan->total_rows = plan->root->rows_estimate;
    }
    
    plan->planning_time_ms = get_current_time_ms() - start_time;
    
    // Cache the plan
    optimizer_cache_put(plan->plan_hash, plan);
    
    return plan;
}

// -----------------------------------------------------------------------------
// Plan Execution
// -----------------------------------------------------------------------------

int optimizer_execute(OptQueryPlan *plan, void *store, void *ctx, FILE *out) {
    if (!plan || !plan->root) {
        return -1;
    }
    
    double start_time = get_current_time_ms();
    
    // Execute based on plan node type
    // This would integrate with the executor to run the optimized plan
    // For now, this is a stub
    
    plan->execution_time_ms = get_current_time_ms() - start_time;
    
    return 0;
}

// -----------------------------------------------------------------------------
// EXPLAIN Output
// -----------------------------------------------------------------------------

static void explain_node(OptPlanNode *node, FILE *out, int indent, bool analyze) {
    if (!node || !out) return;
    
    char prefix[256] = "";
    for (int i = 0; i < indent; i++) {
        strcat(prefix, "  ");
    }
    
    const char *type_str = "Unknown";
    switch (node->type) {
        case PLAN_SEQ_SCAN:      type_str = "Seq Scan"; break;
        case PLAN_INDEX_SCAN:    type_str = "Index Scan"; break;
        case PLAN_INDEX_ONLY_SCAN: type_str = "Index Only Scan"; break;
        case PLAN_NESTED_LOOP:   type_str = "Nested Loop"; break;
        case PLAN_HASH_JOIN:     type_str = "Hash Join"; break;
        case PLAN_MERGE_JOIN:    type_str = "Merge Join"; break;
        case PLAN_SORT:          type_str = "Sort"; break;
        case PLAN_AGGREGATE:     type_str = "Aggregate"; break;
        case PLAN_GROUP:         type_str = "GroupAggregate"; break;
        case PLAN_FILTER:        type_str = "Filter"; break;
        case PLAN_PROJECT:       type_str = "Project"; break;
        case PLAN_LIMIT:         type_str = "Limit"; break;
        case PLAN_RESULT:        type_str = "Result"; break;
    }
    
    fprintf(out, "%s%s", prefix, type_str);
    
    // Node-specific details
    switch (node->type) {
        case PLAN_SEQ_SCAN:
            fprintf(out, " on %s", node->data.scan.table_name);
            break;
        case PLAN_INDEX_SCAN:
            fprintf(out, " using %s on %s", 
                    node->data.index_scan.index_name,
                    node->data.index_scan.table_name);
            break;
        case PLAN_LIMIT:
            fprintf(out, " (limit=%lld)", (long long)node->data.limit.limit_count);
            break;
        default:
            break;
    }
    
    // Cost info
    fprintf(out, "  (cost=%.2f..%.2f rows=%.0f width=%d)",
            node->startup_cost, node->total_cost, 
            node->rows_estimate, node->width);
    
    // Actual execution stats if EXPLAIN ANALYZE
    if (analyze && node->actual_time_ms > 0) {
        fprintf(out, "\n%s  (actual time=%.3f..%.3f rows=%.0f loops=%d)",
                prefix, node->startup_cost, node->actual_time_ms,
                node->actual_rows, node->loops);
    }
    
    fprintf(out, "\n");
    
    // Filter condition
    if (node->type == PLAN_SEQ_SCAN && node->data.scan.filter) {
        fprintf(out, "%s  Filter: %s\n", prefix, node->data.scan.filter);
    }
    if (node->type == PLAN_INDEX_SCAN && node->data.index_scan.index_cond) {
        fprintf(out, "%s  Index Cond: %s\n", prefix, node->data.index_scan.index_cond);
    }
    
    // Children
    if (node->left) {
        fprintf(out, "%s  -> ", prefix);
        explain_node(node->left, out, indent + 2, analyze);
    }
    if (node->right) {
        fprintf(out, "%s  -> ", prefix);
        explain_node(node->right, out, indent + 2, analyze);
    }
}

char *optimizer_plan_explain(OptQueryPlan *plan, bool analyze) {
    if (!plan) return NULL;
    
    // Write to temporary buffer (Windows-compatible version)
    char *buffer = (char *)malloc(8192);
    if (!buffer) return NULL;
    
    buffer[0] = '\0';
    int offset = 0;
    
    offset += snprintf(buffer + offset, 8192 - offset, 
                       "Query Plan (cost=%.2f rows=%.0f)\n",
                       plan->total_cost, plan->total_rows);
    
    // Add plan node info (simplified for Windows)
    if (plan->root) {
        const char *type_str = "Unknown";
        switch (plan->root->type) {
            case PLAN_SEQ_SCAN:      type_str = "Seq Scan"; break;
            case PLAN_INDEX_SCAN:    type_str = "Index Scan"; break;
            case PLAN_INDEX_ONLY_SCAN: type_str = "Index Only Scan"; break;
            case PLAN_NESTED_LOOP:   type_str = "Nested Loop"; break;
            case PLAN_HASH_JOIN:     type_str = "Hash Join"; break;
            case PLAN_MERGE_JOIN:    type_str = "Merge Join"; break;
            case PLAN_SORT:          type_str = "Sort"; break;
            case PLAN_AGGREGATE:     type_str = "Aggregate"; break;
            case PLAN_GROUP:         type_str = "GroupAggregate"; break;
            case PLAN_FILTER:        type_str = "Filter"; break;
            case PLAN_PROJECT:       type_str = "Project"; break;
            case PLAN_LIMIT:         type_str = "Limit"; break;
            case PLAN_RESULT:        type_str = "Result"; break;
        }
        
        offset += snprintf(buffer + offset, 8192 - offset,
                           "  -> %s  (cost=%.2f..%.2f rows=%.0f)\n",
                           type_str, plan->root->startup_cost, 
                           plan->root->total_cost, plan->root->rows_estimate);
        
        // Show table for scan nodes
        if (plan->root->type == PLAN_SEQ_SCAN && plan->root->data.scan.table_name) {
            offset += snprintf(buffer + offset, 8192 - offset,
                               "       on %s\n", plan->root->data.scan.table_name);
        }
    }
    
    offset += snprintf(buffer + offset, 8192 - offset,
                       "Planning Time: %.3f ms\n", plan->planning_time_ms);
    if (analyze) {
        offset += snprintf(buffer + offset, 8192 - offset,
                           "Execution Time: %.3f ms\n", plan->execution_time_ms);
    }
    
    if (plan->is_cached) {
        offset += snprintf(buffer + offset, 8192 - offset,
                           "(cached, hits=%lu)\n", (unsigned long)plan->cache_hits);
    }
    
    return buffer;
}

void optimizer_plan_print(OptQueryPlan *plan, FILE *out) {
    char *explain = optimizer_plan_explain(plan, false);
    if (explain) {
        fprintf(out, "%s", explain);
        free(explain);
    }
}

void optimizer_plan_free(OptQueryPlan *plan) {
    if (!plan) return;
    
    plan_node_free(plan->root);
    free(plan->query_text);
    free(plan->plan_hash);
    free(plan);
}

// -----------------------------------------------------------------------------
// EXPLAIN Format Output
// -----------------------------------------------------------------------------

char *optimizer_explain_format(OptQueryPlan *plan, int format, bool analyze) {
    switch (format) {
        case EXPLAIN_FORMAT_TEXT:
            return optimizer_plan_explain(plan, analyze);
            
        case EXPLAIN_FORMAT_JSON: {
            // Simplified JSON output
            char *buffer = (char *)malloc(4096);
            if (!buffer) return NULL;
            
            snprintf(buffer, 4096,
                "{\n"
                "  \"Plan\": {\n"
                "    \"Total Cost\": %.2f,\n"
                "    \"Plan Rows\": %.0f,\n"
                "    \"Planning Time\": %.3f\n"
                "  }\n"
                "}\n",
                plan->total_cost, plan->total_rows, plan->planning_time_ms);
            
            return buffer;
        }
        
        case EXPLAIN_FORMAT_XML: {
            char *buffer = (char *)malloc(4096);
            if (!buffer) return NULL;
            
            snprintf(buffer, 4096,
                "<?xml version=\"1.0\"?>\n"
                "<explain>\n"
                "  <Query>\n"
                "    <Plan>\n"
                "      <Total-Cost>%.2f</Total-Cost>\n"
                "      <Plan-Rows>%.0f</Plan-Rows>\n"
                "    </Plan>\n"
                "    <Planning-Time>%.3f</Planning-Time>\n"
                "  </Query>\n"
                "</explain>\n",
                plan->total_cost, plan->total_rows, plan->planning_time_ms);
            
            return buffer;
        }
        
        default:
            return optimizer_plan_explain(plan, analyze);
    }
}
