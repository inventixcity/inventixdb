#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "executor.h"
#include "row.h"
#include "auth.h"
#include "system.h"
#include "colors.h"

// Forward decls
void execute_select(ASTNode *node, KVStore *store, SessionContext *ctx, FILE *out);
void resolve_where_clause(ASTNode *where, KVStore *store, SessionContext *ctx); 
void scan_callback(const char *key, Value *val, void *ctx); // Added forward decl

// Global Session removed. Passed via context.

// ---------------------------------------------------------
// QUERY OPTIMIZER & EXECUTOR
// ---------------------------------------------------------

// Helper to sanitize output for table view (replaces \n with space)
void print_cell(FILE *out, char *str) {
    char buf[64];
    int i=0, j=0;
    // Cap at 15 chars for display
    while(str[i] != '\0' && j < 15) {
        if (str[i] == '\n') {
            buf[j++] = ' '; 
        } else {
            buf[j++] = str[i];
        }
        i++;
    }
    buf[j] = '\0';
    // If truncated
    if (j == 15 && str[i] != '\0') {
        buf[12] = '.'; buf[13] = '.'; buf[14] = '.';
    }
    
    fprintf(out, "\033[37m%-15s \033[36m| \033[0m", buf);
}

typedef struct GroupBucket {
    char *key;
    int count;
    double sum_val;
    double min_val;
    double max_val;
    struct GroupBucket *next;
} GroupBucket;

typedef struct {
    char *table_name;
    ASTNode *where_clause;
    KVStore *store;
    SessionContext *session; // Added Session Context
    int col_index_id;
    FILE *out;
    // Cache schema for faster lookups
    char **col_names;
    char **col_types; 
    int col_count;
    // Projection
    int *proj_indices; 
    int proj_count;    
    char **proj_names;
    int *proj_func_types; // 0=None, 1=Count, 2=Sum, 3=Avg, 4=Max, 5=Min

    // Index Scan Limits
    char *scan_stop_val; // If set, stop scanning when current value != this (for equality)
    int scan_col_idx;    // Column index being scanned

    // Subquery Support
    int capture_mode;    // 0=Print, 1=Capture First Value
    char *captured_value;// Result of scalar subquery
    int rows_found;      // To detect multi-row limit

    // Grouping Support
    int group_by_active;
    int group_col_idx;   // Index of the column we are grouping by
    GroupBucket *groups; // Head of buckets
} ScanContext;

char* execute_scalar_subquery(ASTNode *node, KVStore *store, SessionContext *session);

// Helper for Aggregations
void update_bucket(ScanContext *ctx, char *groupKey, char **row_values) {
    GroupBucket *b = ctx->groups;
    while(b) {
        if (strcmp(b->key, groupKey) == 0) break;
        b = b->next;
    }
    if (!b) {
        b = malloc(sizeof(GroupBucket));
        b->key = strdup(groupKey);
        b->count = 0;
        b->sum_val = 0;
        b->min_val = 999999999;
        b->max_val = -999999999;
        b->next = ctx->groups;
        ctx->groups = b;
    }

    // Update Aggregates for ALL projected columns that have functions
    // Note: In this simplified version, we only support aggregation logic if we track which column is being aggregated.
    // The simplified GroupBucket currently only has ONE sum_val, logic is limited.
    // To support `SELECT SUM(age), AVG(salary)`, we'd need a list of aggregates per bucket.
    // For this prototype, checking the FIRST aggregation found in projection:
    
    // Find value to aggregate
    double val = 0;
    int found_agg_col = 0;
    
    // We scan projection to find the Aggregate Function usage
    // This is O(N) per row, acceptable for prototype.
    if (ctx->proj_count > 0 && ctx->proj_func_types) {
        for(int k=0; k<ctx->proj_count; k++) {
            if (ctx->proj_func_types[k] >= 2) { // 2=Sum, 3=Avg, ...
                int idx = ctx->proj_indices[k];
                if (idx < ctx->col_count) {
                    val = atof(row_values[idx]);
                    found_agg_col = 1;
                    // We only support ONE aggregated value per bucket in this struct (sum_val)
                    // If user asks for multiple, results will be weird/mixed.
                    // "Next Level" requires fixing this, but let's stick to simple "SUM(x)" first.
                }
            }
        }
    }

    b->count++;
    if (found_agg_col) {
        b->sum_val += val;
        if (val < b->min_val) b->min_val = val;
        if (val > b->max_val) b->max_val = val;
    }
}

