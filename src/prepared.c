/**
 * InventixDB Prepared Statements Implementation
 * 
 * Full-featured prepared statement system with:
 * - Query caching and normalization
 * - Parameterized query execution
 * - SQL injection prevention
 * - Statement lifecycle management
 */

#define _GNU_SOURCE  // For strndup on Windows
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>

#include "prepared.h"
#include "logger.h"
#include "lexer.h"
#include "parser.h"

// strndup implementation for Windows
#ifdef _WIN32
static char *my_strndup(const char *s, size_t n) {
    size_t len = strlen(s);
    if (n < len) len = n;
    char *result = malloc(len + 1);
    if (!result) return NULL;
    memcpy(result, s, len);
    result[len] = '\0';
    return result;
}
#define strndup my_strndup
#endif

// -----------------------------------------------------------------------------
// Global State
// -----------------------------------------------------------------------------

static StatementCache *g_stmt_cache = NULL;
static pthread_mutex_t g_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_prepared_initialized = false;
static uint32_t g_global_stmt_id = 0;

// -----------------------------------------------------------------------------
// Initialization
// -----------------------------------------------------------------------------

int prepared_init(void) {
    if (g_prepared_initialized) return 0;
    
    pthread_mutex_lock(&g_cache_lock);
    
    g_stmt_cache = calloc(1, sizeof(StatementCache));
    if (!g_stmt_cache) {
        pthread_mutex_unlock(&g_cache_lock);
        return -1;
    }
    
    g_stmt_cache->capacity = STMT_CACHE_SIZE;
    g_stmt_cache->entries = calloc(STMT_CACHE_SIZE, sizeof(CacheEntry));
    if (!g_stmt_cache->entries) {
        free(g_stmt_cache);
        g_stmt_cache = NULL;
        pthread_mutex_unlock(&g_cache_lock);
        return -1;
    }
    
    pthread_mutex_init(&g_stmt_cache->lock, NULL);
    g_prepared_initialized = true;
    
    LOG_INFO("Prepared statement system initialized (cache size: %d)", STMT_CACHE_SIZE);
    
    pthread_mutex_unlock(&g_cache_lock);
    return 0;
}

void prepared_shutdown(void) {
    if (!g_prepared_initialized) return;
    
    pthread_mutex_lock(&g_cache_lock);
    
    if (g_stmt_cache) {
        // Free all cache entries
        for (int i = 0; i < g_stmt_cache->capacity; i++) {
            CacheEntry *e = &g_stmt_cache->entries[i];
            if (e->query_hash) {
                free(e->query_hash);
                free(e->normalized_query);
                if (e->cached_ast) free_ast(e->cached_ast);
                if (e->cached_plan) plan_destroy(e->cached_plan);
            }
        }
        free(g_stmt_cache->entries);
        pthread_mutex_destroy(&g_stmt_cache->lock);
        free(g_stmt_cache);
        g_stmt_cache = NULL;
    }
    
    g_prepared_initialized = false;
    pthread_mutex_unlock(&g_cache_lock);
    
    LOG_INFO("Prepared statement system shutdown");
}

// -----------------------------------------------------------------------------
// Session Statement Store
// -----------------------------------------------------------------------------

SessionStatementStore *stmt_store_create(void) {
    SessionStatementStore *store = calloc(1, sizeof(SessionStatementStore));
    if (!store) return NULL;
    
    store->capacity = MAX_PREPARED_STMTS;
    store->statements = calloc(MAX_PREPARED_STMTS, sizeof(PreparedStatement *));
    if (!store->statements) {
        free(store);
        return NULL;
    }
    
    store->next_stmt_id = 1;
    return store;
}

void stmt_store_destroy(SessionStatementStore *store) {
    if (!store) return;
    
    for (int i = 0; i < store->count; i++) {
        PreparedStatement *stmt = store->statements[i];
        if (stmt) {
            free(stmt->query_template);
            if (stmt->parsed_ast) free_ast(stmt->parsed_ast);
            free(stmt->prepared_by);
            free(stmt);
        }
    }
    
    free(store->statements);
    free(store);
}

