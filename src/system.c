#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system.h"
#include "storage.h"
#include "colors.h"

void sys_init(KVStore *store) {
    // Create default DB 'public'
    if (!sys_db_exists(store, "public")) {
        sys_create_db(store, "public");
    }
}

int sys_create_db(KVStore *store, const char *dbname) {
    char key[256];
    sprintf(key, "SYS:DB:%s", dbname);
    if (kv_get(store, key)) return 0; // Exists
    
    int dummy = 1;
    kv_put(store, key, &dummy, sizeof(int), VAL_TYPE_ROW);
    return 1;
}

int sys_db_exists(KVStore *store, const char *dbname) {
    char key[256];
    sprintf(key, "SYS:DB:%s", dbname);
    return kv_get(store, key) != NULL;
}

// Context for collecting keys to delete during DROP DATABASE
typedef struct {
    char *prefix;
    char **keys;
    int count;
    int capacity;
} DropDBCtx;

static void drop_db_scan_callback(const char *key, Value *val, void *ctx) {
    (void)val;
    DropDBCtx *c = (DropDBCtx*)ctx;
    if (strncmp(key, c->prefix, strlen(c->prefix)) == 0) {
        if (c->count >= c->capacity) {
            c->capacity *= 2;
            c->keys = realloc(c->keys, sizeof(char*) * c->capacity);
        }
        c->keys[c->count++] = strdup(key);
    }
}

int sys_drop_db(KVStore *store, const char *dbname) {
    // Check existence
    if (!sys_db_exists(store, dbname)) return 0;
    
    // Cannot drop 'public' (system default)
    if (strcmp(dbname, "public") == 0) return -1;
    
    // Collect all keys with prefix "DB:<dbname>:" for deletion
    char prefix[256];
    sprintf(prefix, "DB:%s:", dbname);
    
    DropDBCtx ctx;
    ctx.prefix = prefix;
    ctx.capacity = 64;
    ctx.count = 0;
    ctx.keys = malloc(sizeof(char*) * ctx.capacity);
    
    kv_iterate(store, drop_db_scan_callback, &ctx);
    
    // Delete all collected keys
    for (int i = 0; i < ctx.count; i++) {
        kv_delete(store, ctx.keys[i]);
        free(ctx.keys[i]);
    }
    free(ctx.keys);
    
    // Delete the database registry entry
    char sysKey[256];
    sprintf(sysKey, "SYS:DB:%s", dbname);
    kv_delete(store, sysKey);
    
    return 1;
}

// Context for showing databases
typedef struct {
    FILE *out;
    int count;
} ShowDBCtx;

static void show_db_callback(const char *key, Value *val, void *ctx) {
    (void)val;
    ShowDBCtx *c = (ShowDBCtx*)ctx;
    const char *prefix = "SYS:DB:";
    if (strncmp(key, prefix, strlen(prefix)) == 0) {
        const char *dbname = key + strlen(prefix);
        fprintf(c->out, ANSI_CYAN "  | " ANSI_YELLOW "%-20s" ANSI_CYAN " |\n" ANSI_RESET, dbname);
        c->count++;
    }
}

void sys_show_dbs(KVStore *store, FILE *out) {
    fprintf(out, "\n  " ANSI_BOLD "Available Databases" ANSI_RESET "\n");
    fprintf(out, ANSI_CYAN "  +----------------------+\n");
    fprintf(out, "  | Name                 |\n");
    fprintf(out, "  +----------------------+\n" ANSI_RESET);
    
    ShowDBCtx ctx;
    ctx.out = out;
    ctx.count = 0;
    kv_iterate(store, show_db_callback, &ctx);
    
    if (ctx.count == 0) fprintf(out, ANSI_RED "  | (No databases)       |\n" ANSI_RESET);
    fprintf(out, ANSI_CYAN "  +----------------------+\n" ANSI_RESET "\n");
}

// Callback context for showing tables
typedef struct {
    char *db_prefix;
    FILE *out;
    int count;
} ShowTableCtx;

void show_table_callback(const char *key, Value *val, void *ctx) {
    ShowTableCtx *c = (ShowTableCtx*)ctx;
    (void)val;
    
    if (strncmp(key, c->db_prefix, strlen(c->db_prefix)) == 0) {
        const char *tname = key + strlen(c->db_prefix);
        fprintf(c->out, ANSI_CYAN "  | " ANSI_YELLOW "%-15s" ANSI_CYAN " | " ANSI_GREEN "Table" ANSI_CYAN "  |\n" ANSI_RESET, tname);
        c->count++;
    }
}

void sys_show_tables(KVStore *store, const char *current_db, FILE *out) {
    char prefix[256];
    sprintf(prefix, "DB:%s:META:TBL:", current_db);
    
    ShowTableCtx ctx;
    ctx.db_prefix = prefix;
    ctx.out = out;
    ctx.count = 0;
    
    fprintf(out, "\n  " ANSI_BOLD "List of relations in '%s'" ANSI_RESET "\n", current_db);
    fprintf(out, ANSI_CYAN "  +-----------------+--------+\n");
    fprintf(out, "  | Name            | Type   |\n");
    fprintf(out, "  +-----------------+--------+\n" ANSI_RESET);
    
    kv_iterate(store, show_table_callback, &ctx);
    
    if (ctx.count == 0) fprintf(out, ANSI_RED "  | (No tables)     |        |\n" ANSI_RESET);
    fprintf(out, ANSI_CYAN "  +-----------------+--------+\n" ANSI_RESET "\n");
}

char* sys_generate_key_table(const char *db, const char *table, const char *pk) {
    char *buf = malloc(512);
    sprintf(buf, "DB:%s:TBL:%s:%s", db, table, pk);
    return buf;
}

char* sys_generate_key_schema(const char *db, const char *table) {
    char *buf = malloc(512);
    sprintf(buf, "DB:%s:META:TBL:%s", db, table);
    return buf;
}

char* sys_generate_key_seq(const char *db, const char *table) {
    char *buf = malloc(512);
    sprintf(buf, "DB:%s:SEQ:%s", db, table);
    return buf;
}