void print_groups(ScanContext *ctx) {
    // Print Header matching executed SELECT
    // ... (Already printed by execute_select) ...
    // But we need to format the rows.
    
    GroupBucket *b = ctx->groups;
    while(b) {
        fprintf(ctx->out, "  \033[36m| \033[0m");

        // We iterate through PROJECTIONS
        // If projection is 'group_key', print key.
        // If projection is 'COUNT', print b->count.
        // If projection is 'SUM', print b->sum.
        
        for(int i=0; i<ctx->proj_count; i++) {
            int func = ctx->proj_func_types[i];
            
            char buf[64];
            if (func == 1) { // COUNT
                sprintf(buf, "%d", b->count);
                print_cell(ctx->out, buf);
            } else if (func == 2) { // SUM
                sprintf(buf, "%.2f", b->sum_val);
                print_cell(ctx->out, buf);
            } else if (func == 3) { // AVG
                if (b->count > 0) sprintf(buf, "%.2f", b->sum_val / b->count);
                else sprintf(buf, "0");
                print_cell(ctx->out, buf);
            } else if (func == 4) { // MAX
                sprintf(buf, "%.2f", b->max_val);
                print_cell(ctx->out, buf);
            } else if (func == 5) { // MIN
                sprintf(buf, "%.2f", b->min_val);
                print_cell(ctx->out, buf);
            } else {
                // Must be the group identifier or something else
                // Check if this column is the group column
                if (ctx->proj_indices[i] == ctx->group_col_idx) {
                    print_cell(ctx->out, b->key);
                } else {
                    print_cell(ctx->out, "..."); // Cannot ensure value for non-grouped cols
                }
            }
        }
        fprintf(ctx->out, "\n");
        b = b->next;
    }
}

char* execute_scalar_subquery(ASTNode *node, KVStore *store, SessionContext *session) {
    if (node->type != NODE_EXPR_SUBQUERY) return NULL;
    ASTNode *select_node = node->data.subquery.subquery_stmt;

    // RESOLVE SUBQUERIES FIRST (Nest Support)
    resolve_where_clause(select_node->data.select.where_clause, store, session);

    ScanContext ctx;
    ctx.table_name = select_node->data.select.table_name;
    ctx.where_clause = select_node->data.select.where_clause;
    ctx.store = store;
    ctx.session = session;
    ctx.out = NULL; // No output
    ctx.scan_stop_val = NULL; 
    ctx.scan_col_idx = -1;
    
    // Capture Mode Init
    ctx.capture_mode = 1; 
    ctx.captured_value = NULL; 
    ctx.rows_found = 0;

    // Fetch Schema
    char *key = sys_generate_key_schema(session->current_db, ctx.table_name);
    Value *schemaVal = kv_get(store, key);
    if (!schemaVal) {
            free(key);
            return NULL; // Table not found
    }

    if (schemaVal && schemaVal->type == VAL_TYPE_SCHEMA) {
        char *schemaStr = strdup((char*)schemaVal->data);
        int cols = 0;
        char *p = schemaStr;
        while(*p) { if (*p == ';') cols++; p++; }
        
        ctx.col_count = cols;
        ctx.col_names = malloc(sizeof(char*) * cols);
        
        // Very basic schema parsing just to map names to indices
        p = strdup((char*)schemaVal->data);
        char *pair = strtok(p, ";");
        int i = 0;
        while(pair && i < cols) {
            char *delim = strchr(pair, ':');
            if (delim) *delim = '\0';
            ctx.col_names[i] = strdup(pair);
            pair = strtok(NULL, ";");
            i++;
        }
        free(p);
        free(schemaStr);
        
        // Calculate Projection Indices (We need exactly 1 column)
        ctx.proj_count = 1;
        ctx.proj_indices = malloc(sizeof(int) * 1);
        ctx.proj_names = NULL;

        NodeList *col_list = select_node->data.select.columns;
        if (col_list) {
                // Find index of this column
                int found = -1;
                for(int j=0; j<ctx.col_count; j++) {
                    if (strcmp(ctx.col_names[j], col_list->value) == 0) {
                        found = j; break;
                    }
                }
                ctx.proj_indices[0] = found; // If -1, will capture NULL
        } else {
             // Should not happen in valid AST
             ctx.proj_indices[0] = 0;
        }

    } else {
        free(key);
        return NULL; 
    }

    // Execute Scan
    // Simple Full Table Scan for now (ignoring indexes for subquery optimization)
    kv_iterate(store, scan_callback, &ctx);
        
    // Cleanup
    if (ctx.col_names) {
        for(int i=0; i<ctx.col_count; i++) free(ctx.col_names[i]);
        free(ctx.col_names);
    }
    if (ctx.proj_indices) free(ctx.proj_indices);
    free(key);
    
    // Return captured value (or "NULL" if none found)
    if (!ctx.captured_value) return strdup("NULL");
    return ctx.captured_value;
}

// Loop up column index from cached schema in ScanContext
int get_col_index(ScanContext *ctx, const char *colName) {
    if (!ctx || !ctx->col_names) return -1;
    for (int i = 0; i < ctx->col_count; i++) {
        if (strcmp(ctx->col_names[i], colName) == 0) return i;
    }
    return -1;
}

