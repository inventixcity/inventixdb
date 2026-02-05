#ifndef INVENTIX_SYSTEM_H
#define INVENTIX_SYSTEM_H

#include "storage.h"

// Database Management
void sys_init(KVStore *store);
int sys_create_db(KVStore *store, const char *dbname);
int sys_db_exists(KVStore *store, const char *dbname);
void sys_show_tables(KVStore *store, const char *current_db, FILE *out);

// Key Generation Helper (Namespaced)
char* sys_generate_key_table(const char *db, const char *table, const char *pk);
char* sys_generate_key_schema(const char *db, const char *table);
char* sys_generate_key_seq(const char *db, const char *table);

#endif
