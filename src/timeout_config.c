/**
 * InventixDB Network Timeout Configuration Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

#include "timeout_config.h"
#include "config.h"

// ============================================================================
// GLOBAL INSTANCES
// ============================================================================

TimeoutConfig g_timeout_config;
TimeoutStats g_timeout_stats;

// Thread safety
#ifdef _WIN32
static CRITICAL_SECTION g_timeout_lock;
static int g_lock_initialized = 0;

static void init_lock(void) {
    if (!g_lock_initialized) {
        InitializeCriticalSection(&g_timeout_lock);
        g_lock_initialized = 1;
    }
}

#define LOCK()   EnterCriticalSection(&g_timeout_lock)
#define UNLOCK() LeaveCriticalSection(&g_timeout_lock)
#else
#include <pthread.h>
static pthread_mutex_t g_timeout_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK()   pthread_mutex_lock(&g_timeout_lock)
#define UNLOCK() pthread_mutex_unlock(&g_timeout_lock)
static void init_lock(void) {}
#endif

// ============================================================================
// INITIALIZATION
// ============================================================================

void timeout_config_init(TimeoutConfig *config) {
    if (!config) return;
    
    init_lock();
    
    // Connection Timeouts
    config->connect_timeout_ms = DEFAULT_CONNECT_TIMEOUT_MS;
    config->read_timeout_ms = DEFAULT_READ_TIMEOUT_MS;
    config->write_timeout_ms = DEFAULT_WRITE_TIMEOUT_MS;
    config->idle_timeout_ms = DEFAULT_IDLE_TIMEOUT_MS;
    
    // Keep-Alive
    config->keepalive_interval_ms = DEFAULT_KEEPALIVE_INTERVAL_MS;
    config->keepalive_count = 3;
    config->enable_keepalive = true;
    
    // Cluster
    config->heartbeat_timeout_ms = DEFAULT_HEARTBEAT_TIMEOUT_MS;
    config->cluster_sync_timeout_ms = DEFAULT_CLUSTER_TIMEOUT_MS;
    config->election_timeout_ms = 1000;  // 1 second
    config->replication_timeout_ms = 5000;  // 5 seconds
    
    // Query
    config->query_timeout_ms = DEFAULT_QUERY_TIMEOUT_MS;
    config->txn_timeout_ms = DEFAULT_TXN_TIMEOUT_MS;
    config->lock_wait_timeout_ms = 10000;  // 10 seconds
    
    // Retry
    config->retry_count = 3;
    config->retry_backoff_ms = 100;
    config->retry_max_backoff_ms = 5000;
    config->exponential_backoff = true;
    
    // Initialize global config
    memcpy(&g_timeout_config, config, sizeof(TimeoutConfig));
    memset(&g_timeout_stats, 0, sizeof(TimeoutStats));
}

// ============================================================================
// VALIDATION
// ============================================================================

static uint32_t clamp_timeout(uint32_t value) {
    if (value < MIN_TIMEOUT_MS) return MIN_TIMEOUT_MS;
    if (value > MAX_TIMEOUT_MS) return MAX_TIMEOUT_MS;
    return value;
}

bool timeout_config_validate(const TimeoutConfig *config) {
    if (!config) return false;
    
    // All timeouts must be within bounds
    if (config->connect_timeout_ms < MIN_TIMEOUT_MS || 
        config->connect_timeout_ms > MAX_TIMEOUT_MS) return false;
    if (config->read_timeout_ms < MIN_TIMEOUT_MS || 
        config->read_timeout_ms > MAX_TIMEOUT_MS) return false;
    if (config->write_timeout_ms < MIN_TIMEOUT_MS || 
        config->write_timeout_ms > MAX_TIMEOUT_MS) return false;
    if (config->query_timeout_ms < MIN_TIMEOUT_MS || 
        config->query_timeout_ms > MAX_TIMEOUT_MS) return false;
        
    return true;
}

// ============================================================================
// CONFIGURATION LOADING
// ============================================================================

bool timeout_config_load(TimeoutConfig *config, const char *section) {
    if (!config) return false;
    
    // Initialize with defaults first
    timeout_config_init(config);
    
    // Try to load from global config if available
    if (section && strcmp(section, "network") == 0) {
        config->connect_timeout_ms = g_config.network.connection_timeout_ms > 0 
            ? g_config.network.connection_timeout_ms 
            : DEFAULT_CONNECT_TIMEOUT_MS;
    } else if (section && strcmp(section, "cluster") == 0) {
        config->heartbeat_timeout_ms = g_config.cluster.heartbeat_interval_ms > 0
            ? g_config.cluster.heartbeat_interval_ms
            : DEFAULT_HEARTBEAT_TIMEOUT_MS;
    }
    
    return true;
}

void timeout_config_apply(const TimeoutConfig *config) {
    if (!config) return;
    
    LOCK();
    memcpy(&g_timeout_config, config, sizeof(TimeoutConfig));
    UNLOCK();
}

// ============================================================================
// TIMEOUT ACCESS
// ============================================================================

uint32_t timeout_get(TimeoutType type) {
    uint32_t result;
    
    LOCK();
    switch (type) {
        case TIMEOUT_CONNECT:   result = g_timeout_config.connect_timeout_ms; break;
        case TIMEOUT_READ:      result = g_timeout_config.read_timeout_ms; break;
        case TIMEOUT_WRITE:     result = g_timeout_config.write_timeout_ms; break;
        case TIMEOUT_IDLE:      result = g_timeout_config.idle_timeout_ms; break;
        case TIMEOUT_KEEPALIVE: result = g_timeout_config.keepalive_interval_ms; break;
        case TIMEOUT_HEARTBEAT: result = g_timeout_config.heartbeat_timeout_ms; break;
        case TIMEOUT_CLUSTER:   result = g_timeout_config.cluster_sync_timeout_ms; break;
        case TIMEOUT_QUERY:     result = g_timeout_config.query_timeout_ms; break;
        case TIMEOUT_TXN:       result = g_timeout_config.txn_timeout_ms; break;
        case TIMEOUT_LOCK:      result = g_timeout_config.lock_wait_timeout_ms; break;
        default:                result = DEFAULT_CONNECT_TIMEOUT_MS; break;
    }
    UNLOCK();
    
    return result;
}

void timeout_set(TimeoutType type, uint32_t timeout_ms) {
    timeout_ms = clamp_timeout(timeout_ms);
    
    LOCK();
    switch (type) {
        case TIMEOUT_CONNECT:   g_timeout_config.connect_timeout_ms = timeout_ms; break;
        case TIMEOUT_READ:      g_timeout_config.read_timeout_ms = timeout_ms; break;
        case TIMEOUT_WRITE:     g_timeout_config.write_timeout_ms = timeout_ms; break;
        case TIMEOUT_IDLE:      g_timeout_config.idle_timeout_ms = timeout_ms; break;
        case TIMEOUT_KEEPALIVE: g_timeout_config.keepalive_interval_ms = timeout_ms; break;
        case TIMEOUT_HEARTBEAT: g_timeout_config.heartbeat_timeout_ms = timeout_ms; break;
        case TIMEOUT_CLUSTER:   g_timeout_config.cluster_sync_timeout_ms = timeout_ms; break;
        case TIMEOUT_QUERY:     g_timeout_config.query_timeout_ms = timeout_ms; break;
        case TIMEOUT_TXN:       g_timeout_config.txn_timeout_ms = timeout_ms; break;
        case TIMEOUT_LOCK:      g_timeout_config.lock_wait_timeout_ms = timeout_ms; break;
        default: break;
    }
    UNLOCK();
}

// ============================================================================
// STATISTICS
// ============================================================================

void timeout_record_event(TimeoutType type) {
    LOCK();
    switch (type) {
        case TIMEOUT_CONNECT:   g_timeout_stats.connect_timeouts++; break;
        case TIMEOUT_READ:      g_timeout_stats.read_timeouts++; break;
        case TIMEOUT_WRITE:     g_timeout_stats.write_timeouts++; break;
        case TIMEOUT_IDLE:      g_timeout_stats.idle_disconnects++; break;
        case TIMEOUT_QUERY:     g_timeout_stats.query_timeouts++; break;
        case TIMEOUT_TXN:       g_timeout_stats.txn_timeouts++; break;
        case TIMEOUT_LOCK:      g_timeout_stats.lock_timeouts++; break;
        case TIMEOUT_CLUSTER:
        case TIMEOUT_HEARTBEAT: g_timeout_stats.cluster_timeouts++; break;
        default: break;
    }
    UNLOCK();
}

void timeout_record_retry(bool successful) {
    LOCK();
    g_timeout_stats.total_retries++;
    if (successful) {
        g_timeout_stats.successful_retries++;
    }
    UNLOCK();
}

TimeoutStats timeout_get_stats(void) {
    TimeoutStats stats;
    LOCK();
    memcpy(&stats, &g_timeout_stats, sizeof(TimeoutStats));
    UNLOCK();
    return stats;
}

void timeout_reset_stats(void) {
    LOCK();
    memset(&g_timeout_stats, 0, sizeof(TimeoutStats));
    UNLOCK();
}

// ============================================================================
// BACKOFF CALCULATION
// ============================================================================

uint32_t timeout_calculate_backoff(int retry_attempt) {
    LOCK();
    uint32_t base = g_timeout_config.retry_backoff_ms;
    uint32_t max_backoff = g_timeout_config.retry_max_backoff_ms;
    bool exponential = g_timeout_config.exponential_backoff;
    UNLOCK();
    
    uint32_t backoff;
    
    if (exponential) {
        // Exponential backoff: base * 2^attempt
        backoff = base * (1 << retry_attempt);
    } else {
        // Linear backoff: base * (attempt + 1)
        backoff = base * (retry_attempt + 1);
    }
    
    // Add some jitter (10%)
    uint32_t jitter = (backoff * (rand() % 20)) / 100;
    backoff += jitter;
    
    // Cap at maximum
    if (backoff > max_backoff) {
        backoff = max_backoff;
    }
    
    return backoff;
}

// ============================================================================
// SOCKET TIMEOUT HELPERS
// ============================================================================

int socket_set_recv_timeout(socket_t sock, uint32_t timeout_ms) {
#ifdef _WIN32
    DWORD timeout = timeout_ms;
    return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, 
                      (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

int socket_set_send_timeout(socket_t sock, uint32_t timeout_ms) {
#ifdef _WIN32
    DWORD timeout = timeout_ms;
    return setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, 
                      (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

int socket_set_connect_timeout(socket_t sock, uint32_t timeout_ms) {
    // Set socket to non-blocking for connect timeout
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
}

int socket_set_keepalive(socket_t sock, bool enable, int idle_sec, 
                         int interval_sec, int count) {
    int optval = enable ? 1 : 0;
    
    if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, 
                   (const char*)&optval, sizeof(optval)) < 0) {
        return -1;
    }
    
    if (!enable) return 0;
    
#ifdef _WIN32
    // Windows uses tcp_keepalive structure
    struct tcp_keepalive ka;
    ka.onoff = 1;
    ka.keepalivetime = idle_sec * 1000;
    ka.keepaliveinterval = interval_sec * 1000;
    
    DWORD bytes_returned;
    return WSAIoctl(sock, SIO_KEEPALIVE_VALS, &ka, sizeof(ka),
                    NULL, 0, &bytes_returned, NULL, NULL);
#else
    // Linux-specific keepalive options
    #ifdef TCP_KEEPIDLE
    if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle_sec, sizeof(idle_sec)) < 0) {
        return -1;
    }
    #endif
    
    #ifdef TCP_KEEPINTVL
    if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &interval_sec, sizeof(interval_sec)) < 0) {
        return -1;
    }
    #endif
    
    #ifdef TCP_KEEPCNT
    if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count)) < 0) {
        return -1;
    }
    #endif
    
    return 0;
#endif
}