int evaluate_condition(ASTNode *cond, char **row_values, int col_count, ScanContext *ctx) {
    if (!cond) return 1;

    if (cond->type == NODE_EXPR_BINARY) {
        char *op = cond->data.binary_expr.op;
        
        // Logical Operators (Recursive)
        if (strcmp(op, "AND") == 0) {
            return evaluate_condition(cond->data.binary_expr.left, row_values, col_count, ctx) &&
                   evaluate_condition(cond->data.binary_expr.right, row_values, col_count, ctx);
        }
        if (strcmp(op, "OR") == 0) {
            return evaluate_condition(cond->data.binary_expr.left, row_values, col_count, ctx) ||
                   evaluate_condition(cond->data.binary_expr.right, row_values, col_count, ctx);
        }

        // Comparison Operators
        char *colName = cond->data.binary_expr.left->data.literal.value;
        int colIdx = get_col_index(ctx, colName);
        
        if (colIdx == -1 || colIdx >= col_count) return 0; 
        
        char *cellVal = row_values[colIdx];
        char *rightVal = NULL;
        
        if (cond->data.binary_expr.right->type == NODE_EXPR_LITERAL) {
            rightVal = cond->data.binary_expr.right->data.literal.value;
        } else if (cond->data.binary_expr.right->type == NODE_EXPR_SUBQUERY) {
             // Subqueries should have been resolved before this by resolve_where_clause via replacement
             // If we are here, it means it wasn't valid or resolution failed.
             return 0;
        }

        if (!rightVal) return 0;

        int res = 0;
        int is_numeric = 0;
        if (ctx && ctx->col_types && colIdx >= 0) {
             char *t = ctx->col_types[colIdx];
             if (t && (strcasecmp(t, "INT")==0 || strcasecmp(t, "FLOAT")==0 || strcasecmp(t, "INTEGER")==0 || strcasecmp(t, "DOUBLE")==0)) {
                 is_numeric = 1;
             }
        }

        if (is_numeric) {
             double lv = atof(cellVal);
             double rv = atof(rightVal);
             if (strcmp(op, "=") == 0) res = (lv == rv);
             else if (strcmp(op, ">") == 0) res = (lv > rv);
             else if (strcmp(op, "<") == 0) res = (lv < rv);
        } else {
             int cmp = strcmp(cellVal, rightVal);
             if (strcmp(op, "=") == 0) res = (cmp == 0);
             else if (strcmp(op, ">") == 0) res = (cmp > 0);
             else if (strcmp(op, "<") == 0) res = (cmp < 0);
        }
        
        return res;
    }
    return 1;
}

// Updated context to include store for subqueries
typedef struct {
     ScanContext base;
     KVStore *store; // Redundant but explicit
} FullScanContext;


void scan_callback(const char *key, Value *val, void *ctx) {
    ScanContext *scan = (ScanContext*)ctx;
    
    // Stop scanning if we already found a scalar value (Optimization)
    if (scan->capture_mode && scan->rows_found > 0) return;

    char prefix[256];
    sprintf(prefix, "DB:%s:TBL:%s:", scan->session->current_db, scan->table_name);
    
    if (strncmp(key, prefix, strlen(prefix)) == 0) {
        // Deserialize row
        int count = 0;
        char **tokens = row_deserialize(val->data, val->size, &count);
        
        if (!tokens) return;
        
        if (evaluate_condition(scan->where_clause, tokens, count, scan)) {
             
             if (scan->capture_mode) {
                 scan->rows_found++;
                 // ... (Scalar Logic) ...
                 int target_col = (scan->proj_count > 0) ? scan->proj_indices[0] : 0;
                 if (target_col < count) scan->captured_value = strdup(tokens[target_col]);
                 row_free_result(tokens, count);
                 return;
             }

             // Grouping Logic
             if (scan->group_by_active) {
                 char *groupKey = "ALL"; // Default if no group col (Implicit Group)
                 if (scan->group_col_idx >= 0 && scan->group_col_idx < count) {
                     groupKey = tokens[scan->group_col_idx];
                 }
                 update_bucket(scan, groupKey, tokens);
                 row_free_result(tokens, count);
                 return;
             }

            // Standard Print Logic
            fprintf(scan->out, "  \033[36m| \033[0m");
            
            int print_cols = scan->proj_count > 0 ? scan->proj_count : count;
            
            for(int i=0; i<print_cols; i++) {
                int col_idx = (scan->proj_count > 0) ? scan->proj_indices[i] : i;
                
                // Aggregate Function check (Print placeholders if user did SELECT COUNT(*) without GROUP BY)
                // Actually, standard SQL says SELECT COUNT(*) without GROUP BY is an implicit single group.
                // My logic above handles "ALL" group key.
                // So if we are here, it means NO Group By and NO Aggregates were detected?
                // Wait, if user does `SELECT COUNT(*) FROM users` without group by, my parser sets `group_by` to NULL.
                // I need to set `group_by_active = 1` implicitely in `execute_select` if aggregates exist.
                
                if (col_idx < count) {
                     print_cell(scan->out, tokens[col_idx]);
                } else {
                     print_cell(scan->out, "NULL");
                }
            }
            fprintf(scan->out, "\n");
        }
        row_free_result(tokens, count);
    }
}

// Resolve RHS of binary expr if it's a subquery
void resolve_where_clause(ASTNode *where, KVStore *store, SessionContext *ctx) {
    if (!where) return;
    if (where->type == NODE_EXPR_BINARY) {
        if (where->data.binary_expr.right->type == NODE_EXPR_SUBQUERY) {
             char *val = execute_scalar_subquery(where->data.binary_expr.right, store, ctx);
             // Replace AST node with literal
             // free old node logic ignored for now
             ASTNode *lit = malloc(sizeof(ASTNode));
             lit->type = NODE_EXPR_LITERAL;
             lit->data.literal.value = val;
             where->data.binary_expr.right = lit;
             printf("Resolved Subquery to: %s\n", val);
        }
    }
}

