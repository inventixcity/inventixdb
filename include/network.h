/**
 * InventixDB Network Protocol System
 * 
 * Features:
 * - Length-prefixed binary protocol (no more <<EOF>> markers)
 * - Connection pooling for master-worker communication
 * - Async I/O (IOCP on Windows, io_uring on Linux)
 * - Protocol versioning and backward compatibility
 * - Compression support for large payloads
 * - Heartbeat and keepalive mechanisms
 */

#ifndef INVENTIX_NETWORK_H
#define INVENTIX_NETWORK_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#define SOCK_ERROR SOCKET_ERROR
#define closesock closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int socket_t;
#define INVALID_SOCK -1
#define SOCK_ERROR -1
#define closesock close
#endif

// ============================================================================
// PROTOCOL CONSTANTS
// ============================================================================

#define NET_PROTOCOL_VERSION    0x0100      // v1.0
#define NET_MAGIC               0x494E5658  // "INVX" in hex
#define NET_MAX_MESSAGE_SIZE    (64 * 1024 * 1024)  // 64 MB max
#define NET_HEADER_SIZE         sizeof(NetMessageHeader)
#define NET_POOL_DEFAULT_SIZE   16
#define NET_POOL_MAX_SIZE       256
#define NET_RECV_BUFFER_SIZE    (64 * 1024)  // 64 KB receive buffer
#define NET_SEND_BUFFER_SIZE    (64 * 1024)  // 64 KB send buffer
#define NET_KEEPALIVE_INTERVAL  30000       // 30 seconds
#define NET_READ_TIMEOUT        60000       // 60 seconds
#define NET_CONNECT_TIMEOUT     10000       // 10 seconds

// IOCP specific
#define NET_IOCP_CONCURRENT     0           // Use default (num CPUs)
#define NET_OVERLAPPED_POOL     1024        // Pre-allocated overlapped structures

// Compression threshold
#define NET_COMPRESS_THRESHOLD  (4 * 1024)  // Compress if > 4KB

// ============================================================================
// MESSAGE TYPES
// ============================================================================

typedef enum {
    // Client-Server messages
    MSG_TYPE_QUERY          = 0x01,     // SQL/NoSQL query request
    MSG_TYPE_QUERY_RESULT   = 0x02,     // Query response
    MSG_TYPE_ERROR          = 0x03,     // Error response
    MSG_TYPE_AUTH_REQUEST   = 0x04,     // Authentication request
    MSG_TYPE_AUTH_RESPONSE  = 0x05,     // Authentication response
    MSG_TYPE_PING           = 0x06,     // Keepalive ping
    MSG_TYPE_PONG           = 0x07,     // Keepalive pong
    MSG_TYPE_CLOSE          = 0x08,     // Connection close
    
    // Cluster/Distributed messages
    MSG_TYPE_CLUSTER_JOIN   = 0x10,     // Node joining cluster
    MSG_TYPE_CLUSTER_LEAVE  = 0x11,     // Node leaving cluster
    MSG_TYPE_CLUSTER_SYNC   = 0x12,     // Data synchronization
    MSG_TYPE_CLUSTER_FORWARD = 0x13,    // Query forwarding
    MSG_TYPE_CLUSTER_RESULT = 0x14,     // Forwarded query result
    MSG_TYPE_CLUSTER_HEARTBEAT = 0x15,  // Cluster heartbeat
    
    // Replication messages
    MSG_TYPE_RAFT_VOTE_REQ  = 0x20,     // Raft vote request
    MSG_TYPE_RAFT_VOTE_RESP = 0x21,     // Raft vote response
    MSG_TYPE_RAFT_APPEND    = 0x22,     // Raft append entries
    MSG_TYPE_RAFT_APPEND_RESP = 0x23,   // Raft append response
    
    // Admin messages
    MSG_TYPE_STATUS_REQUEST = 0x30,     // Status request
    MSG_TYPE_STATUS_RESPONSE = 0x31,    // Status response
    MSG_TYPE_SHUTDOWN       = 0x32,     // Shutdown command
    
    MSG_TYPE_MAX            = 0xFF
} NetMessageType;

// ============================================================================
// MESSAGE FLAGS
// ============================================================================

