#ifndef INVENTIX_DIST_H
#define INVENTIX_DIST_H

#include <stdbool.h>
#include "parser.h"

// Config
#define MAX_WORKERS 8  // Increased for scalability
typedef struct {
    char *ip;
    int port;
} WorkerNode;

// Initialization and shutdown
void dist_init(void);
void dist_shutdown(void);

// Master/Worker mode
int dist_is_master(void);
void dist_set_master(int is_master);
int dist_get_worker_count(void);

// Query routing - returns a malloc'd string response from the worker(s)
char* dist_route_query(ASTNode *node, const char *raw_query);

// Protocol control
void dist_set_binary_protocol(bool enable);

#endif
