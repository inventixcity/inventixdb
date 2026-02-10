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

#define SERVER_VERSION "1.3.0"
#define USE_ASYNC_IO 1              // Enable IOCP async I/O
#define ENABLE_CONNECTION_POOL 1    // Enable connection pooling for workers
#define BUFFER_SIZE 8192
#define MAX_CLIENTS 256
#define SESSION_MEMORY_QUOTA (8 * 1024 * 1024)  // 8 MB per session
#define QUERY_MEMORY_QUOTA   (2 * 1024 * 1024)  // 2 MB per query

// Security: Rate limiting & brute-force protection
#define MAX_LOGIN_ATTEMPTS      5       // Max failed logins before lockout
#define LOGIN_LOCKOUT_SECONDS   300     // 5 minute lockout
#define MAX_QUERIES_PER_MINUTE  1000    // Rate limit per client
#define CONNECTION_TIMEOUT_SEC  300     // Idle connection timeout (5 min)
#define MAX_IP_BLACKLIST        128     // Max blacklisted IPs

static int g_port = 8888;
static int g_running = 1;
static int g_client_count = 0;
static pthread_mutex_t g_client_lock;

KVStore *global_store;

// Global memory pool for client connections
static MemPool *g_conn_pool = NULL;

// ============================================================================
// IP RATE LIMITING & BRUTE-FORCE PROTECTION
// ============================================================================

typedef struct {
    char ip[64];
    int failed_logins;
    time_t lockout_until;
    int queries_this_minute;
    time_t minute_start;
    time_t last_activity;
} IPTracker;

static IPTracker g_ip_tracker[MAX_CLIENTS];
static int g_ip_tracker_count = 0;
static pthread_mutex_t g_ip_lock = PTHREAD_MUTEX_INITIALIZER;

// IP Blacklist 
static char g_ip_blacklist[MAX_IP_BLACKLIST][64];
static int g_ip_blacklist_count = 0;

static IPTracker* find_or_create_ip_tracker(const char *ip) {
    pthread_mutex_lock(&g_ip_lock);
    for (int i = 0; i < g_ip_tracker_count; i++) {
        if (strcmp(g_ip_tracker[i].ip, ip) == 0) {
            pthread_mutex_unlock(&g_ip_lock);
            return &g_ip_tracker[i];
        }
    }
    if (g_ip_tracker_count < MAX_CLIENTS) {
        IPTracker *t = &g_ip_tracker[g_ip_tracker_count++];
        memset(t, 0, sizeof(IPTracker));
        strncpy(t->ip, ip, sizeof(t->ip) - 1);
        t->minute_start = time(NULL);
        t->last_activity = time(NULL);
        pthread_mutex_unlock(&g_ip_lock);
        return t;
    }
    pthread_mutex_unlock(&g_ip_lock);
    return NULL;
}

static bool is_ip_blacklisted(const char *ip) {
    for (int i = 0; i < g_ip_blacklist_count; i++) {
        if (strcmp(g_ip_blacklist[i], ip) == 0) return true;
    }
    return false;
}

static bool is_ip_locked_out(IPTracker *tracker) {
    if (!tracker) return false;
    if (tracker->lockout_until > 0 && time(NULL) < tracker->lockout_until) return true;
    if (tracker->lockout_until > 0 && time(NULL) >= tracker->lockout_until) {
        tracker->failed_logins = 0;
        tracker->lockout_until = 0;
    }
    return false;
}

static bool check_rate_limit(IPTracker *tracker) {
    if (!tracker) return false;
    time_t now = time(NULL);
    if (now - tracker->minute_start >= 60) {
        tracker->queries_this_minute = 0;
        tracker->minute_start = now;
    }
    tracker->queries_this_minute++;
    tracker->last_activity = now;
    return tracker->queries_this_minute <= MAX_QUERIES_PER_MINUTE;
}

static void record_failed_login(IPTracker *tracker) {
    if (!tracker) return;
    tracker->failed_logins++;
    if (tracker->failed_logins >= MAX_LOGIN_ATTEMPTS) {
        tracker->lockout_until = time(NULL) + LOGIN_LOCKOUT_SECONDS;
        LOG_SECURITY("IP %s locked out for %d seconds after %d failed login attempts",
                     tracker->ip, LOGIN_LOCKOUT_SECONDS, tracker->failed_logins);
    }
}