void execute_create_table(ASTNode *node, KVStore *store, SessionContext *ctx, FILE *out) {
    char *key = sys_generate_key_schema(ctx->current_db, node->data.create_table.table_name);
    
    // Check if table exists
    if (kv_get(store, key) != NULL) {
        fprintf(out, "Error: Table '%s' already exists.\n", node->data.create_table.table_name);
        free(key);
        return;
    }
    
    // Key is consumed by kv_put if we pass it? No kv_put copies.
    // We should free buffer.
    
    char schema[1024] = "";
    // int pkIndex = -1; // Unused for now
    for(int i=0; i<node->data.create_table.col_count; i++) {
        if (node->data.create_table.columns[i].is_pk) {
            // pkIndex = i;
            // Also store logic for this?
        }
        strcat(schema, node->data.create_table.columns[i].name);
        strcat(schema, ":");
        strcat(schema, node->data.create_table.columns[i].type);
        if (node->data.create_table.columns[i].is_pk) strcat(schema, ":PK");
        strcat(schema, ";");
    }
    
    // Store PK index separately in meta?
    // or parse from schema string.
    // Let's store schema string with :PK suffix.
    
    kv_put(store, key, schema, strlen(schema)+1, VAL_TYPE_SCHEMA);
    free(key);
    
    // Initialize Auto Increment Sequence
    char *seqKey = sys_generate_key_seq(ctx->current_db, node->data.create_table.table_name);
    int startSeq = 0;
    kv_put(store, seqKey, &startSeq, sizeof(int), VAL_TYPE_ROW); // Type Row abuse for int
    free(seqKey);

    fprintf(out, "Table '%s' created.\n", node->data.create_table.table_name);
}

void execute_insert(ASTNode *node, KVStore *store, SessionContext *ctx, FILE *out) {
    if (!node->data.insert.rows) return;
    
    // Determine PK index/metadata once
    char *metaKey = sys_generate_key_schema(ctx->current_db, node->data.insert.table_name);
    Value *schemaVal = kv_get(store, metaKey);
    int pkIdx = 0; // Default first col
    char *s_copy = NULL;

    if (schemaVal && schemaVal->type == VAL_TYPE_SCHEMA) {
        s_copy = strdup((char*)schemaVal->data);
        char *p = strtok(s_copy, ";");
        int idx = 0;
        while (p) {
             if (strstr(p, ":PK")) {
                 pkIdx = idx;
                 break;
             }
             p = strtok(NULL, ";");
             idx++;
        }
    }
    if (s_copy) free(s_copy);

    int count = 0;
    RowValueList *currentRow = node->data.insert.rows;
    
    while (currentRow) {
        NodeList *vals = currentRow->values;
        
        // Find value at pkIdx
        NodeList *curr = vals;
        int k=0;
        while(curr && k < pkIdx) { curr=curr->next; k++; }
        
        if (!curr) { 
            fprintf(out, "Error: PK value missing in row %d\n", count+1); 
            currentRow = currentRow->next;
            continue; 
        }
        
        char *id = curr->value; 
        char finalId[64];
        
        // Auto Increment Logic
        if (strcmp(id, "AUTO") == 0) {
            char *seqKey = sys_generate_key_seq(ctx->current_db, node->data.insert.table_name);
            Value *v = kv_get(store, seqKey);
            int seq = 1;
            if (v) {
                seq = *(int*)v->data + 1;
            }
            kv_put(store, seqKey, &seq, sizeof(int), VAL_TYPE_ROW);
            sprintf(finalId, "%d", seq);
            free(seqKey);
            
            // Update the value in the list
            free(curr->value);
            curr->value = strdup(finalId);
        } else {
            strcpy(finalId, id);
            // Update Sequence if explicit ID is numeric
            if (strspn(finalId, "0123456789") == strlen(finalId)) {
                char *seqKey = sys_generate_key_seq(ctx->current_db, node->data.insert.table_name);
                Value *v = kv_get(store, seqKey);
                int currentSeq = 0;
                if (v) currentSeq = *(int*)v->data;
                
                int insertedId = atoi(finalId);
                if (insertedId > currentSeq) {
                    kv_put(store, seqKey, &insertedId, sizeof(int), VAL_TYPE_ROW);
                }
                free(seqKey);
            }
        }

        char *key = sys_generate_key_table(ctx->current_db, node->data.insert.table_name, finalId);
        
        // Duplicate Check
        if (kv_get(store, key) != NULL) {
            fprintf(out, "Warning: Duplicate entry for Primary Key '%s' (Skipped).\n", finalId);
            free(key);
            currentRow = currentRow->next;
            continue;
        }
        
        size_t rowSize;
        void *rowBin = row_serialize(vals, &rowSize);
        
        if (rowBin) {
            kv_put(store, key, rowBin, rowSize, VAL_TYPE_ROW);
            free(rowBin);

            // Update Indexes
            if (schemaVal && schemaVal->type == VAL_TYPE_SCHEMA) {
                char indexStr[2048] = "";
                char *s2 = strdup((char*)schemaVal->data);
                char *p = strtok(s2, ";"); 
                NodeList *v = vals;
                while(p && v) {
                    char *colon = strchr(p, ':');
                    if (colon) {
                        *colon = 0; 
                        strcat(indexStr, p);
                        strcat(indexStr, ":");
                        strcat(indexStr, v->value);
                        strcat(indexStr, ";");
                    }
                    p = strtok(NULL, ";"); 
                    v = v->next;
                }
                free(s2);
                kv_update_indexes(store, node->data.insert.table_name, key, indexStr);
            }
            count++;
        }
        free(key);
        currentRow = currentRow->next;
    }
    free(metaKey);
    fprintf(out, "Inserted %d row(s) into '%s'.\n", count, node->data.insert.table_name);
}

