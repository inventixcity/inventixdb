#ifndef INVENTIX_DIST_H
#define INVENTIX_DIST_H

#include "parser.h"

// Config
#define MAX_WORKERS 2
typedef struct {
    char *ip;
    int port;
} WorkerNode;

void dist_init();
int dist_is_master();
void dist_set_master(int is_master);

// Returns a malloc'd string response from the worker(s)
char* dist_route_query(ASTNode *node, const char *raw_query);

#endif
