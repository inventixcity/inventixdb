/**
 * InventixDB Network Server v1.0
 * 
 * Features:
 * - PBKDF2 password hashing (bcrypt-style)
 * - Role-Based Access Control (RBAC)
 * - Session management with tokens
 * - Improved professional interface
 * - Distributed query routing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "lexer.h"
#include "parser.h"
#include "executor.h"
#include "storage.h"
#include "config.h"
#include "logger.h"
#include "distributed.h"
#include "security.h"
#include "memory.h"
#include "network.h"
#include "prepared.h"
#include "index.h"
#include "optimizer.h"
#include "join.h"
#include "mvcc.h"
#include "query_result.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")

// strcasestr for Windows
#ifdef _WIN32
static char *strcasestr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++; n++;
        }
        if (!*n) return (char *)haystack;
    }
    return NULL;
}
#endif

// ============================================================================
// CONFIGURATION
// ============================================================================

#define SERVER_VERSION "1.2.0"
#define USE_ASYNC_IO 1              // Enable IOCP async I/O
#define ENABLE_CONNECTION_POOL 1    // Enable connection pooling for workers
#define BUFFER_SIZE 8192
#define MAX_CLIENTS 256
#define SESSION_MEMORY_QUOTA (8 * 1024 * 1024)  // 8 MB per session
#define QUERY_MEMORY_QUOTA   (2 * 1024 * 1024)  // 2 MB per query

static int g_port = 8888;
static int g_running = 1;
static int g_client_count = 0;
static pthread_mutex_t g_client_lock;

KVStore *global_store;

// Global memory pool for client connections
static MemPool *g_conn_pool = NULL;

// ============================================================================
// CLIENT SESSION
// ============================================================================

typedef struct {
    SOCKET socket;
    char client_ip[64];
    int client_port;
    Session *session;
    char auth_token[SECURITY_TOKEN_LENGTH + 1];
    bool authenticated;
    SessionContext ctx;
    
    // Memory management
    SessionMemory *mem;          // Per-session memory context
    Arena *query_arena;          // Per-query arena (reset each query)
    size_t queries_executed;
    size_t memory_used;
    
    // Protocol state
    bool use_binary_protocol;    // True if client uses binary protocol
    uint32_t query_id_counter;   // For generating query IDs
    NetConnection *net_conn;     // Network connection (for async mode)
    
    // Prepared statements
    SessionStatementStore *stmt_store;  // Per-session prepared statement store
} ClientConnection;

// Global async server instance
#if USE_ASYNC_IO
static AsyncServer *g_async_server = NULL;
#endif

// Connection pools for workers
#if ENABLE_CONNECTION_POOL
static ConnectionPool **g_worker_pools = NULL;
static int g_worker_pool_count = 0;
#endif

// ============================================================================
// PROTOCOL-AWARE SEND FUNCTIONS
// ============================================================================

// Send response using appropriate protocol (binary or text)
static int send_response(ClientConnection *conn, const char *data, size_t len) {
    if (conn->use_binary_protocol) {
        return net_send_result(conn->socket, conn->query_id_counter, 0, data, len);
    } else {
        // Legacy text protocol
        int sent = send(conn->socket, data, (int)len, 0);
        // Send EOF marker for legacy clients
        send(conn->socket, "<<EOF>>", 7, 0);
        return sent > 0 ? 0 : -1;
    }
}

// Send error using appropriate protocol
static int send_error_response(ClientConnection *conn, const char *error_msg) {
    if (conn->use_binary_protocol) {
        return net_send_error(conn->socket, conn->query_id_counter, error_msg);
    } else {
        char buffer[512];
        int len = snprintf(buffer, sizeof(buffer), "\033[31mError: %s\033[0m\n<<EOF>>", error_msg);
        return send(conn->socket, buffer, len, 0) > 0 ? 0 : -1;
    }
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

static void send_prompt(SOCKET sock, ClientConnection *conn) {
    char prompt[256];
    
    if (!conn->authenticated) {
        snprintf(prompt, sizeof(prompt), "\n\033[33m[not authenticated]\033[0m inventix> ");
    } else {
        snprintf(prompt, sizeof(prompt), "\n\033[32m[%s@%s]\033[0m inventix> ",
                 conn->session ? conn->session->username : "unknown",
                 conn->ctx.current_db);
    }
    
    send(sock, prompt, (int)strlen(prompt), 0);
}

static void send_banner(SOCKET sock, ClientConnection *conn) {
    char banner[2048];
    
    snprintf(banner, sizeof(banner),
        "\n"
        "\033[36m======================================================================\033[0m\n"
        "\033[1;35m  INVENTIXDB SERVER v%s\033[0m\n"
        "\033[36m======================================================================\033[0m\n"
        "  Connection: \033[33m%s:%d\033[0m\n"
        "  Mode: \033[33m%s\033[0m\n"
        "\033[36m======================================================================\033[0m\n"
        "\n"
        "\033[33m  Authentication required. Use: LOGIN <username> <password>\033[0m\n"
        "\033[90m  Default credentials: admin / Admin@123\033[0m\n"
        "\n",
        SERVER_VERSION,
        conn->client_ip, conn->client_port,
        dist_is_master() ? "Master" : "Worker"
    );
    
    send(sock, banner, (int)strlen(banner), 0);
}

// ============================================================================
// AUTHENTICATION HANDLER
// ============================================================================

static int handle_login(ClientConnection *conn, const char *username, const char *password) {
    char error_msg[256];
    
    Session *session = NULL;
    int result = security_login(username, password, conn->client_ip, 
                                conn->client_port, &session, error_msg);
    
    if (result == 0 && session) {
        conn->session = session;
        strncpy(conn->auth_token, session->token, SECURITY_TOKEN_LENGTH);
        conn->authenticated = true;
        strncpy(conn->ctx.current_user, session->username, sizeof(conn->ctx.current_user) - 1);
        
        char msg[512];
        snprintf(msg, sizeof(msg),
            "\n\033[32mLogin successful!\033[0m\n"
            "  User: %s | Role: %s | Database: %s\n",
            username,
            session->is_superuser ? "Superadmin" : "User",
            conn->ctx.current_db
        );
        send(conn->socket, msg, (int)strlen(msg), 0);
        
        LOG_INFO("Client %s:%d authenticated as '%s'", 
                 conn->client_ip, conn->client_port, username);
        
        return 0;
    } else {
        char msg[256];
        snprintf(msg, sizeof(msg), "\n\033[31mLogin failed: %s\033[0m\n", error_msg);
        send(conn->socket, msg, (int)strlen(msg), 0);
        return -1;
    }
}

static void handle_logout(ClientConnection *conn) {
    if (conn->authenticated && conn->session) {
        security_logout(conn->auth_token);
        conn->session = NULL;
        conn->authenticated = false;
        memset(conn->auth_token, 0, sizeof(conn->auth_token));
        strcpy(conn->ctx.current_user, "guest");
        
        send(conn->socket, "\n\033[33mLogged out.\033[0m\n", 24, 0);
    }
}

// ============================================================================
// COMMAND HANDLERS
// ============================================================================

static void handle_help(ClientConnection *conn) {
    const char *help =
        "\n\033[1;36m========================= INVENTIXDB HELP / MADAD =========================\033[0m\n"
        "\033[1;32m InventixDB supports BOTH English AND Hinglish/Urdlish - Mix them freely!\033[0m\n"
        "\n\033[1;33m[Authentication / Tasdiq]\033[0m\n"
        "  LOGIN <user> <pass>    - Authenticate (English)\n"
        "  LOGOUT                 - End session\n"
        "  WHOAMI                 - Show current user\n"
        "\n\033[1;33m[Database / Maloomat]\033[0m\n"
        "  CREATE DATABASE <name> | TABLE BANAO <naam> - Create database\n"
        "  USE <database>         | ISTEMAAL <naam>    - Switch database\n"
        "  SHOW DATABASES         | DEKHO DATABASES    - List databases\n"
        "\n\033[1;33m[Tables / Jadwal]\033[0m\n"
        "  CREATE TABLE <name> (cols) | TABLE BANAO <naam> (cols) - Create table\n"
        "  DROP TABLE <name>          | GIRAO TABLE <naam>        - Delete table\n"
        "  SHOW TABLES                | DEKHO TABLES              - List tables\n"
        "\n\033[1;33m[Data Operations / Data Kaam]\033[0m\n"
        "  INSERT INTO <t> VALUES (...) | DAALO MEIN <t> MAAN (...)   - Insert\n"
        "  SELECT * FROM <t>            | DHUNDO * SE <t>             - Query\n"
        "  SELECT ... WHERE ...         | CHUNO ... JAHAN ...         - Query\n"
        "  UPDATE <t> SET ... WHERE ... | BADLO <t> RAKHO_YEH ... JAHAN ... - Update\n"
        "  DELETE FROM <t> WHERE ...    | NIKALO SE <t> JAHAN ...     - Delete\n"
        "\n\033[1;33m[Joins / Jodna]\033[0m\n"
        "  ... JOIN ... ON ...          | ... MILAO ... ON ...        - Join tables\n"
        "  LEFT JOIN | BAAYA MILAO      RIGHT JOIN | DAAYA MILAO      - Left/Right\n"
        "  FULL OUTER | POORA BAHAR     NATURAL    | KUDRATI          - Full/Natural\n"
        "\n\033[1;33m[Aggregation / Ikatha]\033[0m\n"
        "  COUNT(*) | GINO(*)           SUM(col) | JODO(col)          - Count/Sum\n"
        "  AVG(col) | AUSAAT(col)       MAX(col) | SABSE_BADA(col)    - Avg/Max\n"
        "  MIN(col) | SABSE_CHOTA(col)  GROUP BY  | SAMOOH DWARA      - Min/Group\n"
        "\n\033[1;33m[Ordering & Limits / Kram]\033[0m\n"
        "  ORDER BY col ASC  | KRAM DWARA col CHADHTE   - Sort ascending\n"
        "  ORDER BY col DESC | KRAM DWARA col UTARTE   - Sort descending\n"
        "  LIMIT n OFFSET m  | SEEMA n PAAR m          - Pagination\n"
        "\n\033[1;33m[Transactions / Lenden]\033[0m\n"
        "  BEGIN    | SHURU      COMMIT   | PUKKA     - Start/Commit\n"
        "  ROLLBACK | WAPAS      SAVEPOINT | NISHAAN  - Rollback/Savepoint\n"
        "\n\033[1;33m[Prepared Statements / Tayyar Bayaan]\033[0m\n"
        "  PREPARE name AS query | TAYYAR naam JAISE query  - Prepare\n"
        "  EXECUTE name USING .. | CHALAO naam ISTEMAL ...  - Execute\n"
        "\n\033[1;33m[Backup & Restore / Suraksha]\033[0m\n"
        "  BACKUP DATABASE TO 'path'    | SURAKSHA DATABASE TO 'raasta'\n"
        "  RESTORE DATABASE FROM 'path' | WAPAS_LAO DATABASE SE 'raasta'\n"
        "\n\033[1;33m[ALTER TABLE / Table Badlo]\033[0m\n"
        "  ALTER TABLE t ADD col type   | BADLO_TABLE t JODO_COLUMN col type\n"
        "  ALTER TABLE t DROP COLUMN c  | BADLO_TABLE t GIRAO STAMBH c\n"
        "\n\033[1;33m[NoSQL / Document Store]\033[0m\n"
        "  COLLECTION BANAO <name>      | CREATE COLLECTION <name>  - Create\n"
        "  FIND <coll> WHERE ...        | KHOJO <sangrah> JAHAN ... - Find\n"
        "  UPSERT <coll> ...            | DAL_YA_BADLO <sangrah>    - Upsert\n"
        "\n\033[1;33m[Admin / Intezaam]\033[0m\n"
        "  CREATE USER <n> <p> [role]   - Create user (Admin only)\n"
        "  DROP USER <name>             - Delete user (Admin only)\n"
        "  SHOW USERS                   - List users\n"
        "\n\033[1;33m[Commands / Hukm]\033[0m\n"
        "  HELP | MADAD     STATUS | HALAT     EXIT | NIKLO | BAHAR\n"
        "\n\033[1;32m TIP: You can mix! Example: SELECT * SE users JAHAN id > 5\033[0m\n"
        "\033[1;36m===========================================================================\033[0m\n";
    
    send(conn->socket, help, (int)strlen(help), 0);
}

static void handle_whoami(ClientConnection *conn) {
    char buffer[512];
    
    if (!conn->authenticated) {
        snprintf(buffer, sizeof(buffer), "\n\033[33mNot authenticated.\033[0m\n");
    } else {
        snprintf(buffer, sizeof(buffer),
            "\n  Username: \033[1m%s\033[0m\n"
            "  Superuser: %s\n"
            "  Database: %s\n"
            "  Client: %s:%d\n",
            conn->session->username,
            conn->session->is_superuser ? "\033[32mYes\033[0m" : "No",
            conn->ctx.current_db,
            conn->client_ip, conn->client_port
        );
    }
    
    send(conn->socket, buffer, (int)strlen(buffer), 0);
}

static void handle_status(ClientConnection *conn) {
    char buffer[2048];
    
    // Get memory stats
    size_t session_mem = conn->mem ? arena_get_used(conn->mem->arena) : 0;
    size_t query_mem = conn->query_arena ? arena_get_used(conn->query_arena) : 0;
    
    snprintf(buffer, sizeof(buffer),
        "\n\033[1;36m============== Server Status ==============\033[0m\n"
        "  Version: %s | Mode: %s\n"
        "  Port: %d | Clients: %d/%d\n"
        "  Sessions: %d | Users: %d\n"
        "\n\033[1;33m[Memory]\033[0m\n"
        "  Session Memory:  %zu KB / %d KB quota\n"
        "  Query Memory:    %zu KB / %d KB quota\n"
        "  Queries Run:     %zu\n"
        "  Global Stats:    %zu allocs, %zu frees\n"
        "\033[1;36m============================================\033[0m\n",
        SERVER_VERSION,
        dist_is_master() ? "Master" : "Worker",
        g_port, g_client_count, MAX_CLIENTS,
        g_security ? g_security->session_count : 0,
        g_security ? g_security->user_count : 0,
        session_mem / 1024, SESSION_MEMORY_QUOTA / 1024,
        query_mem / 1024, QUERY_MEMORY_QUOTA / 1024,
        conn->queries_executed,
        g_mem_stats.malloc_count, g_mem_stats.free_count
    );
    
    send(conn->socket, buffer, (int)strlen(buffer), 0);
}

static void handle_show_users(ClientConnection *conn) {
    if (!conn->authenticated || !security_session_has_permission(conn->session, PERM_VIEW_STATS)) {
        send(conn->socket, "\n\033[31mPermission denied.\033[0m\n", 30, 0);
        return;
    }
    
    char buffer[4096];
    int offset = 0;
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
        "\n\033[1;36m========== Users ==========\033[0m\n"
        " %-15s %-15s %-10s\n"
        "----------------------------\n",
        "USERNAME", "ROLE", "STATUS");
    
    if (g_security) {
        for (int i = 0; i < g_security->user_count; i++) {
            User *u = &g_security->users[i];
            offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                " %-15s %-15s %s\n",
                u->username,
                u->role_count > 0 ? u->roles[0] : "-",
                u->status == USER_STATUS_ACTIVE ? "Active" : "Locked"
            );
        }
    }
    
    send(conn->socket, buffer, (int)strlen(buffer), 0);
}

// ============================================================================
// CLUSTER STATUS HANDLERS (Bilingual: English + Hinglish)
// ============================================================================

static void handle_cluster_status(ClientConnection *conn) {
    char buffer[4096];
    int offset = 0;
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
        "\n\033[1;36m============= CLUSTER STATUS / JHAAD HALAT =============\033[0m\n");
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
        "  Mode / Dhang:        \033[33m%s\033[0m\n"
        "  Server Version:      %s\n"
        "  Protocol Version:    %d.%d\n"
        "  Worker Count:        %d\n"
        "  Connected Clients:   %d / %d\n",
        dist_is_master() ? "MASTER (Mukhiya)" : "WORKER (Mazdoor)",
        SERVER_VERSION,
        NET_PROTOCOL_VERSION >> 8, NET_PROTOCOL_VERSION & 0xFF,
        dist_get_worker_count(),
        g_client_count, MAX_CLIENTS
    );
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
        "\n\033[1;33m[Replication / Nakal]\033[0m\n"
        "  Mode: Async (Asynchronous Replication)\n"
        "  Strategy: Hash Partitioning\n"
        "  Replication Factor: 2\n");
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
        "\n\033[1;33m[Connections / Jodav]\033[0m\n"
        "  Binary Protocol: Enabled (Chalu)\n"
        "  Connection Pooling: Enabled (Chalu)\n"
        "  Keep-Alive: 30 seconds\n");
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
        "\033[1;36m=========================================================\033[0m\n");
    
    send(conn->socket, buffer, (int)strlen(buffer), 0);
}

static void handle_cluster_nodes(ClientConnection *conn) {
    char buffer[4096];
    int offset = 0;
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
        "\n\033[1;36m============= CLUSTER NODES / JHAAD GRANTHI =============\033[0m\n"
        " %-5s %-20s %-10s %-12s\n"
        "------------------------------------------------------\n",
        "ID", "ADDRESS / PATA", "ROLE", "STATUS");
    
    // Add self (this node)
    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
        " %-5d %-20s %-10s \033[32m%-12s\033[0m\n",
        0, "localhost:self", 
        dist_is_master() ? "Master" : "Worker",
        "Online");
    
    // Add workers from distributed configuration
    int worker_count = dist_get_worker_count();
    for (int i = 0; i < worker_count; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
            " %-5d %-20s %-10s %-12s\n",
            i + 1, 
            "127.0.0.1:worker",
            "Worker",
            "Unknown");
    }
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
        "\n  Total Nodes / Kul Granthi: %d\n"
        "\033[1;36m=========================================================\033[0m\n",
        worker_count + 1);
    
    send(conn->socket, buffer, (int)strlen(buffer), 0);
}

static void handle_create_user(ClientConnection *conn, const char *args) {
    if (!conn->authenticated || !security_session_has_permission(conn->session, PERM_CREATE_USER)) {
        send(conn->socket, "\n\033[31mPermission denied.\033[0m\n", 30, 0);
        return;
    }
    
    char username[64], password[64], role[32] = "";
    sscanf(args, "%63s %63s %31s", username, password, role);
    
    if (strlen(role) == 0) strcpy(role, ROLE_GUEST);
    
    char error[128];
    int user_id = security_create_user(username, password, role, error);
    
    char msg[256];
    if (user_id >= 0) {
        snprintf(msg, sizeof(msg), "\n\033[32mUser '%s' created.\033[0m\n", username);
    } else {
        snprintf(msg, sizeof(msg), "\n\033[31mFailed: %s\033[0m\n", error);
    }
    send(conn->socket, msg, (int)strlen(msg), 0);
}

// ============================================================================
// QUERY EXECUTION
// ============================================================================

static void execute_client_query(ClientConnection *conn, const char *query) {
    if (!conn->authenticated) {
        send(conn->socket, "\n\033[31mAuthentication required.\033[0m\n", 36, 0);
        return;
    }
    
    security_touch_session(conn->auth_token);
    
    // Reset query arena for this query (RAII-style cleanup)
    if (conn->query_arena) {
        arena_reset(conn->query_arena);
    }
    conn->queries_executed++;
    
    // Check memory quota before query
    if (conn->mem && conn->mem->quota) {
        QuotaStatus status = quota_status(conn->mem->quota);
        if (status == QUOTA_EXCEEDED) {
            send(conn->socket, "\n\033[31mMemory quota exceeded. Try simpler query.\033[0m\n", 52, 0);
            return;
        } else if (status == QUOTA_CRITICAL) {
            char msg[128];
            snprintf(msg, sizeof(msg), 
                "\n\033[33mWarning: Memory usage at %.1f%% of quota.\033[0m\n",
                quota_usage_percent(conn->mem->quota));
            send(conn->socket, msg, (int)strlen(msg), 0);
        }
    }
    
    TokenList *tokens = tokenize(query);
    if (!tokens || tokens->count == 0) {
        if (tokens) free_token_list(tokens);
        return;
    }
    
    const char *cmd_type = tokens->tokens[0].value;
    
    // Handle prepared statement commands specially
    if (cmd_type) {
        if (strcasecmp(cmd_type, "PREPARE") == 0 || strcasecmp(cmd_type, "TAYYAR") == 0) {
            // PREPARE stmt_name AS query
            if (tokens->count >= 4) {
                const char *stmt_name = tokens->tokens[1].value;
                // Find AS keyword
                int as_pos = -1;
                for (int i = 2; i < tokens->count; i++) {
                    if (strcasecmp(tokens->tokens[i].value, "AS") == 0 ||
                        strcasecmp(tokens->tokens[i].value, "JAISE") == 0) {
                        as_pos = i;
                        break;
                    }
                }
                if (as_pos > 0 && as_pos + 1 < tokens->count) {
                    // Extract query template from original query
                    const char *template_start = strstr(query, tokens->tokens[as_pos + 1].value);
                    if (template_start) {
                        PreparedStatement *stmt = stmt_prepare(conn->stmt_store, stmt_name,
                                                               template_start, conn->ctx.current_user);
                        if (stmt) {
                            char msg[256];
                            snprintf(msg, sizeof(msg), 
                                "\n\033[32mPrepared statement '%s' created (%d parameters)\033[0m\n",
                                stmt_name, stmt->param_count);
                            send(conn->socket, msg, (int)strlen(msg), 0);
                        } else {
                            send(conn->socket, "\n\033[31mFailed to prepare statement\033[0m\n", 40, 0);
                        }
                    }
                } else {
                    send(conn->socket, "\n\033[31mSyntax: PREPARE <name> AS <query>\033[0m\n", 47, 0);
                }
            }
            free_token_list(tokens);
            return;
        }
        
        if (strcasecmp(cmd_type, "EXECUTE") == 0 || strcasecmp(cmd_type, "CHALAO") == 0) {
            // EXECUTE stmt_name USING (params)
            if (tokens->count >= 2) {
                const char *stmt_name = tokens->tokens[1].value;
                Parameter params[MAX_PARAMETERS];
                int param_count = 0;
                
                // Find USING clause
                const char *using_ptr = strcasestr(query, "USING");
                if (!using_ptr) using_ptr = strcasestr(query, "ISTEMAL");
                if (using_ptr) {
                    using_ptr = strchr(using_ptr, '(');
                    if (using_ptr) {
                        param_count = parse_using_clause(using_ptr, params, MAX_PARAMETERS);
                    }
                }
                
                char *result = NULL;
                size_t result_len = 0;
                int rc = stmt_execute(conn->stmt_store, stmt_name, params, param_count,
                                      NULL, &result, &result_len);
                if (rc == 0 && result) {
                    // Execute the substituted query
                    char msg[128];
                    snprintf(msg, sizeof(msg), "\n\033[90m[Executing: %s]\033[0m\n", result);
                    send(conn->socket, msg, (int)strlen(msg), 0);
                    
                    // Recursively execute the resolved query
                    execute_client_query(conn, result);
                    free(result);
                } else {
                    char msg[256];
                    snprintf(msg, sizeof(msg), 
                        "\n\033[31mFailed to execute statement '%s'\033[0m\n", stmt_name);
                    send(conn->socket, msg, (int)strlen(msg), 0);
                }
                
                // Clean up parameters
                for (int i = 0; i < param_count; i++) {
                    if (params[i].type == PARAM_TYPE_STRING && params[i].value.string_val.data) {
                        free(params[i].value.string_val.data);
                    }
                }
            }
            free_token_list(tokens);
            return;
        }
        
        if (strcasecmp(cmd_type, "DEALLOCATE") == 0 || strcasecmp(cmd_type, "HATAO_TAYYAR") == 0) {
            if (tokens->count >= 2) {
                const char *stmt_name = tokens->tokens[1].value;
                if (strcasecmp(stmt_name, "ALL") == 0) {
                    int count = stmt_deallocate_all(conn->stmt_store);
                    char msg[128];
                    snprintf(msg, sizeof(msg), 
                        "\n\033[32mDeallocated %d prepared statements\033[0m\n", count);
                    send(conn->socket, msg, (int)strlen(msg), 0);
                } else {
                    if (stmt_deallocate(conn->stmt_store, stmt_name) == 0) {
                        char msg[128];
                        snprintf(msg, sizeof(msg), 
                            "\n\033[32mDeallocated statement '%s'\033[0m\n", stmt_name);
                        send(conn->socket, msg, (int)strlen(msg), 0);
                    } else {
                        char msg[128];
                        snprintf(msg, sizeof(msg), 
                            "\n\033[31mStatement '%s' not found\033[0m\n", stmt_name);
                        send(conn->socket, msg, (int)strlen(msg), 0);
                    }
                }
            }
            free_token_list(tokens);
            return;
        }
        
        // Handle EXPLAIN command
        if (strcasecmp(cmd_type, "EXPLAIN") == 0 || strcasecmp(cmd_type, "SAMJHAO") == 0) {
            bool analyze = false;
            int query_start = 1;
            
            // Check for ANALYZE keyword
            if (tokens->count >= 2) {
                if (strcasecmp(tokens->tokens[1].value, "ANALYZE") == 0) {
                    analyze = true;
                    query_start = 2;
                }
            }
            
            // Parse the query to explain
            if (tokens->count > query_start) {
                // Find the start of the actual query
                const char *explain_query = query;
                for (int i = 0; i < query_start; i++) {
                    explain_query = strstr(explain_query, tokens->tokens[i].value);
                    if (explain_query) {
                        explain_query += strlen(tokens->tokens[i].value);
                        while (*explain_query && isspace(*explain_query)) explain_query++;
                    }
                }
                
                if (explain_query && *explain_query) {
                    // Parse and optimize the query
                    TokenList *explain_tokens = tokenize(explain_query);
                    if (explain_tokens) {
                        ASTNode *explain_ast = parse(explain_tokens);
                        if (explain_ast) {
                            OptQueryPlan *plan = optimizer_optimize(explain_ast, NULL);
                            if (plan) {
                                char *explain_output = optimizer_plan_explain(plan, analyze);
                                if (explain_output) {
                                    send(conn->socket, "\n\033[36m", 6, 0);
                                    send(conn->socket, explain_output, (int)strlen(explain_output), 0);
                                    send(conn->socket, "\033[0m\n", 5, 0);
                                    free(explain_output);
                                }
                                optimizer_plan_free(plan);
                            }
                            free_ast(explain_ast);
                        } else {
                            send(conn->socket, "\n\033[31mCannot parse query for EXPLAIN\033[0m\n", 42, 0);
                        }
                        free_token_list(explain_tokens);
                    }
                } else {
                    send(conn->socket, "\n\033[31mSyntax: EXPLAIN [ANALYZE] <query>\033[0m\n", 46, 0);
                }
            } else {
                send(conn->socket, "\n\033[31mSyntax: EXPLAIN [ANALYZE] <query>\033[0m\n", 46, 0);
            }
            free_token_list(tokens);
            return;
        }
    }
    
    if (cmd_type && !security_authorize_command(conn->session, cmd_type)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "\n\033[31mPermission denied for %s.\033[0m\n", cmd_type);
        send(conn->socket, msg, (int)strlen(msg), 0);
        free_token_list(tokens);
        return;
    }
    
    ASTNode *ast = parse(tokens);
    if (ast) {
        if (dist_is_master()) {
            char *resp = dist_route_query(ast, query);
            if (resp) {
                send(conn->socket, "\n", 1, 0);
                send(conn->socket, resp, (int)strlen(resp), 0);
                free(resp);
            }
        } else {
            FILE *fp = tmpfile();
            if (fp) {
                execute_query(ast, global_store, &conn->ctx, fp);
                fseek(fp, 0, SEEK_SET);
                
                char out_buf[BUFFER_SIZE];
                send(conn->socket, "\n", 1, 0);
                while (fgets(out_buf, BUFFER_SIZE, fp)) {
                    send(conn->socket, out_buf, (int)strlen(out_buf), 0);
                }
                fclose(fp);
            }
        }
    } else {
        send(conn->socket, "\n\033[31mParse error.\033[0m\n", 24, 0);
    }
    
    free_token_list(tokens);
}

// ============================================================================
// CLIENT HANDLER
// ============================================================================

static void *client_handler(void *arg) {
    ClientConnection *conn = (ClientConnection *)arg;
    char buffer[BUFFER_SIZE];
    int read_size;
    
    pthread_mutex_lock(&g_client_lock);
    g_client_count++;
    pthread_mutex_unlock(&g_client_lock);
    
    // Initialize session memory management
    conn->mem = session_memory_create(SESSION_MEMORY_QUOTA);
    conn->query_arena = arena_create(QUERY_MEMORY_QUOTA);
    if (!conn->mem || !conn->query_arena) {
        LOG_ERROR("Failed to allocate session memory for %s:%d", 
                  conn->client_ip, conn->client_port);
        closesocket(conn->socket);
        if (conn->mem) session_memory_destroy(conn->mem);
        if (conn->query_arena) arena_destroy(conn->query_arena);
        free(conn);
        
        pthread_mutex_lock(&g_client_lock);
        g_client_count--;
        pthread_mutex_unlock(&g_client_lock);
        return NULL;
    }
    conn->queries_executed = 0;
    conn->memory_used = 0;
    
    LOG_DEBUG("Session memory initialized for %s:%d (quota: %zu KB)",
              conn->client_ip, conn->client_port, SESSION_MEMORY_QUOTA / 1024);
    
    send_banner(conn->socket, conn);
    send_prompt(conn->socket, conn);
    
    while (g_running) {
        memset(buffer, 0, BUFFER_SIZE);
        read_size = recv(conn->socket, buffer, BUFFER_SIZE - 1, 0);
        
        if (read_size <= 0) break;
        
        buffer[strcspn(buffer, "\r\n")] = 0;
        if (strlen(buffer) == 0) {
            send_prompt(conn->socket, conn);
            continue;
        }
        
        char cmd[32] = {0}, arg1[128] = {0}, arg2[128] = {0};
        sscanf(buffer, "%31s %127s %127s", cmd, arg1, arg2);
        
        for (int i = 0; cmd[i]; i++) cmd[i] = toupper((unsigned char)cmd[i]);
        
        // Exit commands (English and Hinglish)
        if (strcmp(cmd, "EXIT") == 0 || strcmp(cmd, "QUIT") == 0 ||
            strcmp(cmd, "NIKLO") == 0 || strcmp(cmd, "BAHAR") == 0 ||
            strcmp(cmd, "KHATAM_KARO") == 0) {
            send(conn->socket, "\nAlvida! Goodbye!\n", 18, 0);
            break;
        }
        else if (strcmp(cmd, "LOGIN") == 0) {
            handle_login(conn, arg1, arg2);
        }
        else if (strcmp(cmd, "LOGOUT") == 0) {
            handle_logout(conn);
        }
        // Help commands (English and Hinglish)
        else if (strcmp(cmd, "HELP") == 0 || strcmp(cmd, "?") == 0 ||
                 strcmp(cmd, "MADAD") == 0) {
            handle_help(conn);
        }
        else if (strcmp(cmd, "WHOAMI") == 0) {
            handle_whoami(conn);
        }
        // Status commands (English and Hinglish)
        else if (strcmp(cmd, "STATUS") == 0 || strcmp(cmd, "HALAT") == 0 ||
                 strcmp(cmd, "STHITI") == 0) {
            handle_status(conn);
        }
        // Cluster status commands
        else if (strcmp(cmd, "CLUSTER") == 0 || strcmp(cmd, "JHAAD") == 0) {
            handle_cluster_status(conn);
        }
        else if (strcmp(cmd, "NODES") == 0 || strcmp(cmd, "GRANTHI") == 0) {
            handle_cluster_nodes(conn);
        }
        else if (strcmp(cmd, "SHOW") == 0 && strcasecmp(arg1, "USERS") == 0) {
            handle_show_users(conn);
        }
        else if (strcmp(cmd, "CREATE") == 0 && strcasecmp(arg1, "USER") == 0) {
            char rest[256];
            if (sscanf(buffer + 12, "%255[^\n]", rest) == 1) {
                handle_create_user(conn, rest);
            }
        }
        else {
            execute_client_query(conn, buffer);
        }
        
        send_prompt(conn->socket, conn);
    }
    
    if (conn->authenticated) {
        security_logout(conn->auth_token);
    }
    
    closesocket(conn->socket);
    
    // Cleanup session memory
    size_t final_memory = conn->query_arena ? conn->query_arena->total_used : 0;
    size_t total_queries = conn->queries_executed;
    
    if (conn->query_arena) {
        arena_destroy(conn->query_arena);
        conn->query_arena = NULL;
    }
    if (conn->mem) {
        session_memory_destroy(conn->mem);
        conn->mem = NULL;
    }
    
    // Cleanup prepared statements for this session
    if (conn->stmt_store) {
        stmt_store_destroy(conn->stmt_store);
        conn->stmt_store = NULL;
    }
    
    pthread_mutex_lock(&g_client_lock);
    g_client_count--;
    pthread_mutex_unlock(&g_client_lock);
    
    LOG_INFO("Client %s:%d disconnected (queries: %zu, peak mem: %zu bytes)", 
             conn->client_ip, conn->client_port, total_queries, final_memory);
    
    free(conn);
    return NULL;
}

// ============================================================================
// SERVER STARTUP
// ============================================================================

static void print_server_banner(void) {
    printf("\n");
    printf("\033[1;36m======================================================================\033[0m\n");
    printf("\033[1;35m  INVENTIXDB SERVER v%s\033[0m\n", SERVER_VERSION);
    printf("\033[32m  Secure (PBKDF2) | RBAC | Distributed\033[0m\n");
    printf("\033[1;36m======================================================================\033[0m\n");
}

static void print_server_info(void) {
    printf("\n");
    printf("  Mode: \033[33m%s\033[0m | Port: \033[33m%d\033[0m | Workers: \033[33m%d\033[0m\n",
           dist_is_master() ? "MASTER" : "WORKER", g_port, dist_get_worker_count());
    printf("  Auth: \033[32mPBKDF2-SHA256\033[0m | Default: \033[33madmin / Admin@123\033[0m\n");
    printf("\n\033[32mServer ready on port %d\033[0m\n\n", g_port);
}

int main(int argc, char *argv[]) {
    WSADATA wsa;
    SOCKET server_fd, new_socket;
    struct sockaddr_in server, client;
    int c;
    
    // Initialize memory tracking
    mem_tracking_enable(true);
    
    config_init_defaults(&g_config);
    config_load(&g_config, "inventix.conf");
    logger_init();
    
    LOG_INFO("Memory management initialized (tracking enabled)");
    
    if (security_init() != 0) {
        fprintf(stderr, "Failed to initialize security\n");
        return 1;
    }
    security_load("security.dat");
    
    // Initialize prepared statement and index systems
    prepared_init();
    index_manager_init();
    optimizer_init();
    join_init();
    mvcc_init();
    query_cache_init(64 * 1024 * 1024); // 64MB cache
    LOG_INFO("Prepared statements, index manager, optimizer, join, MVCC and query cache initialized");
    
    g_port = CFG_NETWORK.server_port;
    
    dist_init();
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--master") == 0) dist_set_master(1);
        else if (strcmp(argv[i], "--worker") == 0) dist_set_master(0);
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) g_port = atoi(argv[++i]);
    }
    
    pthread_mutex_init(&g_client_lock, NULL);
    print_server_banner();
    
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "Winsock init failed\n");
        return 1;
    }
    
    global_store = kv_create();
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(g_port);
    
    if (bind(server_fd, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR) {
        fprintf(stderr, "Bind failed\n");
        return 1;
    }
    
    listen(server_fd, CFG_NETWORK.backlog_size);
    print_server_info();
    
    LOG_INFO("InventixDB Server v%s started on port %d", SERVER_VERSION, g_port);
    
    c = sizeof(struct sockaddr_in);
    
    while (g_running && (new_socket = accept(server_fd, (struct sockaddr *)&client, &c)) != INVALID_SOCKET) {
        ClientConnection *conn = calloc(1, sizeof(ClientConnection));
        if (!conn) { closesocket(new_socket); continue; }
        
        conn->socket = new_socket;
        inet_ntop(AF_INET, &client.sin_addr, conn->client_ip, sizeof(conn->client_ip));
        conn->client_port = ntohs(client.sin_port);
        conn->authenticated = false;
        strcpy(conn->ctx.current_db, "public");
        strcpy(conn->ctx.current_user, "guest");
        
        // Initialize prepared statement store for this connection
        conn->stmt_store = stmt_store_create();
        
        LOG_INFO("New connection from %s:%d", conn->client_ip, conn->client_port);
        
        pthread_t thread;
        if (pthread_create(&thread, NULL, client_handler, conn) != 0) {
            if (conn->stmt_store) stmt_store_destroy(conn->stmt_store);
            free(conn);
            closesocket(new_socket);
            continue;
        }
        pthread_detach(thread);
    }
    
    closesocket(server_fd);
    
    // Print memory statistics before shutdown
    LOG_INFO("Server shutting down...");
    mem_stats_print();
    
    // Shutdown subsystems
    prepared_shutdown();
    index_manager_shutdown();
    optimizer_shutdown();
    join_shutdown();
    mvcc_shutdown();
    security_shutdown();
    WSACleanup();
    pthread_mutex_destroy(&g_client_lock);
    
    LOG_INFO("InventixDB Server shutdown complete");
    
    return 0;
}