void scan_callback(const char *key, Value *val, void *ctx);

// Callback for Index Range Scan
// Returns 0 to stop scanning, 1 to continue.
int index_scan_callback(const char *pk, const char *val, void *ctx) {
    ScanContext *scan = (ScanContext*)ctx;
    // printf("[DEBUG] Scan Callback: PK=%s Val=%s\n", pk, val);
    // Optimization: Stop if we passed the range (for Equality Check)
    if (scan->scan_stop_val) {
        // Since the index is sorted, if current val != stop val, we are done.
        // Assuming we started at stop_val.
        if (strcmp(val, scan->scan_stop_val) != 0) {
            return 0; // stop
        }
    }
    
    Value *v = kv_get(scan->store, pk);
    if (v) {
        scan_callback(pk, v, ctx);
    }
    return 1;
}

void execute_select(ASTNode *node, KVStore *store, SessionContext *session, FILE *out) {
    // RESOLVE SUBQUERIES FIRST
    resolve_where_clause(node->data.select.where_clause, store, session);

    ScanContext ctx;
    ctx.table_name = node->data.select.table_name;
    ctx.where_clause = node->data.select.where_clause;
    ctx.store = store;
    ctx.session = session;
    ctx.out = out;
    ctx.scan_stop_val = NULL; 
    ctx.scan_col_idx = -1;

    int distinct_lookup = 0;
    char *lookup_id = NULL;
    
    int index_scan = 0;
    char *idx_col = NULL;
    char *idx_val = NULL;
    char *idx_op = NULL;

    if (node->data.select.where_clause && 
        node->data.select.where_clause->type == NODE_EXPR_BINARY) {
        
        ASTNode *bin = node->data.select.where_clause;
        char *op = bin->data.binary_expr.op;
        char *col = bin->data.binary_expr.left->data.literal.value;
        char *val = NULL;
        if (bin->data.binary_expr.right->type == NODE_EXPR_LITERAL) {
             val = bin->data.binary_expr.right->data.literal.value;
        }

        if (col && op && val) {
            if (strcmp(col, "id") == 0 && strcmp(op, "=") == 0) {
                 distinct_lookup = 1;
                 lookup_id = val;
            } else if (kv_has_index(store, ctx.table_name, col)) {
                 if (strcmp(op, "=") == 0 || strcmp(op, ">") == 0) {
                      index_scan = 1;
                      idx_col = col; 
                      idx_val = val; 
                      idx_op = op;
                 }
            }
        }
    }
    
    // Fetch Schema
    char *key = sys_generate_key_schema(session->current_db, ctx.table_name);
    Value *schemaVal = kv_get(store, key);
    if (!schemaVal) {
            fprintf(out, "Error: Table '%s' does not exist.\n", ctx.table_name);
            free(key);
            return;
    }

    // Initialize Grouping
    ctx.group_by_active = 0;
    ctx.groups = NULL;
    ctx.group_col_idx = -1;
    ctx.proj_func_types = NULL;

    if (schemaVal && schemaVal->type == VAL_TYPE_SCHEMA) {
        char *schemaStr = strdup((char*)schemaVal->data);
        int cols = 0;
        char *p = schemaStr;
        while(*p) { if (*p == ';') cols++; p++; }
        
        ctx.col_count = cols;
        ctx.col_names = malloc(sizeof(char*) * cols);
        ctx.col_types = malloc(sizeof(char*) * cols);
        
        p = strdup((char*)schemaVal->data); 
        char *pair = strtok(p, ";");
        int i = 0;
        while(pair && i < cols) {
            char *delim = strchr(pair, ':');
            if (delim) {
                *delim = '\0';
                ctx.col_names[i] = strdup(pair);
                
                char *typePart = delim + 1;
                char *delim2 = strchr(typePart, ':');
                if (delim2) *delim2 = '\0';
                ctx.col_types[i] = strdup(typePart);
            } else {
                 ctx.col_names[i] = strdup(pair);
                 ctx.col_types[i] = strdup("STRING"); 
            }
            pair = strtok(NULL, ";");
            i++;
        }
        free(p);
        free(schemaStr);
        
        // Calculate Projection Indices
        ctx.proj_count = 0;
        ctx.proj_indices = NULL;
        ctx.proj_names = NULL;

        NodeList *col_list = node->data.select.columns;
        if (col_list) {
                if (strcmp(col_list->value, "*") == 0 && col_list->func_type == 0) {
                    ctx.proj_count = 0; // Use all
                } else {
                    int req = 0; 
                    NodeList *temp = col_list;
                    while(temp) { req++; temp=temp->next; }
                    
                    ctx.proj_count = req;
                    ctx.proj_indices = malloc(sizeof(int) * req);
                    ctx.proj_names = malloc(sizeof(char*) * req);
                    ctx.proj_func_types = malloc(sizeof(int) * req);
                    
                    temp = col_list;
                    int k=0;
                    int has_aggregates = 0;
                    while(temp) {
                        int found = -1;
                        // Special handling for COUNT(*): value is *
                         if (strcmp(temp->value, "*") == 0 && temp->func_type == 1) {
                              ctx.proj_indices[k] = 0; // Any col works for count(*)
                              ctx.proj_names[k] = strdup("COUNT(*)");
                         } else {
                            for(int j=0; j<ctx.col_count; j++) {
                                if (strcmp(ctx.col_names[j], temp->value) == 0) {
                                    found = j;
                                    break;
                                }
                            }
                            if (found == -1) ctx.proj_indices[k] = 999; 
                            else ctx.proj_indices[k] = found;
                            ctx.proj_names[k] = strdup(temp->value);
                         }

                        ctx.proj_func_types[k] = temp->func_type;
                        if (temp->func_type > 0) has_aggregates = 1;

                        k++;
                        temp = temp->next;
                    }
                    
                    // Logic for Implicit Grouping
                    if (has_aggregates && !node->data.select.group_by) {
                        ctx.group_by_active = 1;
                        ctx.group_col_idx = -1; // -1 means "All rows in one bucket"
                    }
                }
        }
        
        // Explicit Group By
        if (node->data.select.group_by) {
            ctx.group_by_active = 1;
            char *gName = node->data.select.group_by->value;
            int found = -1;
            for(int j=0; j<ctx.col_count; j++) {
                if (strcmp(ctx.col_names[j], gName) == 0) {
                    found = j;
                    break;
                }
            }
            if (found == -1) {
                fprintf(out, "Error: Unknown Group By column '%s'\n", gName);
                ctx.group_by_active = 0; // Disable to fail gracefully
            } else {
                ctx.group_col_idx = found;
            }
        }

    } else {
        ctx.col_count = 0;
        ctx.col_names = NULL;
    }


    if (distinct_lookup) {
        fprintf(out, "Execution Plan: PRIMARY KEY INDEX LOOKUP\n");
    } else if (index_scan) {
        fprintf(out, "Execution Plan: INDEX SCAN ON %s %s %s\n", idx_col, idx_op, idx_val);
    } else {
        fprintf(out, "Execution Plan: FULL TABLE SCAN\n");
    }

    // Print Header
    if (ctx.col_names) {
        int print_cols = ctx.proj_count > 0 ? ctx.proj_count : ctx.col_count;
        
        fprintf(out, "\n  \033[33m+");
        for(int i=0; i<print_cols; i++) fprintf(out, "----------------+");
        fprintf(out, "\033[0m\n  \033[36m| \033[0m");
        for(int i=0; i<print_cols; i++) {
            char *name = (ctx.proj_count > 0) ? ctx.proj_names[i] : ctx.col_names[i];
            fprintf(out, "\033[1;33m%-15s \033[36m| \033[0m", name);
        }
        fprintf(out, "\n  \033[33m+");
        for(int i=0; i<print_cols; i++) fprintf(out, "----------------+");
        fprintf(out, "\033[0m\n");
    }

    if (distinct_lookup) {
        char *key = sys_generate_key_table(session->current_db, node->data.select.table_name, lookup_id);
        Value *val = kv_get(store, key);
        if (val) {
            scan_callback(key, val, &ctx);
        } else {
            fprintf(out, "  | \033[31mNo data found   \033[0m|\n");
        }
        free(key);
    } else if (index_scan) {
         if (strcmp(idx_op, "=") == 0) ctx.scan_stop_val = idx_val;
         
         kv_scan_index_range(store, ctx.table_name, idx_col, idx_val, 
                             (strcmp(idx_op, "=") == 0), 
                             index_scan_callback, &ctx);
    } else {
        kv_iterate(store, scan_callback, &ctx);
    }
    
    if (ctx.group_by_active) {
         print_groups(&ctx);
         // Cleanup Groups
         while(ctx.groups) {
             GroupBucket *n = ctx.groups->next;
             // free(ctx.groups->key); // Likely referenced from tokens which were freed? No, strdup in update_bucket.
             free(ctx.groups);
             ctx.groups = n;
         }
    }
        
    // Footer line
    if (ctx.col_names) {
         int print_cols = ctx.proj_count > 0 ? ctx.proj_count : ctx.col_count;
         fprintf(out, "  \033[33m+");
         for(int i=0; i<print_cols; i++) fprintf(out, "----------------+");
         fprintf(out, "\033[0m\n\n");
    }

    // Cleanup
    if (ctx.col_names) {
        for(int i=0; i<ctx.col_count; i++) free(ctx.col_names[i]);
        free(ctx.col_names);
    }
    if (ctx.proj_indices) free(ctx.proj_indices);
    if (ctx.proj_func_types) free(ctx.proj_func_types);
    if (ctx.proj_names) {
         for(int i=0; i<ctx.proj_count; i++) free(ctx.proj_names[i]);
         free(ctx.proj_names);
    }    
    free(key);
}

