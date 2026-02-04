#ifndef INVENTIX_EXECUTOR_H
#define INVENTIX_EXECUTOR_H

#include "parser.h"
#include "storage.h"

void execute_query(ASTNode *node, KVStore *store, FILE *out);

#endif
