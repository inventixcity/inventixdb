/**
 * InventixDB Standalone Client
 * 
 * A command-line client that can connect to remote InventixDB servers.
 * Supports both single-server and cluster mode connections.
 * 
 * Features:
 * - Binary protocol support (length-prefixed messages)
 * - Legacy text protocol for backward compatibility
 * - Auto-detection of server protocol version
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")
#define close_socket closesocket
#ifndef TCP_NODELAY
#define TCP_NODELAY 1
#endif
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#define close_socket close
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

#include "network.h"

#define CLIENT_VERSION "1.2.0"
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 9876
#define BUFFER_SIZE 65536
#define MAX_HISTORY 100
#define EOF_MARKER "<<EOF>>"

// Protocol mode
typedef enum {
    PROTOCOL_LEGACY = 0,    // Text with <<EOF>> markers
    PROTOCOL_BINARY = 1     // Length-prefixed binary
} ProtocolMode;

// Connection state
typedef struct {
    SOCKET socket;
    char host[256];
    int port;
    bool connected;
    bool authenticated;
    char current_db[64];
    char username[64];
    
    // Protocol state
    ProtocolMode protocol_mode;
    uint32_t query_id_counter;
    NetConnection *net_conn;
} ClientState;

// Command history
typedef struct {
    char *entries[MAX_HISTORY];
    int count;
    int position;
} History;

static ClientState g_client = {0};
static History g_history = {0};

// ============================================================================
// UTILITIES
// ============================================================================

static void trim_whitespace(char *str) {
    if (!str) return;
    
    // Leading whitespace
    char *start = str;
    while (*start && (*start == ' ' || *start == '\t' || 
                      *start == '\n' || *start == '\r')) {
        start++;
    }
    
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    
    // Trailing whitespace
    size_t len = strlen(str);
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' || 
                       str[len-1] == '\n' || str[len-1] == '\r')) {
        str[--len] = '\0';
    }
}

static void history_add(const char *cmd) {
    if (!cmd || strlen(cmd) == 0) return;
    
    // Don't add duplicates
    if (g_history.count > 0 && 
        strcmp(g_history.entries[g_history.count - 1], cmd) == 0) {
        return;
    }
    
    if (g_history.count >= MAX_HISTORY) {
        free(g_history.entries[0]);
        memmove(g_history.entries, g_history.entries + 1, 
                (MAX_HISTORY - 1) * sizeof(char*));
        g_history.count--;
    }
    
    g_history.entries[g_history.count++] = strdup(cmd);
    g_history.position = g_history.count;
}

static void history_free(void) {
    for (int i = 0; i < g_history.count; i++) {
        free(g_history.entries[i]);
    }
    memset(&g_history, 0, sizeof(g_history));
}

static void print_banner(void) {
    printf("\n");
    printf("\033[1;36m======================================================================\033[0m\n");
    printf("\033[1;35m  INVENTIXDB CLIENT v%s\033[0m - Remote Database Client\n", CLIENT_VERSION);
    printf("\033[32m  Secure | Interactive | Distributed\033[0m\n");
    printf("\033[1;36m======================================================================\033[0m\n");
    printf("\n");
    printf("  Type '\033[33mhelp\033[0m' for commands, '\033[33mquit\033[0m' to exit\n");
    printf("  Use '\033[33m\\connect <host> <port>\033[0m' to connect to server\n");
    printf("\n");
}

static void print_help(void) {
    printf("\n\033[1;36m===================== INVENTIXDB CLIENT HELP / MADAD =====================\033[0m\n");
    printf("\033[1;32m  InventixDB supports BOTH English AND Hinglish/Urdlish - Mix them freely!\033[0m\n");
    
    printf("\n\033[1;33mCONNECTION / JUDNA:\033[0m\n");
    printf("  \\connect <host> <port>  - Connect to server / Server se judo\n");
    printf("  \\disconnect             - Disconnect / Alahida karo\n");
    printf("  \\reconnect              - Reconnect / Phir judo\n");
    printf("  \\status                 - Show status / Halat dikhao\n");
    
    printf("\n\033[1;33mAUTHENTICATION / TASDIQ:\033[0m\n");
    printf("  LOGIN <user> <pass>     - Authenticate / Tasdiq karo\n");
    printf("  LOGOUT                  - End session / Session khatam\n");
    printf("  WHOAMI                  - Show current user / Kaun hoon main\n");
    
    printf("\n\033[1;33mDATABASE / MALOOMAT:\033[0m\n");
    printf("  USE <database>          - Switch database / ISTEMAAL <naam>\n");
    printf("  SHOW DATABASES          - List databases / DEKHO DATABASES\n");
    printf("  CREATE DATABASE <name>  - Create database\n");
    
    printf("\n\033[1;33mTABLES / JADWAL:\033[0m\n");
    printf("  SHOW TABLES | DEKHO TABLES              - List tables\n");
    printf("  CREATE TABLE <n> (...) | TABLE BANAO    - Create table\n");
    printf("  DROP TABLE <name> | GIRAO TABLE <naam>  - Drop table\n");
    
    printf("\n\033[1;33mDATA (English | Hinglish):\033[0m\n");
    printf("  INSERT INTO <t> VALUES (...) | DAALO MEIN <t> MAAN (...)\n");
    printf("  SELECT * FROM <table>        | DHUNDO * SE <table>\n");
    printf("  UPDATE t SET ...             | BADLO t RAKHO_YEH ...\n");
    printf("  DELETE FROM <table> ...      | NIKALO SE <table> ...\n");
    printf("  ... WHERE condition          | ... JAHAN sharait\n");
    
    printf("\n\033[1;33mJOINS / JODNA:\033[0m\n");
    printf("  JOIN | MILAO     LEFT | BAAYA     RIGHT | DAAYA\n");
    printf("  FULL | POORA     OUTER | BAHAR    NATURAL | KUDRATI\n");
    
    printf("\n\033[1;33mAGGREGATION / IKATHA:\033[0m\n");
    printf("  COUNT(*) | GINO(*)       SUM(col) | JODO(col)\n");
    printf("  AVG(col) | AUSAAT(col)   MAX | SABSE_BADA   MIN | SABSE_CHOTA\n");
    printf("  GROUP BY | SAMOOH DWARA  ORDER BY | KRAM DWARA\n");
    
    printf("\n\033[1;33mTRANSACTIONS / LENDEN:\033[0m\n");
    printf("  BEGIN | SHURU   COMMIT | PUKKA   ROLLBACK | WAPAS\n");
    
    printf("\n\033[1;33mCLIENT / CLIENT:\033[0m\n");
    printf("  \\history                - Show command history\n");
    printf("  \\clear                  - Clear screen\n");
    printf("  help | MADAD            - Show this help\n");
    printf("  quit | exit | NIKLO     - Exit client / Bahar niklo\n");
    
    printf("\n\033[1;32m TIP: Mix freely! Example: SELECT * SE users JAHAN age > 20 ORDER DWARA name\033[0m\n");
    printf("\033[1;36m=========================================================================\033[0m\n\n");
}

// ============================================================================
// NETWORKING
// ============================================================================

static int init_network(void) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "Error: WSAStartup failed\n");
        return -1;
    }
#endif
    return 0;
}

static void cleanup_network(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

// Forward declaration
static void disconnect_from_server(void);

static int connect_to_server(const char *host, int port) {
    // Disconnect if already connected
    if (g_client.connected) {
        disconnect_from_server();
    }
    
    // Initialize network subsystem
    net_init();
    
    g_client.socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_client.socket == INVALID_SOCKET) {
        fprintf(stderr, "Error: Cannot create socket\n");
        return -1;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(g_client.socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));
    
    // Set timeout
#ifdef _WIN32
    DWORD timeout = 10000;
    setsockopt(g_client.socket, SOL_SOCKET, SO_RCVTIMEO, 
               (const char*)&timeout, sizeof(timeout));
    setsockopt(g_client.socket, SOL_SOCKET, SO_SNDTIMEO, 
               (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv = {10, 0};
    setsockopt(g_client.socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(g_client.socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    
    printf("Connecting to %s:%d...\n", host, port);
    
    if (connect(g_client.socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Error: Cannot connect to %s:%d\n", host, port);
        close_socket(g_client.socket);
        return -1;
    }
    
    strncpy(g_client.host, host, sizeof(g_client.host) - 1);
    g_client.port = port;
    g_client.connected = true;
    strcpy(g_client.current_db, "public");
    g_client.query_id_counter = 0;
    
    // Default to legacy protocol (server will upgrade if supported)
    g_client.protocol_mode = PROTOCOL_LEGACY;
    
    printf("Connected to InventixDB at %s:%d (protocol: %s)\n", 
           host, port,
           g_client.protocol_mode == PROTOCOL_BINARY ? "binary" : "text");
    return 0;
}

// Switch to binary protocol mode
static int upgrade_to_binary_protocol(void) {
    if (!g_client.connected) {
        printf("Not connected\n");
        return -1;
    }
    
    // Try sending a ping to test binary protocol support
    if (net_send_ping(g_client.socket) == 0) {
        NetMessageHeader header;
        char *payload = NULL;
        size_t len = 0;
        
        if (net_recv_message(g_client.socket, &header, &payload, &len, 5000) == 0) {
            if (header.type == MSG_TYPE_PONG) {
                g_client.protocol_mode = PROTOCOL_BINARY;
                printf("Upgraded to binary protocol\n");
                free(payload);
                return 0;
            }
            free(payload);
        }
    }
    
    printf("Server does not support binary protocol, using legacy mode\n");
    return -1;
}

static void disconnect_from_server(void) {
    if (g_client.connected) {
        // Send close message if using binary protocol
        if (g_client.protocol_mode == PROTOCOL_BINARY && g_client.net_conn) {
            NetMessageHeader header = {0};
            header.type = MSG_TYPE_CLOSE;
            net_send_message(g_client.socket, &header, NULL, 0);
            async_disconnect(g_client.net_conn);
            g_client.net_conn = NULL;
        } else {
            close_socket(g_client.socket);
        }
        g_client.connected = false;
        printf("Disconnected\n");
    } else {
        printf("Not connected\n");
    }
}

// Send query using binary protocol
static int send_query_binary(const char *query, char *response, size_t response_capacity) {
    g_client.query_id_counter++;
    
    // Send query
    if (net_send_query(g_client.socket, g_client.query_id_counter, 
                       query, strlen(query), 60000) < 0) {
        strncpy(response, "Error: Failed to send query\n", response_capacity);
        g_client.connected = false;
        return -1;
    }
    
    // Receive response
    NetMessageHeader header;
    char *payload = NULL;
    size_t payload_len = 0;
    
    if (net_recv_message(g_client.socket, &header, &payload, &payload_len, 60000) < 0) {
        strncpy(response, "Error: Failed to receive response\n", response_capacity);
        g_client.connected = false;
        return -1;
    }
    
    // Parse response
    size_t copy_len = 0;
    
    if (header.type == MSG_TYPE_QUERY_RESULT && payload_len >= sizeof(NetQueryResult)) {
        NetQueryResult *qr = (NetQueryResult *)payload;
        if (qr->result_len > 0) {
            copy_len = qr->result_len;
            if (copy_len >= response_capacity) copy_len = response_capacity - 1;
            memcpy(response, payload + sizeof(NetQueryResult), copy_len);
            response[copy_len] = '\0';
        } else {
            strcpy(response, "OK\n");
            copy_len = 3;
        }
    } else if (header.type == MSG_TYPE_ERROR) {
        copy_len = payload_len;
        if (copy_len >= response_capacity) copy_len = response_capacity - 1;
        memcpy(response, payload, copy_len);
        response[copy_len] = '\0';
    } else if (header.type == MSG_TYPE_PONG) {
        strcpy(response, "PONG\n");
        copy_len = 5;
    } else {
        // Handle raw text response for backward compatibility
        copy_len = payload_len;
        if (copy_len >= response_capacity) copy_len = response_capacity - 1;
        memcpy(response, payload, copy_len);
        response[copy_len] = '\0';
    }
    
    free(payload);
    return (int)copy_len;
}

// Send query using legacy text protocol
static int send_query_legacy(const char *query, char *response, size_t response_capacity) {
    // Send query
    int sent = send(g_client.socket, query, (int)strlen(query), 0);
    if (sent <= 0) {
        strncpy(response, "Error: Failed to send query\n", response_capacity);
        g_client.connected = false;
        return -1;
    }
    
    // Receive response
    size_t total = 0;
    bool found_eof = false;
    
    while (!found_eof && total < response_capacity - 1) {
        int len = recv(g_client.socket, response + total, 
                       (int)(response_capacity - total - 1), 0);
        if (len <= 0) {
            if (total == 0) {
                strncpy(response, "Error: Connection lost\n", response_capacity);
                g_client.connected = false;
                return -1;
            }
            break;
        }
        total += len;
        response[total] = '\0';
        
        // Check for EOF marker
        if (strstr(response, EOF_MARKER)) {
            found_eof = true;
        }
    }
    
    // Remove EOF marker
    char *marker = strstr(response, EOF_MARKER);
    if (marker) {
        *marker = '\0';
        total = marker - response;
    }
    
    return (int)total;
}

// Main send function - uses appropriate protocol
static int send_query(const char *query, char *response, size_t response_capacity) {
    if (!g_client.connected) {
        strncpy(response, "Error: Not connected to server\n", response_capacity);
        return -1;
    }
    
    if (g_client.protocol_mode == PROTOCOL_BINARY) {
        return send_query_binary(query, response, response_capacity);
    } else {
        return send_query_legacy(query, response, response_capacity);
    }
}

// ============================================================================
// COMMAND PROCESSING
// ============================================================================

static int process_meta_command(const char *cmd) {
    if (strcmp(cmd, "\\help") == 0 || strcmp(cmd, "help") == 0) {
        print_help();
        return 0;
    }
    
    if (strcmp(cmd, "\\status") == 0) {
        if (g_client.connected) {
            printf("Connected to %s:%d\n", g_client.host, g_client.port);
            printf("Current database: %s\n", g_client.current_db);
        } else {
            printf("Not connected\n");
        }
        return 0;
    }
    
    if (strcmp(cmd, "\\disconnect") == 0) {
        disconnect_from_server();
        return 0;
    }
    
    if (strcmp(cmd, "\\reconnect") == 0) {
        if (strlen(g_client.host) > 0 && g_client.port > 0) {
            connect_to_server(g_client.host, g_client.port);
        } else {
            printf("No previous connection\n");
        }
        return 0;
    }
    
    if (strcmp(cmd, "\\db") == 0) {
        printf("Current database: %s\n", g_client.current_db);
        return 0;
    }
    
    if (strcmp(cmd, "\\list") == 0) {
        if (!g_client.connected) {
            printf("Not connected\n");
            return 0;
        }
        char response[BUFFER_SIZE];
        send_query("SHOW DATABASES", response, sizeof(response));
        printf("%s", response);
        return 0;
    }
    
    if (strcmp(cmd, "\\history") == 0) {
        printf("\n=== Command History ===\n");
        for (int i = 0; i < g_history.count; i++) {
            printf("%3d: %s\n", i + 1, g_history.entries[i]);
        }
        printf("\n");
        return 0;
    }
    
    if (strcmp(cmd, "\\clear") == 0) {
#ifdef _WIN32
        system("cls");
#else
        system("clear");
#endif
        return 0;
    }
    
    // Parse commands with arguments
    if (strncmp(cmd, "\\connect ", 9) == 0) {
        char host[256] = DEFAULT_HOST;
        int port = DEFAULT_PORT;
        
        const char *args = cmd + 9;
        if (sscanf(args, "%255s %d", host, &port) < 1) {
            printf("Usage: \\connect <host> [port]\n");
            return 0;
        }
        
        connect_to_server(host, port);
        return 0;
    }
    
    if (strncmp(cmd, "\\use ", 5) == 0) {
        if (!g_client.connected) {
            printf("Not connected\n");
            return 0;
        }
        
        const char *db = cmd + 5;
        while (*db == ' ') db++;
        
        char query[256];
        snprintf(query, sizeof(query), "USE %s", db);
        
        char response[BUFFER_SIZE];
        if (send_query(query, response, sizeof(response)) >= 0) {
            if (strstr(response, "Error") == NULL) {
                strncpy(g_client.current_db, db, sizeof(g_client.current_db) - 1);
            }
            printf("%s", response);
        }
        return 0;
    }
    
    // Unknown meta command
    printf("Unknown command: %s\n", cmd);
    printf("Type 'help' for available commands\n");
    return 0;
}

static int process_command(const char *input) {
    char cmd[BUFFER_SIZE];
    strncpy(cmd, input, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    trim_whitespace(cmd);
    
    if (strlen(cmd) == 0) {
        return 0;
    }
    
    // Check for exit commands
    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0 ||
        strcmp(cmd, "bahar") == 0 || strcmp(cmd, "niklo") == 0 ||
        strcmp(cmd, "\\q") == 0 || strcmp(cmd, "\\quit") == 0) {
        return -1;  // Signal to exit
    }
    
    // Meta commands (start with backslash)
    if (cmd[0] == '\\' || strcmp(cmd, "help") == 0) {
        return process_meta_command(cmd);
    }
    
    // SQL query - need to be connected
    if (!g_client.connected) {
        printf("Not connected. Use '\\connect <host> <port>' to connect.\n");
        return 0;
    }
    
    // Add to history
    history_add(cmd);
    
    // Send to server
    char response[BUFFER_SIZE];
    int result = send_query(cmd, response, sizeof(response));
    
    if (result >= 0) {
        printf("%s", response);
        if (strlen(response) > 0 && response[strlen(response) - 1] != '\n') {
            printf("\n");
        }
    }
    
    return 0;
}

// ============================================================================
// MAIN
// ============================================================================

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  -h, --host <host>   Server host (default: %s)\n", DEFAULT_HOST);
    printf("  -p, --port <port>   Server port (default: %d)\n", DEFAULT_PORT);
    printf("  -d, --database <db> Initial database (default: public)\n");
    printf("  -c, --command <sql> Execute SQL and exit\n");
    printf("  -f, --file <file>   Execute SQL from file and exit\n");
    printf("  --help              Show this help\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s -h 192.168.1.100 -p 9876\n", prog);
    printf("  %s -c \"SELECT * FROM users\"\n", prog);
    printf("  %s -f queries.sql\n", prog);
    printf("\n");
}

int main(int argc, char *argv[]) {
    char host[256] = DEFAULT_HOST;
    int port = DEFAULT_PORT;
    char *initial_command = NULL;
    char *sql_file = NULL;
    bool auto_connect = false;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--host") == 0) {
            if (i + 1 < argc) {
                strncpy(host, argv[++i], sizeof(host) - 1);
                auto_connect = true;
            }
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc) {
                port = atoi(argv[++i]);
                auto_connect = true;
            }
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--database") == 0) {
            if (i + 1 < argc) {
                strncpy(g_client.current_db, argv[++i], sizeof(g_client.current_db) - 1);
            }
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--command") == 0) {
            if (i + 1 < argc) {
                initial_command = argv[++i];
                auto_connect = true;
            }
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) {
            if (i + 1 < argc) {
                sql_file = argv[++i];
                auto_connect = true;
            }
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }
    
    // Initialize networking
    if (init_network() != 0) {
        return 1;
    }
    
    // Print banner in interactive mode
    if (!initial_command && !sql_file) {
        print_banner();
    }
    
    // Auto-connect if host/port specified
    if (auto_connect) {
        if (connect_to_server(host, port) != 0) {
            cleanup_network();
            return 1;
        }
    }
    
    // Execute single command mode
    if (initial_command) {
        char response[BUFFER_SIZE];
        int result = send_query(initial_command, response, sizeof(response));
        if (result >= 0) {
            printf("%s", response);
        }
        disconnect_from_server();
        cleanup_network();
        return result >= 0 ? 0 : 1;
    }
    
    // Execute file mode
    if (sql_file) {
        FILE *f = fopen(sql_file, "r");
        if (!f) {
            fprintf(stderr, "Error: Cannot open file: %s\n", sql_file);
            disconnect_from_server();
            cleanup_network();
            return 1;
        }
        
        char line[BUFFER_SIZE];
        char query[BUFFER_SIZE] = "";
        
        while (fgets(line, sizeof(line), f)) {
            // Skip comments
            if (line[0] == '-' && line[1] == '-') continue;
            if (line[0] == '#') continue;
            
            // Append to query
            strncat(query, line, sizeof(query) - strlen(query) - 1);
            
            // Check for semicolon (end of statement)
            char *semi = strchr(query, ';');
            if (semi) {
                *semi = '\0';
                trim_whitespace(query);
                
                if (strlen(query) > 0) {
                    printf("> %s\n", query);
                    char response[BUFFER_SIZE];
                    if (send_query(query, response, sizeof(response)) >= 0) {
                        printf("%s\n", response);
                    }
                }
                
                // Keep any remainder after semicolon
                strcpy(query, semi + 1);
            }
        }
        
        // Execute any remaining query
        trim_whitespace(query);
        if (strlen(query) > 0) {
            printf("> %s\n", query);
            char response[BUFFER_SIZE];
            if (send_query(query, response, sizeof(response)) >= 0) {
                printf("%s\n", response);
            }
        }
        
        fclose(f);
        disconnect_from_server();
        cleanup_network();
        return 0;
    }
    
    // Interactive mode
    char input[BUFFER_SIZE];
    
    while (1) {
        // Generate prompt
        if (g_client.connected) {
            printf("%s@%s:%d> ", g_client.current_db, g_client.host, g_client.port);
        } else {
            printf("(not connected)> ");
        }
        fflush(stdout);
        
        // Read input
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break;
        }
        
        // Process command
        if (process_command(input) < 0) {
            break;
        }
    }
    
    // Cleanup
    disconnect_from_server();
    history_free();
    cleanup_network();
    
    printf("Goodbye!\n");
    return 0;
}