void execute_create_user(ASTNode *node, KVStore *store, SessionContext *ctx, FILE *out) {
    (void)ctx;
    if (auth_create_user(store, node->data.create_user.username, node->data.create_user.password)) {
        fprintf(out, "User '%s' created.\n", node->data.create_user.username);
    } else {
        fprintf(out, "Error: User '%s' already exists.\n", node->data.create_user.username);
    }
}

void execute_create_db(ASTNode *node, KVStore *store, SessionContext *ctx, FILE *out) {
    (void)ctx;
    if (sys_create_db(store, node->data.create_db.db_name)) {
        fprintf(out, "Database '%s' created.\n", node->data.create_db.db_name);
    } else {
        fprintf(out, "Error: Database '%s' already exists.\n", node->data.create_db.db_name);
    }
}

void execute_show_tables(ASTNode *node, KVStore *store, SessionContext *ctx, FILE *out) {
    (void)node; // Unused
    sys_show_tables(store, ctx->current_db, out);
}

void execute_doc_insert(ASTNode *node, KVStore *store, SessionContext *ctx, FILE *out) {
    int id = rand() % 10000;
    char key[256];
    sprintf(key, "DB:%s:DOC:%s:%d", ctx->current_db, node->data.doc_insert.collection, id);
    
    kv_put(store, key, node->data.doc_insert.json_body, strlen(node->data.doc_insert.json_body)+1, VAL_TYPE_JSON);
    fprintf(out, "Document saved with key: %s\n", key);
}