typedef enum {
    MSG_FLAG_NONE           = 0x00,
    MSG_FLAG_COMPRESSED     = 0x01,     // Payload is compressed
    MSG_FLAG_ENCRYPTED      = 0x02,     // Payload is encrypted
    MSG_FLAG_FRAGMENTED     = 0x04,     // Message is fragmented
    MSG_FLAG_LAST_FRAGMENT  = 0x08,     // Last fragment
    MSG_FLAG_PRIORITY_HIGH  = 0x10,     // High priority message
    MSG_FLAG_NO_RESPONSE    = 0x20,     // No response expected
    MSG_FLAG_BROADCAST      = 0x40,     // Broadcast to all nodes
} NetMessageFlags;

// ============================================================================
// MESSAGE HEADER (Binary Protocol)
// ============================================================================

#pragma pack(push, 1)

/**
 * Wire format for all messages:
 * [4 bytes magic][2 bytes version][1 byte type][1 byte flags]
 * [4 bytes sequence][4 bytes payload_len][payload...]
 */
typedef struct {
    uint32_t magic;         // NET_MAGIC for validation
    uint16_t version;       // Protocol version
    uint8_t  type;          // NetMessageType
    uint8_t  flags;         // NetMessageFlags
    uint32_t sequence;      // Sequence number for ordering
    uint32_t payload_len;   // Length of payload following header
} NetMessageHeader;

// Authentication request
typedef struct {
    uint32_t username_len;
    uint32_t password_len;
    // Followed by username and password bytes
} NetAuthRequest;

// Authentication response
typedef struct {
    uint32_t success;       // 0 = failed, 1 = success
    uint32_t session_id;    // Session ID on success
    uint32_t token_len;     // Auth token length
    uint32_t error_len;     // Error message length (if failed)
    // Followed by token or error message
} NetAuthResponse;

// Query request
typedef struct {
    uint32_t query_id;      // Client-assigned query ID
    uint32_t query_len;     // Query string length
    uint32_t timeout_ms;    // Query timeout in milliseconds
    // Followed by query string
} NetQueryRequest;

// Query result
typedef struct {
    uint32_t query_id;      // Matching query ID
    uint32_t status;        // 0 = success, non-zero = error
    uint32_t rows_affected; // Number of rows affected
    uint32_t result_len;    // Result data length
    // Followed by result data
} NetQueryResult;

// Cluster forward request
typedef struct {
    uint32_t source_node;   // Source node ID
    uint32_t target_node;   // Target node ID (0 = broadcast)
    uint32_t query_id;      // Query ID for correlation
    uint32_t query_len;     // Query length
    // Followed by query string
} NetClusterForward;

// Heartbeat message
typedef struct {
    uint32_t node_id;       // Sender node ID
    uint64_t timestamp;     // Unix timestamp in ms
    uint32_t load;          // Current load percentage (0-100)
    uint32_t conn_count;    // Active connection count
} NetHeartbeat;

#pragma pack(pop)

// ============================================================================
// CONNECTION STATE
// ============================================================================

typedef enum {
    CONN_STATE_DISCONNECTED = 0,
    CONN_STATE_CONNECTING,
    CONN_STATE_CONNECTED,
    CONN_STATE_AUTHENTICATED,
    CONN_STATE_CLOSING,
    CONN_STATE_ERROR
} NetConnState;

// ============================================================================
// ASYNC I/O OPERATION TYPES
// ============================================================================

typedef enum {
    IO_OP_NONE = 0,
    IO_OP_ACCEPT,
    IO_OP_CONNECT,
    IO_OP_READ,
    IO_OP_WRITE,
    IO_OP_DISCONNECT
} IOOperationType;

// ============================================================================
// ASYNC I/O CONTEXT (Platform-specific)
// ============================================================================

#ifdef _WIN32
// Windows IOCP structures
typedef struct {
    OVERLAPPED overlapped;          // Must be first for IOCP
    IOOperationType op_type;
    WSABUF wsa_buf;
    char *buffer;
    size_t buffer_size;
    size_t bytes_transferred;
    void *connection;               // Back-pointer to NetConnection
    void *user_data;
} IOContext;
#else
// Linux io_uring placeholder (falls back to epoll if not available)
typedef struct {
    IOOperationType op_type;
    char *buffer;
    size_t buffer_size;
    size_t bytes_transferred;
    void *connection;
    void *user_data;
    int fd;
} IOContext;
#endif

// ============================================================================
// CONNECTION STRUCTURE
// ============================================================================

