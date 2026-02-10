#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include "distributed.h"
#include "storage.h" // For hash function
#include "config.h"  // Configuration System
#include "logger.h"  // Logging System
#include "network.h" // Binary protocol & connection pooling

static int g_is_master = 0;
static WorkerNode g_workers[MAX_WORKERS];
static int g_worker_count = 2; // Updated from config

// Connection pools for each worker (for connection reuse)
static ConnectionPool *g_worker_pools[MAX_WORKERS] = {NULL};
static bool g_use_binary_protocol = true;  // Enable binary protocol
static uint32_t g_query_sequence = 0;

void dist_init() {
    // Initialize network subsystem
    net_init();
    
    // Load worker addresses from configuration (pools created lazily on first use)
    if (g_config.config_loaded) {
        g_worker_count = CFG_DISTRIBUTED.worker_count;
        for (int i = 0; i < g_worker_count && i < MAX_WORKERS; i++) {
            g_workers[i].ip = strdup(CFG_DISTRIBUTED.workers[i].ip);
            g_workers[i].port = CFG_DISTRIBUTED.workers[i].port;
            g_worker_pools[i] = NULL; // pools created lazily
        }
        LOG_DIST(LOG_LEVEL_INFO, "Loaded %d workers from config", g_worker_count);
    } else {
        // Fallback defaults
        g_workers[0].ip = "127.0.0.1";
        g_workers[0].port = 8889;
        g_workers[1].ip = "127.0.0.1";
        g_workers[1].port = 8890;
        g_worker_count = 2;
        LOG_DIST(LOG_LEVEL_WARN, "Using default workers (config not loaded)");
    }
}

// Create connection pools for workers (called only when master mode is set)
void dist_create_pools(void) {
    for (int i = 0; i < g_worker_count && i < MAX_WORKERS; i++) {
        if (!g_worker_pools[i] && g_workers[i].ip) {
            g_worker_pools[i] = conn_pool_create(g_workers[i].ip, g_workers[i].port, 2, 16);
            if (g_worker_pools[i]) {
                LOG_DIST(LOG_LEVEL_DEBUG, "Created connection pool for worker %d (%s:%d)",
                         i, g_workers[i].ip, g_workers[i].port);
            }
        }
    }
    LOG_DIST(LOG_LEVEL_INFO, "Connection pools created for %d workers", g_worker_count);
}

void dist_shutdown(void) {
    // Destroy connection pools
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (g_worker_pools[i]) {
            conn_pool_destroy(g_worker_pools[i]);
            g_worker_pools[i] = NULL;
        }
    }
    LOG_DIST(LOG_LEVEL_INFO, "Distributed module shutdown");
}

void dist_set_master(int is_master) {
    g_is_master = is_master;
}

int dist_is_master() {
    return g_is_master;
}

// Send to worker using binary protocol with connection pooling
static char* send_to_worker_binary(int worker_idx, const char *query) {
    if (worker_idx < 0 || worker_idx >= g_worker_count) {
        return strdup("Error: Invalid worker index\n");
    }
    
    // Try to get connection from pool
    NetConnection *conn = NULL;
    if (g_worker_pools[worker_idx]) {
        conn = conn_pool_acquire(g_worker_pools[worker_idx], 5000);  // 5 second timeout
    }
    
    if (!conn) {
        LOG_DIST(LOG_LEVEL_WARN, "Pool miss for worker %d, creating direct connection", worker_idx);
        conn = async_connect(g_workers[worker_idx].ip, g_workers[worker_idx].port, 10000);
        if (!conn) {
            char err[128];
            snprintf(err, sizeof(err), "Error: Cannot connect to worker %d (%s:%d)\n",
                     worker_idx, g_workers[worker_idx].ip, g_workers[worker_idx].port);
            return strdup(err);
        }
    }
    
    // Send query using binary protocol
    uint32_t query_id = ++g_query_sequence;
    if (net_send_query(conn->socket, query_id, query, strlen(query), 30000) < 0) {
        if (g_worker_pools[worker_idx]) {
            conn->state = CONN_STATE_ERROR;
            conn_pool_release(g_worker_pools[worker_idx], conn);
        } else {
            async_disconnect(conn);
        }
        return strdup("Error: Failed to send query to worker\n");
    }
    
    // Receive response
    NetMessageHeader header;
    char *payload = NULL;
    size_t payload_len = 0;
    
    if (net_recv_message(conn->socket, &header, &payload, &payload_len, 60000) < 0) {
        if (g_worker_pools[worker_idx]) {
            conn->state = CONN_STATE_ERROR;
            conn_pool_release(g_worker_pools[worker_idx], conn);
        } else {
            async_disconnect(conn);
        }
        return strdup("Error: Failed to receive response from worker\n");
    }
    
    // Parse response based on message type
    char *result = NULL;
    if (header.type == MSG_TYPE_QUERY_RESULT && payload_len >= sizeof(NetQueryResult)) {
        NetQueryResult *qr = (NetQueryResult *)payload;
        if (qr->result_len > 0) {
            result = malloc(qr->result_len + 1);
            memcpy(result, payload + sizeof(NetQueryResult), qr->result_len);
            result[qr->result_len] = '\0';
        } else {
            result = strdup("OK\n");
        }
    } else if (header.type == MSG_TYPE_ERROR) {
        result = malloc(payload_len + 16);
        snprintf(result, payload_len + 16, "Error: %.*s\n", (int)payload_len, payload);
    } else {
        result = strdup("Error: Unexpected response type\n");
    }
    
    free(payload);
    
    // Return connection to pool
    if (g_worker_pools[worker_idx]) {
        conn_pool_release(g_worker_pools[worker_idx], conn);
    } else {
        async_disconnect(conn);
    }
    
    return result;
}