// Callback for MANGWAO (Get All)
void doc_scan_callback(const char *key, Value *val, void *ctx) {
    ScanContext *scan = (ScanContext*)ctx;
    char prefix[256];
    sprintf(prefix, "DB:%s:DOC:%s:", scan->session->current_db, scan->table_name);
    
    if (strncmp(key, prefix, strlen(prefix)) == 0) {
        if (val->type == VAL_TYPE_JSON) {
            fprintf(scan->out, ANSI_CYAN "- Key: " ANSI_RESET "%s\n  " ANSI_YELLOW "Val: " ANSI_RESET "%s\n", key, (char*)val->data);
        }
    }
}

void execute_doc_get(ASTNode *node, KVStore *store, SessionContext *ctx, FILE *out) {
    if (node->data.doc_get.doc_id) {
        // Get One
         char key[256];
         sprintf(key, "DB:%s:DOC:%s:%s", ctx->current_db, node->data.doc_get.collection, node->data.doc_get.doc_id);
         
         Value *val = kv_get(store, key);
         
         if (val && val->type == VAL_TYPE_JSON) {
             fprintf(out, ANSI_GREEN "Document Found:\n" ANSI_RESET);
             fprintf(out, ANSI_CYAN "Key: " ANSI_RESET "%s\n", key);
             fprintf(out, ANSI_YELLOW "Body: " ANSI_RESET "%s\n", (char*)val->data);
         } else {
             fprintf(out, ANSI_RED "Document not found.\n" ANSI_RESET);
         }
    } else {
        // Get All
        ScanContext sctx;
        sctx.table_name = node->data.doc_get.collection;
        sctx.store = store;
        sctx.out = out;
        sctx.session = ctx;
        fprintf(out, ANSI_BOLD "\nCollection: %s (DB: %s)\n" ANSI_RESET, node->data.doc_get.collection, ctx->current_db);
        kv_iterate(store, doc_scan_callback, &sctx);
        fprintf(out, "\n");
    }
}

void execute_doc_remove(ASTNode *node, KVStore *store, SessionContext *ctx, FILE *out) {
     char key[256];
     sprintf(key, "DB:%s:DOC:%s:%s", ctx->current_db, node->data.doc_remove.collection, node->data.doc_remove.doc_id);
     
     if (kv_get(store, key)) {
         kv_delete(store, key);
         fprintf(out, "Document removed: %s\n", key);
         return;
     } 
     fprintf(out, "Error: Document not found.\n");
}

void execute_delete(ASTNode *node, KVStore *store, SessionContext *ctx, FILE *out) {
    if (node->data.delete_stmt.where_clause && 
        node->data.delete_stmt.where_clause->type == NODE_EXPR_BINARY) {
         ASTNode *bin = node->data.delete_stmt.where_clause;
         if (strcmp(bin->data.binary_expr.op, "=") == 0 && 
             strcmp(bin->data.binary_expr.left->data.literal.value, "id") == 0) {
                 
             char *id = bin->data.binary_expr.right->data.literal.value;
             char *key = sys_generate_key_table(ctx->current_db, node->data.delete_stmt.table_name, id);
             kv_delete(store, key);
             fprintf(out, "Deleted key: %s\n", key);
             free(key);
             return;
         }
    }
    // Fallback: If NIKALO command doesn't match ID lookup, we need generic scan-and-delete.
    // Assuming simple ID delete for this step based on prompt requirements.
    fprintf(out, "Generic Delete requires ID currently (e.g. id=1).\n");
}