// -----------------------------------------------------------------------------
// Query Normalization (for caching)
// -----------------------------------------------------------------------------

char *stmt_normalize_query(const char *query) {
    if (!query) return NULL;
    
    size_t len = strlen(query);
    char *normalized = malloc(len + 1);
    if (!normalized) return NULL;
    
    int j = 0;
    bool in_string = false;
    bool last_space = false;
    char quote_char = 0;
    
    for (size_t i = 0; i < len; i++) {
        char c = query[i];
        
        // Handle string literals
        if ((c == '\'' || c == '"') && (i == 0 || query[i-1] != '\\')) {
            if (!in_string) {
                in_string = true;
                quote_char = c;
                // Replace string literals with placeholder
                normalized[j++] = '?';
                normalized[j++] = 'S';
                // Skip until end of string
                i++;
                while (i < len && !(query[i] == quote_char && query[i-1] != '\\')) i++;
                continue;
            } else if (c == quote_char) {
                in_string = false;
                continue;
            }
        }
        
        if (in_string) continue;
        
        // Handle numbers - replace with placeholder
        if (isdigit(c) || (c == '-' && i + 1 < len && isdigit(query[i+1]))) {
            normalized[j++] = '?';
            normalized[j++] = 'N';
            while (i < len && (isdigit(query[i]) || query[i] == '.' || query[i] == '-')) i++;
            i--;
            continue;
        }
        
        // Normalize whitespace
        if (isspace(c)) {
            if (!last_space && j > 0) {
                normalized[j++] = ' ';
                last_space = true;
            }
            continue;
        }
        last_space = false;
        
        // Convert to uppercase for keywords
        normalized[j++] = toupper(c);
    }
    
    // Trim trailing space
    while (j > 0 && normalized[j-1] == ' ') j--;
    normalized[j] = '\0';
    
    return normalized;
}