typedef struct NetConnection {
    socket_t socket;
    NetConnState state;
    
    // Remote endpoint info
    char remote_ip[64];
    uint16_t remote_port;
    
    // Protocol state
    uint32_t sequence_out;          // Outgoing sequence number
    uint32_t sequence_in;           // Expected incoming sequence
    uint32_t session_id;            // Session ID (after auth)
    
    // Receive buffer (for assembling fragmented messages)
    char *recv_buffer;
    size_t recv_buffer_size;
    size_t recv_buffer_used;
    
    // Send buffer (for queued writes)
    char *send_buffer;
    size_t send_buffer_size;
    size_t send_buffer_used;
    
    // Async I/O contexts
#ifdef _WIN32
    IOContext *read_ctx;
    IOContext *write_ctx;
#else
    IOContext read_ctx;
    IOContext write_ctx;
#endif
    
    // Statistics
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t last_activity;         // Timestamp of last activity
    
    // Pool management
    bool in_pool;                   // Is this connection pooled?
    int pool_index;                 // Index in pool (-1 if not pooled)
    
    // User data
    void *user_data;
    
    // Linked list for pool
    struct NetConnection *next;
    struct NetConnection *prev;
} NetConnection;

// ============================================================================
// CONNECTION POOL
// ============================================================================

typedef struct {
    // Pool configuration
    char target_host[256];
    uint16_t target_port;
    size_t min_connections;
    size_t max_connections;
    
    // Active connections
    NetConnection **connections;
    size_t count;
    size_t capacity;
    
    // Free list for quick allocation
    NetConnection *free_list;
    size_t free_count;
    
    // Statistics
    uint64_t total_acquired;
    uint64_t total_released;
    uint64_t total_created;
    uint64_t total_destroyed;
    uint64_t peak_usage;
    
    // Synchronization
#ifdef _WIN32
    CRITICAL_SECTION lock;
#else
    pthread_mutex_t lock;
#endif
    
    // Health check
    uint64_t last_health_check;
    size_t health_check_interval_ms;
    
    bool initialized;
} ConnectionPool;

// ============================================================================
// ASYNC SERVER (IOCP/io_uring)
// ============================================================================

typedef void (*NetMessageHandler)(NetConnection *conn, NetMessageHeader *header, 
                                  const char *payload, size_t payload_len);
typedef void (*NetConnectHandler)(NetConnection *conn);
typedef void (*NetDisconnectHandler)(NetConnection *conn);
typedef void (*NetErrorHandler)(NetConnection *conn, int error_code, const char *message);

typedef struct {
    // Server socket
    socket_t listen_socket;
    uint16_t port;
    
    // Async I/O handle
#ifdef _WIN32
    HANDLE iocp_handle;
    LPFN_ACCEPTEX fn_accept_ex;
    LPFN_GETACCEPTEXSOCKADDRS fn_get_accept_ex_sockaddrs;
#else
    int epoll_fd;
    // io_uring_fd when available
#endif
    
    // Worker threads
    size_t worker_count;
    void **worker_threads;
    bool running;
    
    // Pre-allocated accept contexts
    IOContext *accept_contexts;
    size_t accept_context_count;
    
    // Active connections
    NetConnection **connections;
    size_t connection_count;
    size_t connection_capacity;
    
    // Callbacks
    NetMessageHandler on_message;
    NetConnectHandler on_connect;
    NetDisconnectHandler on_disconnect;
    NetErrorHandler on_error;
    
    // Statistics
    uint64_t total_connections;
    uint64_t active_connections;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    
    // Configuration
    size_t max_connections;
    size_t recv_buffer_size;
    size_t send_buffer_size;
    
} AsyncServer;

// ============================================================================
// MESSAGE BUILDING & PARSING
// ============================================================================

// Message builder for constructing outgoing messages
typedef struct {
    char *buffer;
    size_t capacity;
    size_t used;
    NetMessageHeader *header;
} NetMessageBuilder;

// Message parser for incoming messages
typedef struct {
    const char *data;
    size_t data_len;
    size_t offset;
    NetMessageHeader header;
    bool header_parsed;
} NetMessageParser;

// ============================================================================
// CORE NETWORK FUNCTIONS
// ============================================================================

// Initialization
int net_init(void);
void net_shutdown(void);

