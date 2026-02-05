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