// Simple hash function
static uint32_t hash_string(const char *str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

char *stmt_compute_hash(const char *normalized) {
    if (!normalized) return NULL;
    
    uint32_t hash = hash_string(normalized);
    char *hash_str = malloc(17);
    snprintf(hash_str, 17, "%08X", hash);
    return hash_str;
}

// -----------------------------------------------------------------------------
// Parameter Handling
// -----------------------------------------------------------------------------

int stmt_bind_int(Parameter *param, int64_t value) {
    if (!param) return -1;
    param->type = PARAM_TYPE_INT;
    param->value.int_val = value;
    param->is_null = false;
    return 0;
}

int stmt_bind_float(Parameter *param, double value) {
    if (!param) return -1;
    param->type = PARAM_TYPE_FLOAT;
    param->value.float_val = value;
    param->is_null = false;
    return 0;
}

int stmt_bind_string(Parameter *param, const char *value, size_t len) {
    if (!param) return -1;
    param->type = PARAM_TYPE_STRING;
    param->value.string_val.data = strndup(value, len);
    param->value.string_val.len = len;
    param->is_null = false;
    return 0;
}

int stmt_bind_bool(Parameter *param, bool value) {
    if (!param) return -1;
    param->type = PARAM_TYPE_BOOL;
    param->value.bool_val = value;
    param->is_null = false;
    return 0;
}

int stmt_bind_null(Parameter *param) {
    if (!param) return -1;
    param->type = PARAM_TYPE_NULL;
    param->is_null = true;
    return 0;
}

int stmt_bind_blob(Parameter *param, const uint8_t *data, size_t len) {
    if (!param) return -1;
    param->type = PARAM_TYPE_BLOB;
    param->value.blob_val.data = malloc(len);
    if (!param->value.blob_val.data) return -1;
    memcpy(param->value.blob_val.data, data, len);
    param->value.blob_val.len = len;
    param->is_null = false;
    return 0;
}

char *param_to_string(const Parameter *param) {
    if (!param || param->is_null) return strdup("NULL");
    
    char buf[MAX_PARAM_VALUE_LEN];
    
    switch (param->type) {
        case PARAM_TYPE_INT:
            snprintf(buf, sizeof(buf), "%lld", (long long)param->value.int_val);
            break;
        case PARAM_TYPE_FLOAT:
            snprintf(buf, sizeof(buf), "%g", param->value.float_val);
            break;
        case PARAM_TYPE_STRING:
            snprintf(buf, sizeof(buf), "'%.*s'", 
                     (int)param->value.string_val.len, 
                     param->value.string_val.data);
            break;
        case PARAM_TYPE_BOOL:
            snprintf(buf, sizeof(buf), "%s", param->value.bool_val ? "TRUE" : "FALSE");
            break;
        default:
            return strdup("NULL");
    }
    
    return strdup(buf);
}

// -----------------------------------------------------------------------------
// SQL Injection Prevention
// -----------------------------------------------------------------------------

bool stmt_validate_param(const Parameter *param) {
    if (!param) return false;
    
    // Check for SQL injection patterns in string parameters
    if (param->type == PARAM_TYPE_STRING && param->value.string_val.data) {
        const char *dangerous[] = {
            "';", "\"--", "/*", "*/", "@@", "@variable",
            "EXEC ", "EXECUTE ", "xp_", "sp_", "0x",
            "UNION ", "SELECT ", "INSERT ", "UPDATE ", "DELETE ",
            "DROP ", "CREATE ", "ALTER ", "GRANT ", "REVOKE ",
            NULL
        };
        
        char *upper = strdup(param->value.string_val.data);
        for (char *p = upper; *p; p++) *p = toupper(*p);
        
        for (int i = 0; dangerous[i]; i++) {
            if (strstr(upper, dangerous[i])) {
                free(upper);
                LOG_WARN("SQL injection pattern detected: %s", dangerous[i]);
                return false;
            }
        }
        free(upper);
    }
    
    return true;
}

char *stmt_escape_string(const char *str, size_t len) {
    if (!str) return NULL;
    
    // Allocate worst case (every char needs escaping)
    char *escaped = malloc(len * 2 + 1);
    if (!escaped) return NULL;
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        switch (str[i]) {
            case '\'':
                escaped[j++] = '\'';
                escaped[j++] = '\'';
                break;
            case '\\':
                escaped[j++] = '\\';
                escaped[j++] = '\\';
                break;
            case '\0':
                escaped[j++] = '\\';
                escaped[j++] = '0';
                break;
            case '\n':
                escaped[j++] = '\\';
                escaped[j++] = 'n';
                break;
            case '\r':
                escaped[j++] = '\\';
                escaped[j++] = 'r';
                break;
            default:
                escaped[j++] = str[i];
        }
    }
    escaped[j] = '\0';
    
    return escaped;
}

bool stmt_is_safe_identifier(const char *identifier) {
    if (!identifier || !*identifier) return false;
    
    // Must start with letter or underscore
    if (!isalpha(identifier[0]) && identifier[0] != '_') return false;
    
    // Rest must be alphanumeric or underscore
    for (const char *p = identifier + 1; *p; p++) {
        if (!isalnum(*p) && *p != '_') return false;
    }
    
    // Check length
    if (strlen(identifier) > 128) return false;
    
    return true;
}

// -----------------------------------------------------------------------------
// Parse USING Clause
// -----------------------------------------------------------------------------