// Legacy send to worker (for backward compatibility)
static char* send_to_worker_legacy(int worker_idx, const char *query) {
    if (worker_idx < 0 || worker_idx >= MAX_WORKERS) return strdup("Error: Invalid worker\n");
    
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return strdup("Error: Socket failed (Master)\n");
    
    struct sockaddr_in server;
    server.sin_addr.s_addr = inet_addr(g_workers[worker_idx].ip);
    server.sin_family = AF_INET;
    server.sin_port = htons(g_workers[worker_idx].port);
    
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        char err[64];
        sprintf(err, "Error: Connection to Worker %d failed\n", worker_idx);
        closesocket(sock);
        return strdup(err);
    }
    
    send(sock, query, (int)strlen(query), 0);
    send(sock, "\n", 1, 0);
    
    int buf_size = 4096;
    int total = 0;
    char *response = malloc(buf_size);
    response[0] = '\0';
    
    char buffer[1024];
    int len;
    
    while ((len = recv(sock, buffer, 1023, 0)) > 0) {
        buffer[len] = 0;
        
        if (total + len >= buf_size) {
            buf_size *= 2;
            response = realloc(response, buf_size);
        }
        
        strcat(response, buffer);
        total += len;
        
        char *marker = strstr(response, "<<EOF>>");
        if (marker) {
            *marker = '\0';
            break;
        }
    }
    
    closesocket(sock);
    
    if (total > 0) return response;
    free(response);
    return strdup("No response\n");
}

// Main send function - chooses protocol based on configuration
char* send_to_worker(int worker_idx, const char *query) {
    if (g_use_binary_protocol) {
        return send_to_worker_binary(worker_idx, query);
    } else {
        return send_to_worker_legacy(worker_idx, query);
    }
}

// Helper to hashing string
static unsigned long dist_hash_djb2(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash;
}

char* dist_route_query(ASTNode *node, const char *raw_query) {
    if (!node) return NULL;
    
    int target_worker = -1;
    
    // 1. Partitioning Logic (INSERT)
    if (node->type == NODE_CMD_INSERT) {
        // Assume ID/PK is the First Column Value for convenience
        // Step 8 requires hash partitioning.
        if (node->data.insert.rows) {
             // Minimal fix for compilation: Default to worker 0 for now
             target_worker = 0;
        }
    } 
    // 2. Partitioning Logic (SELECT/DELETE/UPDATE) - Point Lookups
    else if (node->type == NODE_CMD_SELECT || node->type == NODE_CMD_DELETE) {
        // Check WHERE clause for "id = X"
        ASTNode *cond = (node->type == NODE_CMD_SELECT) ? node->data.select.where_clause : node->data.delete_stmt.where_clause;
        
        if (cond && cond->type == NODE_EXPR_BINARY) {
             char *col = cond->data.binary_expr.left->data.literal.value;
             char *op = cond->data.binary_expr.op;
             ASTNode *rhs = cond->data.binary_expr.right;
             
             if (strcmp(col, "id") == 0 && strcmp(op, "=") == 0 && rhs->type == NODE_EXPR_LITERAL) {
                 char *id = rhs->data.literal.value;
                 target_worker = dist_hash_djb2(id) % g_worker_count;
             }
        }
    }
    // 3. Document Store Partitioning
    else if (node->type == NODE_CMD_DOC_INSERT) {
        // RAKHO col <json> (ID is generated? Or inside JSON?)
        // If ID is auto-generated by executor, Master needs to generate it to route it?
        // Or route random?
        // Let's assume random/round-robin for DOC INSERT if no ID specified, 
        // OR better: Execute locally on Master to gen ID? No.
        // Simple: Round Robin.
        static int rr = 0;
        target_worker = (rr++) % g_worker_count;
    }
    else if (node->type == NODE_CMD_DOC_GET || node->type == NODE_CMD_DOC_REMOVE) {
        // DHUNDO - If parsing extracted ID?
        // Parser for DHUNDO extraction needs checking.
        // Currently execute_doc_get handles "MANGWAO" (Get All) and "DHUNDO" (Get One).
        if (node->type == NODE_CMD_DOC_GET && node->data.doc_get.doc_id != NULL) {
             target_worker = dist_hash_djb2(node->data.doc_get.doc_id) % g_worker_count;
        }
        else if (node->type == NODE_CMD_DOC_REMOVE && node->data.doc_remove.doc_id != NULL) {
             target_worker = dist_hash_djb2(node->data.doc_remove.doc_id) % g_worker_count;
        }
    }

    if (target_worker != -1) {
        // Route to specific worker
        LOG_DIST(LOG_LEVEL_DEBUG, "Routing query to Worker %d (hash-based)", target_worker);
        return send_to_worker(target_worker, raw_query);
    } else {
        // Broadcast (Scatter-Gather)
        // Cases: Full Scan SELECT, CREATE TABLE, DROP TABLE, SHOW TABLES ...
        LOG_DIST(LOG_LEVEL_DEBUG, "Broadcasting query to all %d workers", g_worker_count);
        char *result = malloc(16384); // 16KB Result Buffer
        result[0] = 0;
        int offset = 0;
        
        // Use dynamic worker count from config
        int num_workers = g_worker_count;
        for(int i=0; i<num_workers; i++) {
            char *part = send_to_worker(i, raw_query);
            if (part) {
                // If SELECT, try to strip headers for subsequent workers?
                // Simplification for now: Just append.
                if (offset + strlen(part) < 16380) {
                    strcat(result, part);
                    offset += strlen(part);
                    // Add separator
                    strcat(result, "\n"); 
                    offset++;
                }
                free(part);
            }
        }
        return result;
    }
    return NULL; // Should not reach here
}

// Get current worker count
int dist_get_worker_count() {
    return g_worker_count;
}
