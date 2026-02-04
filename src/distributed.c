#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include "distributed.h"
#include "storage.h" // For hash function

static int g_is_master = 0;
static WorkerNode g_workers[MAX_WORKERS];

void dist_init() {
    // Hardcoded workers for demo
    g_workers[0].ip = "127.0.0.1";
    g_workers[0].port = 8889;
    
    g_workers[1].ip = "127.0.0.1";
    g_workers[1].port = 8890;
}

void dist_set_master(int is_master) {
    g_is_master = is_master;
}

int dist_is_master() {
    return g_is_master;
}

// Helper to send to worker
char* send_to_worker(int worker_idx, const char *query) {
    if (worker_idx < 0 || worker_idx >= MAX_WORKERS) return strdup("Error: Invalid worker\n");
    
    // Create socket
    SOCKET sock;
    struct sockaddr_in server;
    
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return strdup("Error: Socket failed\n");
    
    server.sin_addr.s_addr = inet_addr(g_workers[worker_idx].ip);
    server.sin_family = AF_INET;
    server.sin_port = htons(g_workers[worker_idx].port);
    
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        return strdup("Error: Connection to worker failed\n");
    }
    
    // Send
    send(sock, query, strlen(query), 0);
    send(sock, "\n", 1, 0); // Ensure newline
    
    // Receive
    char buffer[4096];
    memset(buffer, 0, 4096);
    int len = recv(sock, buffer, 4096, 0); // Blocking
    
    closesocket(sock);
    
    if (len > 0) return strdup(buffer);
    return strdup("No response\n");
}

char* dist_route_query(ASTNode *node, const char *raw_query) {
    if (!node) return NULL;
    
    // Logic: Partition by ID Hash
    int target_worker = -1;
    
    if (node->type == NODE_CMD_INSERT) {
        // TBL:<name>:<id>
        // Extract ID
        if (node->data.insert.values) {
             char *id = node->data.insert.values->value;
             unsigned long h = hash(id); // Use storage hash
             target_worker = h % MAX_WORKERS;
        }
    } else if (node->type == NODE_CMD_SELECT) {
        // Check if ID lookup
        // Similar logic to executor
        if (node->data.select.where_clause && 
            node->data.select.where_clause->type == NODE_EXPR_BINARY) {
                 if (strcmp(node->data.select.where_clause->data.binary_expr.left->data.literal.value, "id") == 0) {
                     char *id = node->data.select.where_clause->data.binary_expr.right->data.literal.value;
                     unsigned long h = hash(id);
                     target_worker = h % MAX_WORKERS;
                 }
        }
    }
    
    if (target_worker != -1) {
        // Route to specific worker
        printf("[Master] Routing to Worker %d\n", target_worker);
        return send_to_worker(target_worker, raw_query);
    } else {
        // Broadcast (e.g. Full Scan)
        printf("[Master] Broadcasting to all workers\n");
        char *result = malloc(8192); // Large buffer
        result[0] = 0;
        
        for(int i=0; i<MAX_WORKERS; i++) {
            char *part = send_to_worker(i, raw_query);
            strcat(result, "--- Worker Response ---\n");
            strcat(result, part);
            free(part);
        }
        return result;
    }
}