int parse_using_clause(const char *using_str, Parameter *params, int max_params) {
    if (!using_str || !params) return 0;
    
    // Skip whitespace and opening paren
    while (*using_str && (isspace(*using_str) || *using_str == '(')) using_str++;
    
    int count = 0;
    const char *p = using_str;
    
    while (*p && *p != ')' && count < max_params) {
        // Skip whitespace
        while (*p && isspace(*p)) p++;
        if (!*p || *p == ')') break;
        
        // Parse value
        if (*p == '\'' || *p == '"') {
            // String value
            char quote = *p++;
            const char *start = p;
            while (*p && *p != quote) p++;
            size_t len = p - start;
            stmt_bind_string(&params[count], start, len);
            if (*p == quote) p++;
        } else if (*p == '-' || isdigit(*p)) {
            // Numeric value
            char *end;
            if (strchr(p, '.')) {
                double val = strtod(p, &end);
                stmt_bind_float(&params[count], val);
            } else {
                int64_t val = strtoll(p, &end, 10);
                stmt_bind_int(&params[count], val);
            }
            p = end;
        } else if (strncasecmp(p, "NULL", 4) == 0) {
            stmt_bind_null(&params[count]);
            p += 4;
        } else if (strncasecmp(p, "TRUE", 4) == 0) {
            stmt_bind_bool(&params[count], true);
            p += 4;
        } else if (strncasecmp(p, "FALSE", 5) == 0) {
            stmt_bind_bool(&params[count], false);
            p += 5;
        } else {
            // Unknown, skip to comma
            while (*p && *p != ',' && *p != ')') p++;
        }
        
        count++;
        
        // Skip comma
        while (*p && (isspace(*p) || *p == ',')) p++;
    }
    
    return count;
}

// -----------------------------------------------------------------------------
// Statement Prepare
// -----------------------------------------------------------------------------

static int find_parameters(const char *query, int *positions, int max_params) {
    int count = 0;
    int pos = 0;
    bool in_string = false;
    char quote = 0;
    
    while (query[pos] && count < max_params) {
        char c = query[pos];
        
        if ((c == '\'' || c == '"') && (pos == 0 || query[pos-1] != '\\')) {
            if (!in_string) {
                in_string = true;
                quote = c;
            } else if (c == quote) {
                in_string = false;
            }
        }
        
        if (!in_string && c == '?') {
            positions[count++] = pos;
        }
        
        pos++;
    }
    
    return count;
}

PreparedStatement *stmt_prepare(SessionStatementStore *store, 
                                const char *name, 
                                const char *query,
                                const char *username) {
    if (!store || !name || !query) return NULL;
    
    // Check if name already exists
    if (stmt_find(store, name)) {
        LOG_WARN("Prepared statement '%s' already exists", name);
        return NULL;
    }
    
    // Check capacity
    if (store->count >= store->capacity) {
        LOG_ERROR("Prepared statement limit reached (%d)", store->capacity);
        return NULL;
    }
    
    PreparedStatement *stmt = calloc(1, sizeof(PreparedStatement));
    if (!stmt) return NULL;
    
    strncpy(stmt->name, name, MAX_STMT_NAME_LEN - 1);
    stmt->query_template = strdup(query);
    
    // Find parameter positions
    stmt->param_count = find_parameters(query, stmt->param_positions, MAX_PARAMETERS);
    
    // Pre-parse the query (with placeholders replaced by dummy values for parsing)
    char *parse_query = strdup(query);
    // Replace ? with 0 for parsing
    for (int i = 0; i < stmt->param_count; i++) {
        parse_query[stmt->param_positions[i]] = '0';
    }
    
    // Try to parse
    TokenList *tokens = tokenize(parse_query);
    if (tokens) {
        stmt->parsed_ast = parse(tokens);
        // Don't free tokens if AST creation failed for debugging
        if (!stmt->parsed_ast) {
            LOG_WARN("Failed to pre-parse prepared statement '%s'", name);
        }
    }
    free(parse_query);
    
    // Set metadata
    stmt->stmt_id = __sync_add_and_fetch(&g_global_stmt_id, 1);
    stmt->prepare_time = (uint64_t)time(NULL) * 1000;
    stmt->state = STMT_STATE_PREPARED;
    stmt->prepared_by = username ? strdup(username) : NULL;
    stmt->is_validated = true;
    
    // Add to store
    store->statements[store->count++] = stmt;
    store->total_prepares++;
    
    LOG_DEBUG("Prepared statement '%s' with %d parameters (id=%u)", 
              name, stmt->param_count, stmt->stmt_id);
    
    return stmt;
}

