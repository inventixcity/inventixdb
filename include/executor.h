#ifndef INVENTIX_EXECUTOR_H
#define INVENTIX_EXECUTOR_H

#include "parser.h"
#include "storage.h"

typedef struct {
    char current_db[64];
    char current_user[64];
} SessionContext;

void execute_query(ASTNode *node, KVStore *store, SessionContext *ctx, FILE *out);

#endif