void execute_drop_table(ASTNode *node, KVStore *store, SessionContext *ctx, FILE *out) {
    char *metaKey = sys_generate_key_schema(ctx->current_db, node->data.drop_table.table_name);
    
    if (kv_get(store, metaKey) == NULL) {
        fprintf(out, "Error: Table '%s' does not exist.\n", node->data.drop_table.table_name);
        free(metaKey);
        return;
    }
    
    // Naively, we should iterate and delete all rows.
    // KVStore needs prefix delete support for "TBL:name:*".
    // For now, I will just delete the metadata, making it "invisible" (soft drop).
    // A real implementation would scan keys and delete.
    
    // Let's at least try to clean up if we can iterate safely.
    // BUT iteration inside mutation might deadlock or be complex.
    // Simpler: Just delete META and SEQ. The data becomes orphaned. 
    // This is "acceptable" for a prototype.
    
    kv_delete(store, metaKey);
    free(metaKey);
    
    char *seqKey = sys_generate_key_seq(ctx->current_db, node->data.drop_table.table_name);
    kv_delete(store, seqKey);
    free(seqKey);
    
    fprintf(out, "Table '%s' dropped (Metadata removed).\n", node->data.drop_table.table_name);
}

void execute_checkpoint(ASTNode *node, KVStore *store, SessionContext *ctx, FILE *out) {
    (void)node; // Unused
    (void)ctx;
    fprintf(out, "Initiating Checkpoint...\n");
    kv_snapshot(store);
    fprintf(out, "Checkpoint Complete. Log Truncated.\n");
}

void execute_create_index(ASTNode *node, KVStore *store, SessionContext *ctx, FILE *out) {
    // 1. Create Index Metadata
    // 2. Populate Index with existing data? (Skip for now, or scan all)
    // We will build it fresh.
    fprintf(out, "Index created on %s(%s) [Optimized]\n", node->data.create_index.table_name, node->data.create_index.col_name);
    // Real implementation requires storage support for secondary indexes.
    
    // Namespace table name for index
    char table_full[256];
    sprintf(table_full, "DB:%s:TBL:%s", ctx->current_db, node->data.create_index.table_name);
    
    kv_create_index(store, table_full, node->data.create_index.col_name);
}

void execute_use_db(ASTNode *node, KVStore *store, SessionContext *ctx, FILE *out) {
    // Check if public (always available)
    if (strcmp(node->data.use_db.db_name, "public") == 0) {
         strcpy(ctx->current_db, "public");
         fprintf(out, ANSI_GREEN "Switched to database 'public'.\n" ANSI_RESET);
         return;
    }
    
    // Check Existence
    if (sys_db_exists(store, node->data.use_db.db_name)) {
        strcpy(ctx->current_db, node->data.use_db.db_name);
        fprintf(out, ANSI_GREEN "Switched to database '%s'.\n" ANSI_RESET, ctx->current_db);
    } else {
        fprintf(out, "Error: Database '%s' does not exist.\n", node->data.use_db.db_name);
    }
}

void execute_query(ASTNode *node, KVStore *store, SessionContext *ctx, FILE *out) {
    if (!node) return;
    switch(node->type) {
        case NODE_CMD_CREATE_TABLE:
            execute_create_table(node, store, ctx, out);
            break;
        case NODE_CMD_CREATE_INDEX:
            execute_create_index(node, store, ctx, out);
            break;
        case NODE_CMD_DROP_TABLE:
            execute_drop_table(node, store, ctx, out);
            break;
        case NODE_CMD_CHECKPOINT:
            execute_checkpoint(node, store, ctx, out);
            break;
        case NODE_CMD_INSERT:
            execute_insert(node, store, ctx, out);
            break;
        case NODE_CMD_SELECT:
            execute_select(node, store, ctx, out);
            break;
        case NODE_CMD_DOC_INSERT:
            execute_doc_insert(node, store, ctx, out);
            break;
        case NODE_CMD_DOC_GET:
            execute_doc_get(node, store, ctx, out);
            break;
        case NODE_CMD_DOC_REMOVE:
            execute_doc_remove(node, store, ctx, out);
            break;
        case NODE_CMD_DELETE:
            execute_delete(node, store, ctx, out);
            break;
        case NODE_CMD_CREATE_USER:
            execute_create_user(node, store, ctx, out);
            break;
        case NODE_CMD_CREATE_DB:
            execute_create_db(node, store, ctx, out);
            break;
        case NODE_CMD_SHOW_TABLES:
            execute_show_tables(node, store, ctx, out);
            break;
        case NODE_CMD_USE_DB:
             execute_use_db(node, store, ctx, out);
             break;
        default:
            fprintf(out, ANSI_RED "Executor: Unknown node type %d.\n" ANSI_RESET, node->type);
    }
}