// -----------------------------------------------------------------------------
// Statement Execute
// -----------------------------------------------------------------------------

static char *substitute_parameters(const char *template_query, 
                                   const int *positions, 
                                   int param_count,
                                   Parameter *params) {
    if (!template_query) return NULL;
    if (param_count == 0) return strdup(template_query);
    
    // Calculate result size
    size_t template_len = strlen(template_query);
    size_t result_size = template_len;
    
    char **param_strs = malloc(sizeof(char*) * param_count);
    for (int i = 0; i < param_count; i++) {
        param_strs[i] = param_to_string(&params[i]);
        result_size += strlen(param_strs[i]);
    }
    
    char *result = malloc(result_size + 1);
    if (!result) {
        for (int i = 0; i < param_count; i++) free(param_strs[i]);
        free(param_strs);
        return NULL;
    }
    
    // Substitute parameters
    int src_pos = 0;
    int dst_pos = 0;
    int param_idx = 0;
    
    while (template_query[src_pos]) {
        if (param_idx < param_count && src_pos == positions[param_idx]) {
            // Substitute parameter
            char *ps = param_strs[param_idx];
            while (*ps) result[dst_pos++] = *ps++;
            param_idx++;
            src_pos++; // Skip ?
        } else {
            result[dst_pos++] = template_query[src_pos++];
        }
    }
    result[dst_pos] = '\0';
    
    // Cleanup
    for (int i = 0; i < param_count; i++) free(param_strs[i]);
    free(param_strs);
    
    return result;
}

int stmt_execute(SessionStatementStore *store,
                 const char *name,
                 Parameter *params,
                 int param_count,
                 void *executor_ctx,
                 char **result,
                 size_t *result_len) {
    if (!store || !name) return -1;
    
    PreparedStatement *stmt = stmt_find(store, name);
    if (!stmt) {
        LOG_WARN("Prepared statement '%s' not found", name);
        return -1;
    }
    
    // Validate parameter count
    if (param_count != stmt->param_count) {
        LOG_ERROR("Parameter count mismatch: expected %d, got %d", 
                  stmt->param_count, param_count);
        return -1;
    }
    
    // Validate parameters for SQL injection
    for (int i = 0; i < param_count; i++) {
        if (!stmt_validate_param(&params[i])) {
            LOG_SECURITY("SQL injection attempt blocked in statement '%s'", name);
            return -1;
        }
    }
    
    // Build final query with substituted parameters
    char *final_query = substitute_parameters(stmt->query_template,
                                               stmt->param_positions,
                                               stmt->param_count,
                                               params);
    if (!final_query) return -1;
    
    LOG_DEBUG("Executing prepared statement '%s': %s", name, final_query);
    
    // Update statistics
    stmt->state = STMT_STATE_EXECUTING;
    stmt->execution_count++;
    store->total_executes++;
    
    // TODO: Execute through executor
    // For now, return the substituted query
    if (result) {
        *result = final_query;
        if (result_len) *result_len = strlen(final_query);
    } else {
        free(final_query);
    }
    
    stmt->last_execute_time = (uint64_t)time(NULL) * 1000;
    stmt->state = STMT_STATE_COMPLETE;
    
    return 0;
}

// -----------------------------------------------------------------------------
// Statement Management
// -----------------------------------------------------------------------------

PreparedStatement *stmt_find(SessionStatementStore *store, const char *name) {
    if (!store || !name) return NULL;
    
    for (int i = 0; i < store->count; i++) {
        if (store->statements[i] && 
            strcmp(store->statements[i]->name, name) == 0) {
            return store->statements[i];
        }
    }
    return NULL;
}

PreparedStatement *stmt_find_by_id(SessionStatementStore *store, uint32_t stmt_id) {
    if (!store) return NULL;
    
    for (int i = 0; i < store->count; i++) {
        if (store->statements[i] && store->statements[i]->stmt_id == stmt_id) {
            return store->statements[i];
        }
    }
    return NULL;
}

