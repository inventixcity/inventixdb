/**
 * InventixDB Network Protocol Implementation
 * 
 * Length-prefixed binary protocol, connection pooling, and async I/O.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")
#ifndef WSAETIMEDOUT
#define WSAETIMEDOUT 10060
#endif
#ifndef EAGAIN
#define EAGAIN WSAEWOULDBLOCK
#endif
#else
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#define WSAETIMEDOUT ETIMEDOUT
#endif

#include "network.h"
#include "logger.h"

// ============================================================================
// GLOBAL STATE
// ============================================================================

static bool g_net_initialized = false;

#ifdef _WIN32
static CRITICAL_SECTION g_net_lock;
#else
static pthread_mutex_t g_net_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

// Global statistics
static struct {
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t connections_created;
    uint64_t connections_destroyed;
    uint64_t pool_hits;
    uint64_t pool_misses;
} g_net_stats = {0};

// ============================================================================
// INITIALIZATION
// ============================================================================

int net_init(void) {
    if (g_net_initialized) return 0;
    
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        LOG_ERROR("WSAStartup failed: %d", WSAGetLastError());
        return -1;
    }
    InitializeCriticalSection(&g_net_lock);
#endif
    
    memset(&g_net_stats, 0, sizeof(g_net_stats));
    g_net_initialized = true;
    
    LOG_INFO("Network subsystem initialized (protocol v%d.%d)", 
             NET_PROTOCOL_VERSION >> 8, NET_PROTOCOL_VERSION & 0xFF);
    return 0;
}

void net_shutdown(void) {
    if (!g_net_initialized) return;
    
#ifdef _WIN32
    DeleteCriticalSection(&g_net_lock);
    WSACleanup();
#endif
    
    g_net_initialized = false;
    LOG_INFO("Network subsystem shutdown");
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

uint64_t net_timestamp_ms(void) {
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER li;
    GetSystemTimeAsFileTime(&ft);
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    // Convert from 100-nanosecond intervals since 1601 to ms since 1970
    return (li.QuadPart - 116444736000000000ULL) / 10000;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

const char *net_error_string(int error_code) {
#ifdef _WIN32
    static __declspec(thread) char buffer[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, error_code, 0, buffer, sizeof(buffer), NULL);
    return buffer;
#else
    return strerror(error_code);
#endif
}

int net_last_error(void) {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static void net_lock(void) {
#ifdef _WIN32
    EnterCriticalSection(&g_net_lock);
#else
    pthread_mutex_lock(&g_net_lock);
#endif
}

static void net_unlock(void) {
#ifdef _WIN32
    LeaveCriticalSection(&g_net_lock);
#else
    pthread_mutex_unlock(&g_net_lock);
#endif
}

// ============================================================================
// MESSAGE BUILDING
// ============================================================================

NetMessageBuilder *net_msg_builder_create(size_t initial_capacity) {
    if (initial_capacity < NET_HEADER_SIZE + 64) {
        initial_capacity = NET_HEADER_SIZE + 1024;
    }
    
    NetMessageBuilder *builder = calloc(1, sizeof(NetMessageBuilder));
    if (!builder) return NULL;
    
    builder->buffer = malloc(initial_capacity);
    if (!builder->buffer) {
        free(builder);
        return NULL;
    }
    
    builder->capacity = initial_capacity;
    builder->used = NET_HEADER_SIZE;  // Reserve space for header
    builder->header = (NetMessageHeader *)builder->buffer;
    
    // Initialize header
    memset(builder->header, 0, NET_HEADER_SIZE);
    builder->header->magic = NET_MAGIC;
    builder->header->version = NET_PROTOCOL_VERSION;
    
    return builder;
}

void net_msg_builder_destroy(NetMessageBuilder *builder) {
    if (!builder) return;
    free(builder->buffer);
    free(builder);
}

int net_msg_builder_set_type(NetMessageBuilder *builder, NetMessageType type) {
    if (!builder) return -1;
    builder->header->type = (uint8_t)type;
    return 0;
}

int net_msg_builder_set_flags(NetMessageBuilder *builder, uint8_t flags) {
    if (!builder) return -1;
    builder->header->flags = flags;
    return 0;
}

int net_msg_builder_append(NetMessageBuilder *builder, const void *data, size_t len) {
    if (!builder || !data || len == 0) return -1;
    
    // Grow buffer if needed
    while (builder->used + len > builder->capacity) {
        size_t new_capacity = builder->capacity * 2;
        if (new_capacity > NET_MAX_MESSAGE_SIZE) {
            LOG_ERROR("Message too large: %zu > %d", builder->used + len, NET_MAX_MESSAGE_SIZE);
            return -1;
        }
        
        char *new_buffer = realloc(builder->buffer, new_capacity);
        if (!new_buffer) {
            LOG_ERROR("Failed to grow message buffer");
            return -1;
        }
        
        builder->buffer = new_buffer;
        builder->header = (NetMessageHeader *)builder->buffer;
        builder->capacity = new_capacity;
    }
    
    memcpy(builder->buffer + builder->used, data, len);
    builder->used += len;
    return 0;
}

int net_msg_builder_finalize(NetMessageBuilder *builder) {
    if (!builder) return -1;
    
    // Set payload length
    builder->header->payload_len = (uint32_t)(builder->used - NET_HEADER_SIZE);
    
    // Generate sequence number
    static uint32_t g_sequence = 0;
    builder->header->sequence = ++g_sequence;
    
    return 0;
}

const char *net_msg_builder_data(NetMessageBuilder *builder, size_t *out_len) {
    if (!builder) return NULL;
    if (out_len) *out_len = builder->used;
    return builder->buffer;
}

// ============================================================================
// MESSAGE PARSING
// ============================================================================

int net_msg_parse_header(const char *data, size_t len, NetMessageHeader *out_header) {
    if (!data || len < NET_HEADER_SIZE || !out_header) {
        return -1;
    }
    
    memcpy(out_header, data, NET_HEADER_SIZE);
    
    // Validate magic
    if (out_header->magic != NET_MAGIC) {
        LOG_WARN("Invalid message magic: 0x%08X (expected 0x%08X)", 
                 out_header->magic, NET_MAGIC);
        return -1;
    }
    
    return 0;
}

bool net_msg_validate_header(const NetMessageHeader *header) {
    if (!header) return false;
    
    if (header->magic != NET_MAGIC) return false;
    if (header->type >= MSG_TYPE_MAX) return false;
    if (header->payload_len > NET_MAX_MESSAGE_SIZE - NET_HEADER_SIZE) return false;
    
    return true;
}

const char *net_msg_get_payload(const char *data, size_t *out_len) {
    if (!data) return NULL;
    
    NetMessageHeader *header = (NetMessageHeader *)data;
    if (out_len) *out_len = header->payload_len;
    
    return data + NET_HEADER_SIZE;
}

// ============================================================================
// SYNCHRONOUS I/O HELPERS
// ============================================================================

// Send all bytes with retry
static int send_all(socket_t sock, const char *data, size_t len) {
    size_t total = 0;
    
    while (total < len) {
        int sent = send(sock, data + total, (int)(len - total), 0);
        if (sent <= 0) {
            return -1;
        }
        total += sent;
    }
    
    return (int)total;
}

// Receive exact number of bytes with timeout
static int recv_exact(socket_t sock, char *buffer, size_t len, int timeout_ms) {
    size_t total = 0;
    
#ifdef _WIN32
    // Set receive timeout
    DWORD timeout = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    
    while (total < len) {
        int received = recv(sock, buffer + total, (int)(len - total), 0);
        if (received <= 0) {
            if (received == 0) return -1;  // Connection closed
            if (net_last_error() == WSAETIMEDOUT || net_last_error() == EAGAIN) {
                return -2;  // Timeout
            }
            return -1;
        }
        total += received;
    }
    
    return (int)total;
}

int net_send_message(socket_t sock, const NetMessageHeader *header, 
                     const char *payload, size_t payload_len) {
    if (sock == INVALID_SOCK || !header) return -1;
    
    // Create a complete message with header
    NetMessageHeader msg_header = *header;
    msg_header.magic = NET_MAGIC;
    msg_header.version = NET_PROTOCOL_VERSION;
    msg_header.payload_len = (uint32_t)payload_len;
    
    // Send header
    if (send_all(sock, (const char*)&msg_header, NET_HEADER_SIZE) != NET_HEADER_SIZE) {
        LOG_ERROR("Failed to send message header");
        return -1;
    }
    
    // Send payload if present
    if (payload && payload_len > 0) {
        if (send_all(sock, payload, payload_len) != (int)payload_len) {
            LOG_ERROR("Failed to send message payload");
            return -1;
        }
    }
    
    net_lock();
    g_net_stats.messages_sent++;
    g_net_stats.bytes_sent += NET_HEADER_SIZE + payload_len;
    net_unlock();
    
    return 0;
}

int net_recv_message(socket_t sock, NetMessageHeader *out_header, 
                     char **out_payload, size_t *out_len, int timeout_ms) {
    if (sock == INVALID_SOCK || !out_header) return -1;
    
    // Receive header
    char header_buf[NET_HEADER_SIZE];
    int ret = recv_exact(sock, header_buf, NET_HEADER_SIZE, timeout_ms);
    if (ret < 0) {
        return ret;
    }
    
    // Parse header
    if (net_msg_parse_header(header_buf, NET_HEADER_SIZE, out_header) < 0) {
        return -1;
    }
    
    // Validate header
    if (!net_msg_validate_header(out_header)) {
        LOG_ERROR("Invalid message header received");
        return -1;
    }
    
    // Receive payload if present
    if (out_header->payload_len > 0) {
        char *payload = malloc(out_header->payload_len + 1);
        if (!payload) {
            LOG_ERROR("Failed to allocate payload buffer");
            return -1;
        }
        
        ret = recv_exact(sock, payload, out_header->payload_len, timeout_ms);
        if (ret < 0) {
            free(payload);
            return ret;
        }
        
        payload[out_header->payload_len] = '\0';  // Null-terminate for safety
        
        if (out_payload) *out_payload = payload;
        else free(payload);
        
        if (out_len) *out_len = out_header->payload_len;
    } else {
        if (out_payload) *out_payload = NULL;
        if (out_len) *out_len = 0;
    }
    
    net_lock();
    g_net_stats.messages_received++;
    g_net_stats.bytes_received += NET_HEADER_SIZE + out_header->payload_len;
    net_unlock();
    
    return 0;
}

// ============================================================================
// HIGH-LEVEL MESSAGE SENDING
// ============================================================================

int net_send_query(socket_t sock, uint32_t query_id, const char *query, 
                   size_t query_len, int timeout_ms) {
    if (!query) return -1;
    
    // Build payload
    size_t payload_size = sizeof(NetQueryRequest) + query_len;
    char *payload = malloc(payload_size);
    if (!payload) return -1;
    
    NetQueryRequest *req = (NetQueryRequest *)payload;
    req->query_id = query_id;
    req->query_len = (uint32_t)query_len;
    req->timeout_ms = timeout_ms;
    memcpy(payload + sizeof(NetQueryRequest), query, query_len);
    
    NetMessageHeader header = {0};
    header.type = MSG_TYPE_QUERY;
    
    int ret = net_send_message(sock, &header, payload, payload_size);
    free(payload);
    
    return ret;
}

int net_send_result(socket_t sock, uint32_t query_id, int status, 
                    const char *result, size_t result_len) {
    size_t payload_size = sizeof(NetQueryResult) + result_len;
    char *payload = malloc(payload_size);
    if (!payload) return -1;
    
    NetQueryResult *res = (NetQueryResult *)payload;
    res->query_id = query_id;
    res->status = status;
    res->rows_affected = 0;
    res->result_len = (uint32_t)result_len;
    
    if (result && result_len > 0) {
        memcpy(payload + sizeof(NetQueryResult), result, result_len);
    }
    
    NetMessageHeader header = {0};
    header.type = MSG_TYPE_QUERY_RESULT;
    
    int ret = net_send_message(sock, &header, payload, payload_size);
    free(payload);
    
    return ret;
}

int net_send_error(socket_t sock, uint32_t query_id, const char *error_msg) {
    size_t msg_len = error_msg ? strlen(error_msg) : 0;
    return net_send_result(sock, query_id, -1, error_msg, msg_len);
}

int net_send_ping(socket_t sock) {
    NetMessageHeader header = {0};
    header.type = MSG_TYPE_PING;
    
    NetHeartbeat hb = {0};
    hb.timestamp = net_timestamp_ms();
    
    return net_send_message(sock, &header, (const char*)&hb, sizeof(hb));
}

int net_send_pong(socket_t sock) {
    NetMessageHeader header = {0};
    header.type = MSG_TYPE_PONG;
    
    NetHeartbeat hb = {0};
    hb.timestamp = net_timestamp_ms();
    
    return net_send_message(sock, &header, (const char*)&hb, sizeof(hb));
}

// ============================================================================
// CONNECTION POOL IMPLEMENTATION
// ============================================================================

static void pool_lock(ConnectionPool *pool) {
#ifdef _WIN32
    EnterCriticalSection(&pool->lock);
#else
    pthread_mutex_lock(&pool->lock);
#endif
}

static void pool_unlock(ConnectionPool *pool) {
#ifdef _WIN32
    LeaveCriticalSection(&pool->lock);
#else
    pthread_mutex_unlock(&pool->lock);
#endif
}

static NetConnection *create_pooled_connection(ConnectionPool *pool) {
    socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCK) {
        LOG_ERROR("Failed to create socket for pool");
        return NULL;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&opt, sizeof(opt));
    
#ifdef _WIN32
    // Set connection timeout
    DWORD timeout = NET_CONNECT_TIMEOUT;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#endif
    
    struct sockaddr_in server = {0};
    server.sin_family = AF_INET;
    server.sin_port = htons(pool->target_port);
    inet_pton(AF_INET, pool->target_host, &server.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        LOG_ERROR("Failed to connect to %s:%d", pool->target_host, pool->target_port);
        closesock(sock);
        return NULL;
    }
    
    // Create connection structure
    NetConnection *conn = calloc(1, sizeof(NetConnection));
    if (!conn) {
        closesock(sock);
        return NULL;
    }
    
    conn->socket = sock;
    conn->state = CONN_STATE_CONNECTED;
    strncpy(conn->remote_ip, pool->target_host, sizeof(conn->remote_ip) - 1);
    conn->remote_port = pool->target_port;
    conn->in_pool = true;
    conn->last_activity = net_timestamp_ms();
    
    // Allocate buffers
    conn->recv_buffer_size = NET_RECV_BUFFER_SIZE;
    conn->recv_buffer = malloc(conn->recv_buffer_size);
    conn->send_buffer_size = NET_SEND_BUFFER_SIZE;
    conn->send_buffer = malloc(conn->send_buffer_size);
    
    if (!conn->recv_buffer || !conn->send_buffer) {
        free(conn->recv_buffer);
        free(conn->send_buffer);
        closesock(sock);
        free(conn);
        return NULL;
    }
    
    pool->total_created++;
    g_net_stats.connections_created++;
    
    LOG_DEBUG("Created pooled connection to %s:%d", pool->target_host, pool->target_port);
    
    return conn;
}

static void destroy_connection(NetConnection *conn) {
    if (!conn) return;
    
    if (conn->socket != INVALID_SOCK) {
        closesock(conn->socket);
    }
    
    free(conn->recv_buffer);
    free(conn->send_buffer);
    
#ifdef _WIN32
    if (conn->read_ctx) free(conn->read_ctx);
    if (conn->write_ctx) free(conn->write_ctx);
#endif
    
    free(conn);
    g_net_stats.connections_destroyed++;
}

ConnectionPool *conn_pool_create(const char *host, uint16_t port, 
                            size_t min_conns, size_t max_conns) {
    if (!host || port == 0) return NULL;
    
    if (min_conns > max_conns) min_conns = max_conns;
    if (max_conns > NET_POOL_MAX_SIZE) max_conns = NET_POOL_MAX_SIZE;
    if (min_conns < 1) min_conns = 1;
    
    ConnectionPool *pool = calloc(1, sizeof(ConnectionPool));
    if (!pool) return NULL;
    
    strncpy(pool->target_host, host, sizeof(pool->target_host) - 1);
    pool->target_port = port;
    pool->min_connections = min_conns;
    pool->max_connections = max_conns;
    pool->health_check_interval_ms = 30000;  // 30 seconds
    
#ifdef _WIN32
    InitializeCriticalSection(&pool->lock);
#else
    pthread_mutex_init(&pool->lock, NULL);
#endif
    
    // Allocate connection array
    pool->connections = calloc(max_conns, sizeof(NetConnection*));
    if (!pool->connections) {
        free(pool);
        return NULL;
    }
    pool->capacity = max_conns;
    
    // Pre-create minimum connections
    for (size_t i = 0; i < min_conns; i++) {
        NetConnection *conn = create_pooled_connection(pool);
        if (conn) {
            conn->pool_index = (int)pool->count;
            pool->connections[pool->count++] = conn;
            
            // Add to free list
            conn->next = pool->free_list;
            if (pool->free_list) pool->free_list->prev = conn;
            pool->free_list = conn;
            pool->free_count++;
        }
    }
    
    pool->initialized = true;
    
    LOG_INFO("Connection pool created for %s:%d (min=%zu, max=%zu, pre-created=%zu)",
             host, port, min_conns, max_conns, pool->count);
    
    return pool;
}

void conn_pool_destroy(ConnectionPool *pool) {
    if (!pool) return;
    
    pool_lock(pool);
    
    // Destroy all connections
    for (size_t i = 0; i < pool->count; i++) {
        destroy_connection(pool->connections[i]);
    }
    
    free(pool->connections);
    pool->initialized = false;
    
    pool_unlock(pool);
    
#ifdef _WIN32
    DeleteCriticalSection(&pool->lock);
#else
    pthread_mutex_destroy(&pool->lock);
#endif
    
    LOG_INFO("Connection pool destroyed (created=%lu, destroyed=%lu)",
             (unsigned long)pool->total_created, (unsigned long)pool->total_destroyed);
    
    free(pool);
}

NetConnection *conn_pool_acquire(ConnectionPool *pool, int timeout_ms) {
    if (!pool || !pool->initialized) return NULL;
    
    pool_lock(pool);
    
    // Try to get from free list
    if (pool->free_list) {
        NetConnection *conn = pool->free_list;
        pool->free_list = conn->next;
        if (pool->free_list) pool->free_list->prev = NULL;
        conn->next = conn->prev = NULL;
        pool->free_count--;
        
        conn->last_activity = net_timestamp_ms();
        pool->total_acquired++;
        g_net_stats.pool_hits++;
        
        // Track peak usage
        size_t active = pool->count - pool->free_count;
        if (active > pool->peak_usage) pool->peak_usage = active;
        
        pool_unlock(pool);
        
        LOG_DEBUG("Acquired connection from pool (free=%zu)", pool->free_count);
        return conn;
    }
    
    // No free connections - try to create new one
    if (pool->count < pool->max_connections) {
        pool_unlock(pool);
        
        NetConnection *conn = create_pooled_connection(pool);
        if (conn) {
            pool_lock(pool);
            conn->pool_index = (int)pool->count;
            pool->connections[pool->count++] = conn;
            pool->total_acquired++;
            g_net_stats.pool_misses++;
            
            size_t active = pool->count - pool->free_count;
            if (active > pool->peak_usage) pool->peak_usage = active;
            
            pool_unlock(pool);
            
            LOG_DEBUG("Created new pool connection (total=%zu)", pool->count);
            return conn;
        }
        
        return NULL;
    }
    
    pool_unlock(pool);
    
    // Pool exhausted - could implement wait with timeout here
    LOG_WARN("Connection pool exhausted (max=%zu)", pool->max_connections);
    (void)timeout_ms;  // TODO: Implement wait
    
    return NULL;
}

void conn_pool_release(ConnectionPool *pool, NetConnection *conn) {
    if (!pool || !conn) return;
    
    pool_lock(pool);
    
    // Validate connection is still healthy
    if (conn->state != CONN_STATE_CONNECTED && conn->state != CONN_STATE_AUTHENTICATED) {
        // Connection is broken - remove from pool
        for (size_t i = 0; i < pool->count; i++) {
            if (pool->connections[i] == conn) {
                // Shift remaining connections
                for (size_t j = i; j < pool->count - 1; j++) {
                    pool->connections[j] = pool->connections[j + 1];
                    pool->connections[j]->pool_index = (int)j;
                }
                pool->count--;
                break;
            }
        }
        
        pool->total_destroyed++;
        pool_unlock(pool);
        
        destroy_connection(conn);
        LOG_DEBUG("Removed broken connection from pool");
        return;
    }
    
    // Return to free list
    conn->next = pool->free_list;
    conn->prev = NULL;
    if (pool->free_list) pool->free_list->prev = conn;
    pool->free_list = conn;
    pool->free_count++;
    conn->last_activity = net_timestamp_ms();
    
    pool->total_released++;
    
    pool_unlock(pool);
    
    LOG_DEBUG("Released connection to pool (free=%zu)", pool->free_count);
}

int conn_pool_health_check(ConnectionPool *pool) {
    if (!pool || !pool->initialized) return -1;
    
    pool_lock(pool);
    
    uint64_t now = net_timestamp_ms();
    int removed = 0;
    
    // Check connections in free list for staleness
    NetConnection *conn = pool->free_list;
    while (conn) {
        NetConnection *next = conn->next;
        
        // Check if connection is stale (no activity for too long)
        if (now - conn->last_activity > NET_KEEPALIVE_INTERVAL * 3) {
            // Try to send ping to verify
            if (net_send_ping(conn->socket) < 0) {
                // Connection is dead - remove it
                if (conn->prev) conn->prev->next = conn->next;
                if (conn->next) conn->next->prev = conn->prev;
                if (conn == pool->free_list) pool->free_list = conn->next;
                pool->free_count--;
                
                // Remove from connections array
                for (size_t i = 0; i < pool->count; i++) {
                    if (pool->connections[i] == conn) {
                        for (size_t j = i; j < pool->count - 1; j++) {
                            pool->connections[j] = pool->connections[j + 1];
                            pool->connections[j]->pool_index = (int)j;
                        }
                        pool->count--;
                        break;
                    }
                }
                
                destroy_connection(conn);
                pool->total_destroyed++;
                removed++;
            }
        }
        
        conn = next;
    }
    
    // Ensure minimum connections
    while (pool->count < pool->min_connections) {
        pool_unlock(pool);
        NetConnection *new_conn = create_pooled_connection(pool);
        pool_lock(pool);
        
        if (new_conn) {
            new_conn->pool_index = (int)pool->count;
            pool->connections[pool->count++] = new_conn;
            new_conn->next = pool->free_list;
            if (pool->free_list) pool->free_list->prev = new_conn;
            pool->free_list = new_conn;
            pool->free_count++;
        } else {
            break;
        }
    }
    
    pool->last_health_check = now;
    
    pool_unlock(pool);
    
    if (removed > 0) {
        LOG_DEBUG("Health check removed %d stale connections", removed);
    }
    
    return removed;
}

void conn_pool_get_stats(ConnectionPool *pool, uint64_t *acquired, uint64_t *released,
                    uint64_t *active, uint64_t *peak) {
    if (!pool) return;
    
    pool_lock(pool);
    if (acquired) *acquired = pool->total_acquired;
    if (released) *released = pool->total_released;
    if (active) *active = pool->count - pool->free_count;
    if (peak) *peak = pool->peak_usage;
    pool_unlock(pool);
}

// ============================================================================
// ASYNC SERVER - IOCP (Windows)
// ============================================================================

#ifdef _WIN32

static DWORD WINAPI iocp_worker_thread(LPVOID param);
static void handle_accept_completion(AsyncServer *server, IOContext *ctx, DWORD bytes);
static void handle_read_completion(AsyncServer *server, IOContext *ctx, DWORD bytes);
static void handle_write_completion(AsyncServer *server, IOContext *ctx, DWORD bytes);
static int post_accept(AsyncServer *server, IOContext *ctx);
static int post_read(NetConnection *conn);

AsyncServer *async_server_create(uint16_t port, size_t max_connections) {
    AsyncServer *server = calloc(1, sizeof(AsyncServer));
    if (!server) return NULL;
    
    server->port = port;
    server->max_connections = max_connections;
    server->recv_buffer_size = NET_RECV_BUFFER_SIZE;
    server->send_buffer_size = NET_SEND_BUFFER_SIZE;
    
    // Create IOCP handle
    server->iocp_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, NET_IOCP_CONCURRENT);
    if (server->iocp_handle == NULL) {
        LOG_ERROR("Failed to create IOCP handle: %d", GetLastError());
        free(server);
        return NULL;
    }
    
    // Create listen socket
    server->listen_socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, 
                                        NULL, 0, WSA_FLAG_OVERLAPPED);
    if (server->listen_socket == INVALID_SOCKET) {
        LOG_ERROR("Failed to create listen socket: %d", WSAGetLastError());
        CloseHandle(server->iocp_handle);
        free(server);
        return NULL;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server->listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    
    // Bind
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server->listen_socket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("Failed to bind socket: %d", WSAGetLastError());
        closesocket(server->listen_socket);
        CloseHandle(server->iocp_handle);
        free(server);
        return NULL;
    }
    
    // Associate with IOCP
    if (CreateIoCompletionPort((HANDLE)server->listen_socket, server->iocp_handle, 
                                (ULONG_PTR)server, 0) == NULL) {
        LOG_ERROR("Failed to associate listen socket with IOCP: %d", GetLastError());
        closesocket(server->listen_socket);
        CloseHandle(server->iocp_handle);
        free(server);
        return NULL;
    }
    
    // Get AcceptEx function pointer
    GUID guid_acceptex = WSAID_ACCEPTEX;
    DWORD bytes;
    if (WSAIoctl(server->listen_socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &guid_acceptex, sizeof(guid_acceptex),
                 &server->fn_accept_ex, sizeof(server->fn_accept_ex),
                 &bytes, NULL, NULL) == SOCKET_ERROR) {
        LOG_ERROR("Failed to get AcceptEx pointer: %d", WSAGetLastError());
        closesocket(server->listen_socket);
        CloseHandle(server->iocp_handle);
        free(server);
        return NULL;
    }
    
    // Get GetAcceptExSockaddrs function pointer
    GUID guid_getaddrs = WSAID_GETACCEPTEXSOCKADDRS;
    if (WSAIoctl(server->listen_socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &guid_getaddrs, sizeof(guid_getaddrs),
                 &server->fn_get_accept_ex_sockaddrs, sizeof(server->fn_get_accept_ex_sockaddrs),
                 &bytes, NULL, NULL) == SOCKET_ERROR) {
        LOG_ERROR("Failed to get GetAcceptExSockaddrs pointer: %d", WSAGetLastError());
        closesocket(server->listen_socket);
        CloseHandle(server->iocp_handle);
        free(server);
        return NULL;
    }
    
    // Allocate connection array
    server->connections = calloc(max_connections, sizeof(NetConnection*));
    if (!server->connections) {
        closesocket(server->listen_socket);
        CloseHandle(server->iocp_handle);
        free(server);
        return NULL;
    }
    server->connection_capacity = max_connections;
    
    // Pre-allocate accept contexts
    size_t num_accepts = max_connections < 64 ? max_connections : 64;
    server->accept_contexts = calloc(num_accepts, sizeof(IOContext));
    server->accept_context_count = num_accepts;
    
    for (size_t i = 0; i < num_accepts; i++) {
        IOContext *ctx = &server->accept_contexts[i];
        ctx->op_type = IO_OP_ACCEPT;
        ctx->buffer_size = (sizeof(struct sockaddr_in) + 16) * 2;
        ctx->buffer = malloc(ctx->buffer_size);
    }
    
    LOG_INFO("Async server created on port %d (IOCP, max_conn=%zu)", port, max_connections);
    
    return server;
}

int async_server_start(AsyncServer *server, size_t worker_threads) {
    if (!server) return -1;
    
    // Listen
    if (listen(server->listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        LOG_ERROR("Failed to listen: %d", WSAGetLastError());
        return -1;
    }
    
    server->running = true;
    
    // Create worker threads
    if (worker_threads == 0) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        worker_threads = si.dwNumberOfProcessors * 2;
    }
    
    server->worker_threads = calloc(worker_threads, sizeof(HANDLE));
    server->worker_count = worker_threads;
    
    for (size_t i = 0; i < worker_threads; i++) {
        server->worker_threads[i] = CreateThread(NULL, 0, iocp_worker_thread, server, 0, NULL);
        if (!server->worker_threads[i]) {
            LOG_ERROR("Failed to create worker thread %zu", i);
        }
    }
    
    // Post initial accepts
    for (size_t i = 0; i < server->accept_context_count; i++) {
        post_accept(server, &server->accept_contexts[i]);
    }
    
    LOG_INFO("Async server started with %zu worker threads", worker_threads);
    
    return 0;
}

void async_server_stop(AsyncServer *server) {
    if (!server) return;
    
    server->running = false;
    
    // Post completion notifications to wake up workers
    for (size_t i = 0; i < server->worker_count; i++) {
        PostQueuedCompletionStatus(server->iocp_handle, 0, 0, NULL);
    }
    
    // Wait for workers to finish
    if (server->worker_threads) {
        WaitForMultipleObjects((DWORD)server->worker_count, 
                               (HANDLE*)server->worker_threads, TRUE, 5000);
        
        for (size_t i = 0; i < server->worker_count; i++) {
            CloseHandle(server->worker_threads[i]);
        }
        free(server->worker_threads);
    }
    
    // Close connections
    for (size_t i = 0; i < server->connection_count; i++) {
        if (server->connections[i]) {
            closesocket(server->connections[i]->socket);
            destroy_connection(server->connections[i]);
        }
    }
    
    LOG_INFO("Async server stopped");
}

void async_server_destroy(AsyncServer *server) {
    if (!server) return;
    
    async_server_stop(server);
    
    closesocket(server->listen_socket);
    CloseHandle(server->iocp_handle);
    
    // Free accept contexts
    for (size_t i = 0; i < server->accept_context_count; i++) {
        free(server->accept_contexts[i].buffer);
    }
    free(server->accept_contexts);
    
    free(server->connections);
    free(server);
    
    LOG_INFO("Async server destroyed");
}

static int post_accept(AsyncServer *server, IOContext *ctx) {
    // Create accept socket
    SOCKET accept_sock = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, 
                                     NULL, 0, WSA_FLAG_OVERLAPPED);
    if (accept_sock == INVALID_SOCKET) {
        return -1;
    }
    
    memset(&ctx->overlapped, 0, sizeof(OVERLAPPED));
    ctx->op_type = IO_OP_ACCEPT;
    
    // Store accept socket in user_data temporarily
    ctx->user_data = (void*)(uintptr_t)accept_sock;
    
    DWORD bytes_received;
    BOOL result = server->fn_accept_ex(
        server->listen_socket,
        accept_sock,
        ctx->buffer,
        0,  // No data receive
        sizeof(struct sockaddr_in) + 16,
        sizeof(struct sockaddr_in) + 16,
        &bytes_received,
        &ctx->overlapped
    );
    
    if (!result && WSAGetLastError() != ERROR_IO_PENDING) {
        closesocket(accept_sock);
        return -1;
    }
    
    return 0;
}

static int post_read(NetConnection *conn) {
    if (!conn || !conn->read_ctx) return -1;
    
    IOContext *ctx = conn->read_ctx;
    memset(&ctx->overlapped, 0, sizeof(OVERLAPPED));
    ctx->op_type = IO_OP_READ;
    
    ctx->wsa_buf.buf = conn->recv_buffer + conn->recv_buffer_used;
    ctx->wsa_buf.len = (ULONG)(conn->recv_buffer_size - conn->recv_buffer_used);
    
    DWORD flags = 0;
    DWORD bytes;
    
    int result = WSARecv(conn->socket, &ctx->wsa_buf, 1, &bytes, &flags, 
                         &ctx->overlapped, NULL);
    
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        return -1;
    }
    
    return 0;
}

static DWORD WINAPI iocp_worker_thread(LPVOID param) {
    AsyncServer *server = (AsyncServer*)param;
    
    while (server->running) {
        DWORD bytes_transferred;
        ULONG_PTR completion_key;
        OVERLAPPED *overlapped;
        
        BOOL result = GetQueuedCompletionStatus(
            server->iocp_handle,
            &bytes_transferred,
            &completion_key,
            &overlapped,
            1000  // 1 second timeout
        );
        
        if (!result) {
            DWORD error = GetLastError();
            if (error == WAIT_TIMEOUT) continue;
            if (overlapped == NULL) continue;
            
            // Handle connection error
            IOContext *ctx = CONTAINING_RECORD(overlapped, IOContext, overlapped);
            if (ctx->connection) {
                NetConnection *conn = (NetConnection*)ctx->connection;
                conn->state = CONN_STATE_ERROR;
                
                if (server->on_disconnect) {
                    server->on_disconnect(conn);
                }
            }
            continue;
        }
        
        if (overlapped == NULL) {
            // Shutdown notification
            break;
        }
        
        IOContext *ctx = CONTAINING_RECORD(overlapped, IOContext, overlapped);
        
        switch (ctx->op_type) {
            case IO_OP_ACCEPT:
                handle_accept_completion(server, ctx, bytes_transferred);
                break;
            case IO_OP_READ:
                handle_read_completion(server, ctx, bytes_transferred);
                break;
            case IO_OP_WRITE:
                handle_write_completion(server, ctx, bytes_transferred);
                break;
            default:
                break;
        }
    }
    
    return 0;
}

static void handle_accept_completion(AsyncServer *server, IOContext *ctx, DWORD bytes) {
    (void)bytes;
    
    SOCKET accept_sock = (SOCKET)(uintptr_t)ctx->user_data;
    
    // Associate with IOCP
    if (CreateIoCompletionPort((HANDLE)accept_sock, server->iocp_handle, 
                                (ULONG_PTR)accept_sock, 0) == NULL) {
        closesocket(accept_sock);
        post_accept(server, ctx);
        return;
    }
    
    // Create connection
    NetConnection *conn = calloc(1, sizeof(NetConnection));
    if (!conn) {
        closesocket(accept_sock);
        post_accept(server, ctx);
        return;
    }
    
    conn->socket = accept_sock;
    conn->state = CONN_STATE_CONNECTED;
    conn->last_activity = net_timestamp_ms();
    
    // Allocate buffers
    conn->recv_buffer_size = server->recv_buffer_size;
    conn->recv_buffer = malloc(conn->recv_buffer_size);
    conn->send_buffer_size = server->send_buffer_size;
    conn->send_buffer = malloc(conn->send_buffer_size);
    
    // Allocate I/O contexts
    conn->read_ctx = calloc(1, sizeof(IOContext));
    conn->write_ctx = calloc(1, sizeof(IOContext));
    conn->read_ctx->connection = conn;
    conn->write_ctx->connection = conn;
    
    // Get remote address
    struct sockaddr_in *local_addr, *remote_addr;
    int local_len, remote_len;
    server->fn_get_accept_ex_sockaddrs(
        ctx->buffer, 0,
        sizeof(struct sockaddr_in) + 16,
        sizeof(struct sockaddr_in) + 16,
        (struct sockaddr**)&local_addr, &local_len,
        (struct sockaddr**)&remote_addr, &remote_len
    );
    
    inet_ntop(AF_INET, &remote_addr->sin_addr, conn->remote_ip, sizeof(conn->remote_ip));
    conn->remote_port = ntohs(remote_addr->sin_port);
    
    // Add to connections
    if (server->connection_count < server->connection_capacity) {
        server->connections[server->connection_count++] = conn;
        server->active_connections++;
        server->total_connections++;
    }
    
    // Notify
    if (server->on_connect) {
        server->on_connect(conn);
    }
    
    // Post read
    post_read(conn);
    
    // Post another accept
    post_accept(server, ctx);
    
    LOG_DEBUG("Accepted connection from %s:%d", conn->remote_ip, conn->remote_port);
}

static void handle_read_completion(AsyncServer *server, IOContext *ctx, DWORD bytes) {
    NetConnection *conn = (NetConnection*)ctx->connection;
    if (!conn) return;
    
    if (bytes == 0) {
        // Connection closed
        conn->state = CONN_STATE_DISCONNECTED;
        if (server->on_disconnect) {
            server->on_disconnect(conn);
        }
        return;
    }
    
    conn->recv_buffer_used += bytes;
    conn->bytes_received += bytes;
    conn->last_activity = net_timestamp_ms();
    
    // Try to parse complete messages
    while (conn->recv_buffer_used >= NET_HEADER_SIZE) {
        NetMessageHeader header;
        if (net_msg_parse_header(conn->recv_buffer, conn->recv_buffer_used, &header) < 0) {
            // Invalid header - close connection
            conn->state = CONN_STATE_ERROR;
            if (server->on_error) {
                server->on_error(conn, -1, "Invalid message header");
            }
            return;
        }
        
        size_t total_msg_size = NET_HEADER_SIZE + header.payload_len;
        
        if (conn->recv_buffer_used < total_msg_size) {
            // Need more data
            break;
        }
        
        // Complete message received
        const char *payload = conn->recv_buffer + NET_HEADER_SIZE;
        
        if (server->on_message) {
            server->on_message(conn, &header, payload, header.payload_len);
        }
        
        conn->messages_received++;
        
        // Shift buffer
        size_t remaining = conn->recv_buffer_used - total_msg_size;
        if (remaining > 0) {
            memmove(conn->recv_buffer, conn->recv_buffer + total_msg_size, remaining);
        }
        conn->recv_buffer_used = remaining;
    }
    
    // Post another read
    post_read(conn);
}

static void handle_write_completion(AsyncServer *server, IOContext *ctx, DWORD bytes) {
    (void)server;
    
    NetConnection *conn = (NetConnection*)ctx->connection;
    if (!conn) return;
    
    conn->bytes_sent += bytes;
    conn->messages_sent++;
    conn->last_activity = net_timestamp_ms();
}

int async_server_send(AsyncServer *server, NetConnection *conn,
                      const NetMessageHeader *header, const char *payload) {
    if (!server || !conn || !header) return -1;
    
    size_t total_size = NET_HEADER_SIZE + header->payload_len;
    
    // Build complete message
    NetMessageHeader msg_header = *header;
    msg_header.magic = NET_MAGIC;
    msg_header.version = NET_PROTOCOL_VERSION;
    
    IOContext *ctx = conn->write_ctx;
    memset(&ctx->overlapped, 0, sizeof(OVERLAPPED));
    ctx->op_type = IO_OP_WRITE;
    
    // Copy to send buffer
    if (total_size > conn->send_buffer_size) {
        return -1;  // Message too large
    }
    
    memcpy(conn->send_buffer, &msg_header, NET_HEADER_SIZE);
    if (payload && header->payload_len > 0) {
        memcpy(conn->send_buffer + NET_HEADER_SIZE, payload, header->payload_len);
    }
    
    ctx->wsa_buf.buf = conn->send_buffer;
    ctx->wsa_buf.len = (ULONG)total_size;
    
    DWORD bytes;
    int result = WSASend(conn->socket, &ctx->wsa_buf, 1, &bytes, 0, 
                         &ctx->overlapped, NULL);
    
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        return -1;
    }
    
    return 0;
}

#else  // Linux implementation

// Linux uses epoll by default, io_uring if available
// Simplified implementation using epoll for now

AsyncServer *async_server_create(uint16_t port, size_t max_connections) {
    AsyncServer *server = calloc(1, sizeof(AsyncServer));
    if (!server) return NULL;
    
    server->port = port;
    server->max_connections = max_connections;
    
    // Create listen socket
    server->listen_socket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server->listen_socket < 0) {
        free(server);
        return NULL;
    }
    
    int opt = 1;
    setsockopt(server->listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server->listen_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server->listen_socket);
        free(server);
        return NULL;
    }
    
    // Create epoll
    server->epoll_fd = epoll_create1(0);
    if (server->epoll_fd < 0) {
        close(server->listen_socket);
        free(server);
        return NULL;
    }
    
    server->connections = calloc(max_connections, sizeof(NetConnection*));
    server->connection_capacity = max_connections;
    
    return server;
}

int async_server_start(AsyncServer *server, size_t worker_threads) {
    if (!server) return -1;
    
    listen(server->listen_socket, SOMAXCONN);
    server->running = true;
    
    // Add listen socket to epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server->listen_socket;
    epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, server->listen_socket, &ev);
    
    // TODO: Create worker threads for epoll_wait
    (void)worker_threads;
    
    return 0;
}

void async_server_stop(AsyncServer *server) {
    if (server) server->running = false;
}

void async_server_destroy(AsyncServer *server) {
    if (!server) return;
    
    close(server->listen_socket);
    close(server->epoll_fd);
    free(server->connections);
    free(server);
}

int async_server_send(AsyncServer *server, NetConnection *conn,
                      const NetMessageHeader *header, const char *payload) {
    (void)server;
    return net_send_message(conn->socket, header, payload, header->payload_len);
}

#endif

// ============================================================================
// CALLBACK SETTERS
// ============================================================================

void async_server_set_message_handler(AsyncServer *server, NetMessageHandler handler) {
    if (server) server->on_message = handler;
}

void async_server_set_connect_handler(AsyncServer *server, NetConnectHandler handler) {
    if (server) server->on_connect = handler;
}

void async_server_set_disconnect_handler(AsyncServer *server, NetDisconnectHandler handler) {
    if (server) server->on_disconnect = handler;
}

void async_server_set_error_handler(AsyncServer *server, NetErrorHandler handler) {
    if (server) server->on_error = handler;
}

// ============================================================================
// ASYNC CLIENT
// ============================================================================

NetConnection *async_connect(const char *host, uint16_t port, int timeout_ms) {
    socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCK) {
        return NULL;
    }
    
    // Set options
    int opt = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));
    
#ifdef _WIN32
    DWORD timeout = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    
    struct sockaddr_in server = {0};
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    inet_pton(AF_INET, host, &server.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        closesock(sock);
        return NULL;
    }
    
    NetConnection *conn = calloc(1, sizeof(NetConnection));
    if (!conn) {
        closesock(sock);
        return NULL;
    }
    
    conn->socket = sock;
    conn->state = CONN_STATE_CONNECTED;
    strncpy(conn->remote_ip, host, sizeof(conn->remote_ip) - 1);
    conn->remote_port = port;
    conn->last_activity = net_timestamp_ms();
    
    conn->recv_buffer_size = NET_RECV_BUFFER_SIZE;
    conn->recv_buffer = malloc(conn->recv_buffer_size);
    conn->send_buffer_size = NET_SEND_BUFFER_SIZE;
    conn->send_buffer = malloc(conn->send_buffer_size);
    
    g_net_stats.connections_created++;
    
    return conn;
}

void async_disconnect(NetConnection *conn) {
    if (!conn) return;
    
    if (conn->socket != INVALID_SOCK) {
        // Send close message
        NetMessageHeader header = {0};
        header.type = MSG_TYPE_CLOSE;
        net_send_message(conn->socket, &header, NULL, 0);
        
        closesock(conn->socket);
    }
    
    destroy_connection(conn);
}

int async_send(NetConnection *conn, const NetMessageHeader *header, const char *payload) {
    if (!conn || !header) return -1;
    return net_send_message(conn->socket, header, payload, header->payload_len);
}

int async_recv(NetConnection *conn, NetMessageHeader *out_header, 
               char **out_payload, size_t *out_len, int timeout_ms) {
    if (!conn || !out_header) return -1;
    return net_recv_message(conn->socket, out_header, out_payload, out_len, timeout_ms);
}

// ============================================================================
// DEBUG & STATS
// ============================================================================

void net_debug_header(const NetMessageHeader *header) {
    if (!header) return;
    
    printf("NetMessageHeader {\n");
    printf("  magic: 0x%08X %s\n", header->magic, 
           header->magic == NET_MAGIC ? "(valid)" : "(INVALID!)");
    printf("  version: %d.%d\n", header->version >> 8, header->version & 0xFF);
    printf("  type: 0x%02X\n", header->type);
    printf("  flags: 0x%02X\n", header->flags);
    printf("  sequence: %u\n", header->sequence);
    printf("  payload_len: %u\n", header->payload_len);
    printf("}\n");
}

void net_stats_print(void) {
    printf("\n========== Network Statistics ==========\n");
    printf("  Messages Sent:     %llu\n", (unsigned long long)g_net_stats.messages_sent);
    printf("  Messages Received: %llu\n", (unsigned long long)g_net_stats.messages_received);
    printf("  Bytes Sent:        %llu\n", (unsigned long long)g_net_stats.bytes_sent);
    printf("  Bytes Received:    %llu\n", (unsigned long long)g_net_stats.bytes_received);
    printf("  Connections:       %llu created, %llu destroyed\n", 
           (unsigned long long)g_net_stats.connections_created,
           (unsigned long long)g_net_stats.connections_destroyed);
    printf("  Pool:              %llu hits, %llu misses\n",
           (unsigned long long)g_net_stats.pool_hits,
           (unsigned long long)g_net_stats.pool_misses);
    printf("=========================================\n\n");
}