static void record_successful_login(IPTracker *tracker) {
    if (!tracker) return;
    tracker->failed_logins = 0;
    tracker->lockout_until = 0;
}

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
    
    // Security tracking
    IPTracker *ip_tracker;              // Rate limiting & brute-force tracking
    time_t connect_time;                // Connection start time
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
    
    // Check IP lockout (brute-force protection)
    if (conn->ip_tracker && is_ip_locked_out(conn->ip_tracker)) {
        int remaining = (int)(conn->ip_tracker->lockout_until - time(NULL));
        char msg[256];
        snprintf(msg, sizeof(msg),
            "\n\033[31mAccount locked. Too many failed attempts. Try again in %d seconds.\033[0m\n",
            remaining);
        send(conn->socket, msg, (int)strlen(msg), 0);
        LOG_SECURITY("Blocked login attempt from locked IP %s:%d (user: %s)",
                     conn->client_ip, conn->client_port, username);
        return -1;
    }
    
    Session *session = NULL;
    int result = security_login(username, password, conn->client_ip, 
                                conn->client_port, &session, error_msg);
    
    if (result == 0 && session) {
        conn->session = session;
        strncpy(conn->auth_token, session->token, SECURITY_TOKEN_LENGTH);
        conn->authenticated = true;
        strncpy(conn->ctx.current_user, session->username, sizeof(conn->ctx.current_user) - 1);
        
        // Reset failed login counter on success
        if (conn->ip_tracker) record_successful_login(conn->ip_tracker);
        
        char msg[512];
        snprintf(msg, sizeof(msg),
            "\n\033[32mLogin successful!\033[0m\n"
            "  User: %s | Role: %s | Database: %s\n"
            "  Session Token: %.8s...\n",
            username,
            session->is_superuser ? "Superadmin" : "User",
            conn->ctx.current_db,
            conn->auth_token
        );
        send(conn->socket, msg, (int)strlen(msg), 0);
        
        LOG_SECURITY("Client %s:%d authenticated as '%s' (role: %s)", 
                 conn->client_ip, conn->client_port, username,
                 session->is_superuser ? "superadmin" : "user");
        
        return 0;
    } else {
        // Record failed login for brute-force protection
        if (conn->ip_tracker) record_failed_login(conn->ip_tracker);
        
        char msg[256];
        snprintf(msg, sizeof(msg), "\n\033[31mLogin failed: %s\033[0m\n", error_msg);
        
        if (conn->ip_tracker && conn->ip_tracker->failed_logins > 0) {
            char warning[128];
            snprintf(warning, sizeof(warning),
                "\033[33m  Warning: %d/%d failed attempts.\033[0m\n",
                conn->ip_tracker->failed_logins, MAX_LOGIN_ATTEMPTS);
            strncat(msg, warning, sizeof(msg) - strlen(msg) - 1);
        }
        
        send(conn->socket, msg, (int)strlen(msg), 0);
        
        LOG_SECURITY("Failed login from %s:%d (user: %s, reason: %s, attempts: %d)",
                     conn->client_ip, conn->client_port, username, error_msg,
                     conn->ip_tracker ? conn->ip_tracker->failed_logins : 0);
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
    char buffer[4096];
    
    // Get memory stats
    size_t session_mem = conn->mem ? arena_get_used(conn->mem->arena) : 0;
    size_t query_mem = conn->query_arena ? arena_get_used(conn->query_arena) : 0;
    
    // Calculate uptime
    time_t conn_uptime = time(NULL) - conn->connect_time;
    
    snprintf(buffer, sizeof(buffer),
        "\n\033[1;36m================ Server Status / Halat ================\033[0m\n"
        "\n\033[1;33m[Server / Server]\033[0m\n"
        "  Version: %s | Mode: %s\n"
        "  Port: %d | Clients: %d/%d\n"
        "  Sessions: %d | Users: %d\n"
        "\n\033[1;33m[Security / Suraksha]\033[0m\n"
        "  Auth: PBKDF2-SHA256 | RBAC: Active\n"
        "  Brute-Force Protection: %d max attempts, %ds lockout\n"
        "  Rate Limit: %d queries/min per client\n"
        "  Idle Timeout: %d seconds\n"
        "  Blacklisted IPs: %d\n"
        "\n\033[1;33m[Memory / Yaaddasht]\033[0m\n"
        "  Session Memory:  %zu KB / %d KB quota\n"
        "  Query Memory:    %zu KB / %d KB quota\n"
        "  Queries Run:     %zu\n"
        "  Global Stats:    %zu allocs, %zu frees\n"
        "\n\033[1;33m[Connection / Judav]\033[0m\n"
        "  Your IP: %s:%d\n"
        "  Connected: %lld seconds ago\n"
        "  Authenticated: %s\n"
        "\033[1;36m========================================================\033[0m\n",
        SERVER_VERSION,
        dist_is_master() ? "Master" : "Worker",
        g_port, g_client_count, MAX_CLIENTS,
        g_security ? g_security->session_count : 0,
        g_security ? g_security->user_count : 0,
        MAX_LOGIN_ATTEMPTS, LOGIN_LOCKOUT_SECONDS,
        MAX_QUERIES_PER_MINUTE,
        CONNECTION_TIMEOUT_SEC,
        g_ip_blacklist_count,
        session_mem / 1024, SESSION_MEMORY_QUOTA / 1024,
        query_mem / 1024, QUERY_MEMORY_QUOTA / 1024,
        conn->queries_executed,
        g_mem_stats.malloc_count, g_mem_stats.free_count,
        conn->client_ip, conn->client_port,
        (long long)conn_uptime,
        conn->authenticated ? "Yes" : "No"
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
    
    // Parse: <username> PASSWORD '<password>' [ROLE <role>][;]
    char username[64] = "", password[64] = "", role[32] = "";
    
    // Extract username (first word)
    const char *p = args;
    while (*p && isspace(*p)) p++;
    int i = 0;
    while (*p && !isspace(*p) && i < 63) username[i++] = *p++;
    username[i] = '\0';
    
    // Look for PASSWORD keyword
    const char *pw_start = strcasestr(p, "PASSWORD");
    if (pw_start) {
        pw_start += 8; // skip "PASSWORD"
        while (*pw_start && isspace(*pw_start)) pw_start++;
        
        // Handle quoted password: 'xxx' or "xxx"
        if (*pw_start == '\'' || *pw_start == '"') {
            char quote = *pw_start++;
            i = 0;
            while (*pw_start && *pw_start != quote && i < 63) {
                password[i++] = *pw_start++;
            }
            password[i] = '\0';
        } else {
            // Unquoted — read until space
            i = 0;
            while (*pw_start && !isspace(*pw_start) && *pw_start != ';' && i < 63) {
                password[i++] = *pw_start++;
            }
            password[i] = '\0';
        }
    } else {
        // Fallback: second word is password (old format)
        while (*p && isspace(*p)) p++;
        i = 0;
        while (*p && !isspace(*p) && i < 63) password[i++] = *p++;
        password[i] = '\0';
    }
    
    // Look for ROLE keyword
    const char *role_start = strcasestr(args, "ROLE");
    if (role_start && role_start != args) { // make sure ROLE is not part of username
        role_start += 4; // skip "ROLE"
        while (*role_start && isspace(*role_start)) role_start++;
        i = 0;
        while (*role_start && !isspace(*role_start) && *role_start != ';' && i < 31) {
            role[i++] = *role_start++;
        }
        role[i] = '\0';
    }
    
    if (strlen(role) == 0) strcpy(role, ROLE_GUEST);
    if (strlen(username) == 0 || strlen(password) == 0) {
        send(conn->socket, "\n\033[31mSyntax: CREATE USER <name> PASSWORD '<pass>' [ROLE <role>]\033[0m\n", 70, 0);
        return;
    }
    
    char error[128];
    int user_id = security_create_user(username, password, role, error);
    
    char msg[256];
    if (user_id >= 0) {
        snprintf(msg, sizeof(msg), "\n\033[32mUser '%s' created (role: %s).\033[0m\n", username, role);
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
        LOG_DEBUG("Executing query type=%d for %s:%d", ast->type,
                 conn->client_ip, conn->client_port);
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
                
                // Get the size of the response
                fseek(fp, 0, SEEK_END);
                long resp_size = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                
                // Buffer entire response and send in one call
                if (resp_size > 0) {
                    char *resp_buf = malloc(resp_size + 2);  // +2 for \n prefix
                    if (resp_buf) {
                        resp_buf[0] = '\n';
                        size_t nread = fread(resp_buf + 1, 1, resp_size, fp);
                        resp_buf[1 + nread] = '\0';
                        send(conn->socket, resp_buf, (int)(1 + nread), 0);
                        free(resp_buf);
                    } else {
                        // Fallback: send line by line
                        char out_buf[BUFFER_SIZE];
                        send(conn->socket, "\n", 1, 0);
                        while (fgets(out_buf, BUFFER_SIZE, fp)) {
                            send(conn->socket, out_buf, (int)strlen(out_buf), 0);
                        }
                    }
                }
                fclose(fp);
            } else {
                send(conn->socket, "\n\033[31mServer error: tmpfile() failed.\033[0m\n", 42, 0);
            }
        }
        free_ast(ast);
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
    // Combine prompt + EOF into one send for instant delivery
    {
        char init_prompt[256];
        snprintf(init_prompt, sizeof(init_prompt),
            "\n\033[33m[not authenticated]\033[0m inventix> <<EOF>>");
        send(conn->socket, init_prompt, (int)strlen(init_prompt), 0);
    }
    
    while (g_running) {
        memset(buffer, 0, BUFFER_SIZE);
        read_size = recv(conn->socket, buffer, BUFFER_SIZE - 1, 0);
        
        if (read_size <= 0) break;
        
        buffer[strcspn(buffer, "\r\n")] = 0;
        if (strlen(buffer) == 0) {
            // Combined prompt + EOF in single send
            char prompt_buf[256];
            if (!conn->authenticated) {
                snprintf(prompt_buf, sizeof(prompt_buf), 
                    "\n\033[33m[not authenticated]\033[0m inventix> <<EOF>>");
            } else {
                snprintf(prompt_buf, sizeof(prompt_buf),
                    "\n\033[32m[%s@%s]\033[0m inventix> <<EOF>>",
                    conn->session ? conn->session->username : "unknown",
                    conn->ctx.current_db);
            }
            send(conn->socket, prompt_buf, (int)strlen(prompt_buf), 0);
            continue;
        }
        
        char cmd[32] = {0}, arg1[128] = {0}, arg2[128] = {0};
        sscanf(buffer, "%31s %127s %127s", cmd, arg1, arg2);
        
        for (int i = 0; cmd[i]; i++) cmd[i] = toupper((unsigned char)cmd[i]);
        
        // Security: Rate limiting check
        if (conn->ip_tracker && !check_rate_limit(conn->ip_tracker)) {
            send(conn->socket, 
                "\n\033[31mRate limit exceeded. Slow down your queries.\033[0m\n", 52, 0);
            LOG_SECURITY("Rate limit exceeded for %s:%d (%d queries/min)",
                         conn->client_ip, conn->client_port,
                         conn->ip_tracker->queries_this_minute);
            send_prompt(conn->socket, conn);
            send(conn->socket, "<<EOF>>", 7, 0);
            continue;
        }
        
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
        else if ((strcmp(cmd, "SHOW") == 0 || strcmp(cmd, "DEKHO") == 0) && 
                 (strcasecmp(arg1, "DATABASES") == 0 || strcasecmp(arg1, "DATABASE") == 0)) {
            // SHOW DATABASES / DEKHO DATABASES — route through SQL engine
            if (conn->authenticated) {
                // Ensure query ends with semicolon for parser
                char db_query[256];
                snprintf(db_query, sizeof(db_query), "%s;", buffer);
                execute_client_query(conn, db_query);
            } else {
                send(conn->socket, "\n\033[31mAuthentication required.\033[0m\n", 38, 0);
            }
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
        
        // Send EOF marker so legacy clients know the response is complete
        if (!conn->use_binary_protocol) {
            // Combine prompt + EOF into a single send to prevent TCP fragmentation
            char prompt_buf[256];
            if (!conn->authenticated) {
                snprintf(prompt_buf, sizeof(prompt_buf), 
                    "\n\033[33m[not authenticated]\033[0m inventix> <<EOF>>");
            } else {
                snprintf(prompt_buf, sizeof(prompt_buf),
                    "\n\033[32m[%s@%s]\033[0m inventix> <<EOF>>",
                    conn->session ? conn->session->username : "unknown",
                    conn->ctx.current_db);
            }
            send(conn->socket, prompt_buf, (int)strlen(prompt_buf), 0);
        } else {
            send_prompt(conn->socket, conn);
        }
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
    
    // Only create connection pools to workers if this node is master
    if (dist_is_master()) {
        dist_create_pools();
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
        // Disable Nagle's algorithm for instant response delivery
        int nodelay = 1;
        setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
        
        ClientConnection *conn = calloc(1, sizeof(ClientConnection));
        if (!conn) { closesocket(new_socket); continue; }
        
        conn->socket = new_socket;
        inet_ntop(AF_INET, &client.sin_addr, conn->client_ip, sizeof(conn->client_ip));
        conn->client_port = ntohs(client.sin_port);
        conn->authenticated = false;
        strcpy(conn->ctx.current_db, "public");
        strcpy(conn->ctx.current_user, "guest");
        conn->connect_time = time(NULL);
        
        // Security: Check IP blacklist
        if (is_ip_blacklisted(conn->client_ip)) {
            LOG_SECURITY("Rejected connection from blacklisted IP %s:%d",
                         conn->client_ip, conn->client_port);
            closesocket(new_socket);
            free(conn);
            continue;
        }
        
        // Security: Initialize IP tracker for rate limiting
        conn->ip_tracker = find_or_create_ip_tracker(conn->client_ip);
        
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