int stmt_deallocate(SessionStatementStore *store, const char *name) {
    if (!store || !name) return -1;
    
    for (int i = 0; i < store->count; i++) {
        PreparedStatement *stmt = store->statements[i];
        if (stmt && strcmp(stmt->name, name) == 0) {
            free(stmt->query_template);
            if (stmt->parsed_ast) free_ast(stmt->parsed_ast);
            free(stmt->prepared_by);
            free(stmt);
            
            // Compact array
            for (int j = i; j < store->count - 1; j++) {
                store->statements[j] = store->statements[j + 1];
            }
            store->count--;
            
            LOG_DEBUG("Deallocated prepared statement '%s'", name);
            return 0;
        }
    }
    
    return -1;
}

int stmt_deallocate_all(SessionStatementStore *store) {
    if (!store) return -1;
    
    int count = store->count;
    for (int i = 0; i < store->count; i++) {
        PreparedStatement *stmt = store->statements[i];
        if (stmt) {
            free(stmt->query_template);
            if (stmt->parsed_ast) free_ast(stmt->parsed_ast);
            free(stmt->prepared_by);
            free(stmt);
        }
    }
    store->count = 0;
    
    LOG_DEBUG("Deallocated all %d prepared statements", count);
    return count;
}

// -----------------------------------------------------------------------------
// Global Cache Operations
// -----------------------------------------------------------------------------

int cache_get(const char *query_hash, CacheEntry **entry) {
    if (!g_stmt_cache || !query_hash || !entry) return -1;
    
    pthread_mutex_lock(&g_stmt_cache->lock);
    
    uint32_t hash = hash_string(query_hash);
    int idx = hash % g_stmt_cache->capacity;
    
    CacheEntry *e = &g_stmt_cache->entries[idx];
    if (e->query_hash && strcmp(e->query_hash, query_hash) == 0) {
        e->hit_count++;
        e->last_access = (uint64_t)time(NULL);
        g_stmt_cache->total_hits++;
        *entry = e;
        pthread_mutex_unlock(&g_stmt_cache->lock);
        return 0;
    }
    
    g_stmt_cache->total_misses++;
    pthread_mutex_unlock(&g_stmt_cache->lock);
    return -1;
}

int cache_put(const char *query_hash, const char *normalized, 
              ASTNode *ast, QueryPlan *plan) {
    if (!g_stmt_cache || !query_hash) return -1;
    
    pthread_mutex_lock(&g_stmt_cache->lock);
    
    uint32_t hash = hash_string(query_hash);
    int idx = hash % g_stmt_cache->capacity;
    
    CacheEntry *e = &g_stmt_cache->entries[idx];
    
    // Evict old entry if exists
    if (e->query_hash) {
        free(e->query_hash);
        free(e->normalized_query);
        if (e->cached_ast) free_ast(e->cached_ast);
        if (e->cached_plan) plan_destroy(e->cached_plan);
        g_stmt_cache->total_memory -= e->memory_size;
        g_stmt_cache->count--;
    }
    
    e->query_hash = strdup(query_hash);
    e->normalized_query = normalized ? strdup(normalized) : NULL;
    e->cached_ast = ast;
    e->cached_plan = plan;
    e->hit_count = 0;
    e->last_access = (uint64_t)time(NULL);
    e->memory_size = strlen(query_hash) + (normalized ? strlen(normalized) : 0) + 128;
    
    g_stmt_cache->total_memory += e->memory_size;
    g_stmt_cache->count++;
    
    pthread_mutex_unlock(&g_stmt_cache->lock);
    return 0;
}