// Message building
NetMessageBuilder *net_msg_builder_create(size_t initial_capacity);
void net_msg_builder_destroy(NetMessageBuilder *builder);
int net_msg_builder_set_type(NetMessageBuilder *builder, NetMessageType type);
int net_msg_builder_set_flags(NetMessageBuilder *builder, uint8_t flags);
int net_msg_builder_append(NetMessageBuilder *builder, const void *data, size_t len);
int net_msg_builder_finalize(NetMessageBuilder *builder);
const char *net_msg_builder_data(NetMessageBuilder *builder, size_t *out_len);

// Message parsing
int net_msg_parse_header(const char *data, size_t len, NetMessageHeader *out_header);
bool net_msg_validate_header(const NetMessageHeader *header);
const char *net_msg_get_payload(const char *data, size_t *out_len);

// Synchronous I/O helpers
int net_send_message(socket_t sock, const NetMessageHeader *header, 
                     const char *payload, size_t payload_len);
int net_recv_message(socket_t sock, NetMessageHeader *out_header, 
                     char **out_payload, size_t *out_len, int timeout_ms);

// High-level message sending
int net_send_query(socket_t sock, uint32_t query_id, const char *query, 
                   size_t query_len, int timeout_ms);
int net_send_result(socket_t sock, uint32_t query_id, int status, 
                    const char *result, size_t result_len);
int net_send_error(socket_t sock, uint32_t query_id, const char *error_msg);
int net_send_ping(socket_t sock);
int net_send_pong(socket_t sock);

// ============================================================================
// CONNECTION POOL FUNCTIONS
// ============================================================================

ConnectionPool *conn_pool_create(const char *host, uint16_t port, 
                            size_t min_conns, size_t max_conns);
void conn_pool_destroy(ConnectionPool *pool);
NetConnection *conn_pool_acquire(ConnectionPool *pool, int timeout_ms);
void conn_pool_release(ConnectionPool *pool, NetConnection *conn);
int conn_pool_health_check(ConnectionPool *pool);
void conn_pool_get_stats(ConnectionPool *pool, uint64_t *acquired, uint64_t *released,
                    uint64_t *active, uint64_t *peak);

// ============================================================================
// ASYNC SERVER FUNCTIONS
// ============================================================================

AsyncServer *async_server_create(uint16_t port, size_t max_connections);
void async_server_destroy(AsyncServer *server);
int async_server_start(AsyncServer *server, size_t worker_threads);
void async_server_stop(AsyncServer *server);
void async_server_set_message_handler(AsyncServer *server, NetMessageHandler handler);
void async_server_set_connect_handler(AsyncServer *server, NetConnectHandler handler);
void async_server_set_disconnect_handler(AsyncServer *server, NetDisconnectHandler handler);
void async_server_set_error_handler(AsyncServer *server, NetErrorHandler handler);

// Send message to connection (thread-safe)
int async_server_send(AsyncServer *server, NetConnection *conn,
                      const NetMessageHeader *header, const char *payload);

// Broadcast to all connections
int async_server_broadcast(AsyncServer *server, const NetMessageHeader *header,
                           const char *payload);

// ============================================================================
// ASYNC CLIENT FUNCTIONS
// ============================================================================

NetConnection *async_connect(const char *host, uint16_t port, int timeout_ms);
void async_disconnect(NetConnection *conn);
int async_send(NetConnection *conn, const NetMessageHeader *header, const char *payload);
int async_recv(NetConnection *conn, NetMessageHeader *out_header, 
               char **out_payload, size_t *out_len, int timeout_ms);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Network byte order helpers (already in winsock2.h/arpa/inet.h but here for completeness)
uint32_t net_htonl(uint32_t hostlong);
uint16_t net_htons(uint16_t hostshort);
uint32_t net_ntohl(uint32_t netlong);
uint16_t net_ntohs(uint16_t netshort);

// Timestamp
uint64_t net_timestamp_ms(void);

// Error handling
const char *net_error_string(int error_code);
int net_last_error(void);

// Debug
void net_debug_header(const NetMessageHeader *header);
void net_stats_print(void);

// ============================================================================
// HINGLISH ALIASES (for consistency)
// ============================================================================

#define jaal_shuru             net_init
#define jaal_band              net_shutdown
#define sandesh_bhejo          net_send_message
#define sandesh_prapt          net_recv_message
#define talab_banao_jaal       pool_create
#define talab_mitao_jaal       pool_destroy
#define jod_prapt              pool_acquire
#define jod_wapas              pool_release

#endif // INVENTIX_NETWORK_H