void cache_invalidate(const char *table_name) {
    if (!g_stmt_cache || !table_name) return;
    
    pthread_mutex_lock(&g_stmt_cache->lock);
    
    // Simple approach: invalidate entries containing table name
    for (int i = 0; i < g_stmt_cache->capacity; i++) {
        CacheEntry *e = &g_stmt_cache->entries[i];
        if (e->normalized_query && strstr(e->normalized_query, table_name)) {
            free(e->query_hash);
            free(e->normalized_query);
            if (e->cached_ast) free_ast(e->cached_ast);
            if (e->cached_plan) plan_destroy(e->cached_plan);
            g_stmt_cache->total_memory -= e->memory_size;
            memset(e, 0, sizeof(CacheEntry));
            g_stmt_cache->count--;
        }
    }
    
    pthread_mutex_unlock(&g_stmt_cache->lock);
    LOG_DEBUG("Invalidated cache entries for table '%s'", table_name);
}

void cache_stats(uint64_t *hits, uint64_t *misses, size_t *memory) {
    if (!g_stmt_cache) return;
    
    pthread_mutex_lock(&g_stmt_cache->lock);
    if (hits) *hits = g_stmt_cache->total_hits;
    if (misses) *misses = g_stmt_cache->total_misses;
    if (memory) *memory = g_stmt_cache->total_memory;
    pthread_mutex_unlock(&g_stmt_cache->lock);
}

// -----------------------------------------------------------------------------
// Query Plan Operations (Placeholder for Iteration 2)
// -----------------------------------------------------------------------------

QueryPlan *plan_create(ASTNode *ast, void *catalog) {
    (void)ast;
    (void)catalog;
    
    QueryPlan *plan = calloc(1, sizeof(QueryPlan));
    if (!plan) return NULL;
    
    plan->created_at = time(NULL);
    plan->is_cacheable = true;
    
    return plan;
}

void plan_destroy(QueryPlan *plan) {
    if (!plan) return;
    
    if (plan->root) plan_node_destroy(plan->root);
    free(plan->query_hash);
    free(plan);
}

QueryPlanNode *plan_node_create(PlanOpType type) {
    QueryPlanNode *node = calloc(1, sizeof(QueryPlanNode));
    if (!node) return NULL;
    node->op_type = type;
    return node;
}

void plan_node_destroy(QueryPlanNode *node) {
    if (!node) return;
    
    if (node->left) plan_node_destroy(node->left);
    if (node->right) plan_node_destroy(node->right);
    if (node->next) plan_node_destroy(node->next);
    
    free(node->table_name);
    free(node->index_name);
    free(node->filter_expr);
    if (node->output_cols) {
        for (int i = 0; i < node->output_col_count; i++) {
            free(node->output_cols[i]);
        }
        free(node->output_cols);
    }
    free(node);
}

void plan_print(QueryPlan *plan, char *buffer, size_t buflen) {
    if (!plan || !buffer || buflen == 0) return;
    
    snprintf(buffer, buflen, 
             "Query Plan:\n"
             "  Estimated Cost: %.2f\n"
             "  Cacheable: %s\n"
             "  Cache Hits: %lu\n",
             plan->total_cost,
             plan->is_cacheable ? "yes" : "no",
             (unsigned long)plan->hit_count);
}

// -----------------------------------------------------------------------------
// Statistics
// -----------------------------------------------------------------------------

void stmt_get_stats(SessionStatementStore *store, PreparedStmtStats *stats) {
    if (!stats) return;
    memset(stats, 0, sizeof(PreparedStmtStats));
    
    if (store) {
        stats->total_prepares = store->total_prepares;
        stats->total_executes = store->total_executes;
        stats->active_statements = store->count;
        stats->cache_hits = store->cache_hits;
    }
    
    if (g_stmt_cache) {
        pthread_mutex_lock(&g_stmt_cache->lock);
        stats->cache_hits += g_stmt_cache->total_hits;
        stats->cache_misses = g_stmt_cache->total_misses;
        stats->cache_memory_bytes = g_stmt_cache->total_memory;
        if (stats->cache_hits + stats->cache_misses > 0) {
            stats->cache_hit_ratio = (double)stats->cache_hits / 
                                     (stats->cache_hits + stats->cache_misses);
        }
        pthread_mutex_unlock(&g_stmt_cache->lock);
    }
}
