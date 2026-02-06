/**
 * InventixDB Network Timeout Configuration
 * 
 * Provides configurable timeout settings for all network operations
 * with support for dynamic updates and monitoring.
 */

#ifndef INVENTIX_TIMEOUT_CONFIG_H
#define INVENTIX_TIMEOUT_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// DEFAULT TIMEOUT VALUES (milliseconds)
// ============================================================================

#define DEFAULT_CONNECT_TIMEOUT_MS       5000   // 5 seconds
#define DEFAULT_READ_TIMEOUT_MS          30000  // 30 seconds
#define DEFAULT_WRITE_TIMEOUT_MS         30000  // 30 seconds
#define DEFAULT_IDLE_TIMEOUT_MS          300000 // 5 minutes
#define DEFAULT_KEEPALIVE_INTERVAL_MS    60000  // 1 minute
#define DEFAULT_HEARTBEAT_TIMEOUT_MS     3000   // 3 seconds
#define DEFAULT_CLUSTER_TIMEOUT_MS       10000  // 10 seconds
#define DEFAULT_QUERY_TIMEOUT_MS         60000  // 1 minute
#define DEFAULT_TXN_TIMEOUT_MS           120000 // 2 minutes

// Minimum and maximum bounds
#define MIN_TIMEOUT_MS                   100    // 100ms minimum
#define MAX_TIMEOUT_MS                   3600000 // 1 hour maximum

// ============================================================================
// TIMEOUT CONFIGURATION STRUCTURE
// ============================================================================

typedef struct {
    // Connection Timeouts
    uint32_t connect_timeout_ms;      // Time to establish connection
    uint32_t read_timeout_ms;         // Time to wait for read data
    uint32_t write_timeout_ms;        // Time to wait for write completion
    uint32_t idle_timeout_ms;         // Time before closing idle connection
    
    // Keep-Alive Settings
    uint32_t keepalive_interval_ms;   // Interval between keepalive probes
    uint32_t keepalive_count;         // Number of probes before disconnect
    bool     enable_keepalive;        // Enable TCP keepalive
    
    // Cluster Timeouts
    uint32_t heartbeat_timeout_ms;    // Cluster heartbeat timeout
    uint32_t cluster_sync_timeout_ms; // Cluster synchronization timeout
    uint32_t election_timeout_ms;     // Raft election timeout
    uint32_t replication_timeout_ms;  // Replication ack timeout
    
    // Query Timeouts
    uint32_t query_timeout_ms;        // Max query execution time
    uint32_t txn_timeout_ms;          // Max transaction time
    uint32_t lock_wait_timeout_ms;    // Time to wait for lock acquisition
    
    // Retry Settings
    uint32_t retry_count;             // Number of retries on timeout
    uint32_t retry_backoff_ms;        // Initial backoff between retries
    uint32_t retry_max_backoff_ms;    // Maximum backoff
    bool     exponential_backoff;     // Use exponential backoff
} TimeoutConfig;

// ============================================================================
// TIMEOUT STATISTICS
// ============================================================================

typedef struct {
    uint64_t connect_timeouts;        // Number of connection timeouts
    uint64_t read_timeouts;           // Number of read timeouts
    uint64_t write_timeouts;          // Number of write timeouts
    uint64_t idle_disconnects;        // Number of idle disconnects
    uint64_t query_timeouts;          // Number of query timeouts
    uint64_t txn_timeouts;            // Number of transaction timeouts
    uint64_t lock_timeouts;           // Number of lock wait timeouts
    uint64_t cluster_timeouts;        // Number of cluster operation timeouts
    uint64_t total_retries;           // Total number of retries performed
    uint64_t successful_retries;      // Retries that succeeded
} TimeoutStats;

// ============================================================================
// GLOBAL CONFIGURATION
// ============================================================================

extern TimeoutConfig g_timeout_config;
extern TimeoutStats  g_timeout_stats;

// ============================================================================
// API FUNCTIONS
// ============================================================================

/**
 * Initialize timeout configuration with defaults
 */
void timeout_config_init(TimeoutConfig *config);

/**
 * Validate timeout configuration values
 * @return true if valid, false if any value is out of bounds
 */
bool timeout_config_validate(const TimeoutConfig *config);

/**
 * Load timeout configuration from config file section
 * @param config Config to populate
 * @param section Section name in config file (e.g., "network", "cluster")
 * @return true on success
 */
bool timeout_config_load(TimeoutConfig *config, const char *section);

/**
 * Apply timeout configuration (update global settings)
 */
void timeout_config_apply(const TimeoutConfig *config);

/**
 * Get current timeout for a specific operation type
 */
typedef enum {
    TIMEOUT_CONNECT,
    TIMEOUT_READ,
    TIMEOUT_WRITE,
    TIMEOUT_IDLE,
    TIMEOUT_KEEPALIVE,
    TIMEOUT_HEARTBEAT,
    TIMEOUT_CLUSTER,
    TIMEOUT_QUERY,
    TIMEOUT_TXN,
    TIMEOUT_LOCK
} TimeoutType;

uint32_t timeout_get(TimeoutType type);

/**
 * Set timeout for a specific operation type (thread-safe)
 */
void timeout_set(TimeoutType type, uint32_t timeout_ms);

/**
 * Record a timeout event for statistics
 */
void timeout_record_event(TimeoutType type);

/**
 * Record a retry attempt
 * @param successful true if retry succeeded
 */
void timeout_record_retry(bool successful);

/**
 * Get timeout statistics
 */
TimeoutStats timeout_get_stats(void);

/**
 * Reset timeout statistics
 */
void timeout_reset_stats(void);

/**
 * Calculate backoff time for retry
 * @param retry_attempt Current retry attempt (0-based)
 * @return Backoff time in milliseconds
 */
uint32_t timeout_calculate_backoff(int retry_attempt);

// ============================================================================
// PLATFORM-SPECIFIC SOCKET TIMEOUT HELPERS
// ============================================================================

#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET socket_t;
#else
#include <sys/socket.h>
typedef int socket_t;
#endif

/**
 * Set socket receive timeout
 * @param sock Socket handle
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on failure
 */
int socket_set_recv_timeout(socket_t sock, uint32_t timeout_ms);

/**
 * Set socket send timeout
 * @param sock Socket handle
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on failure
 */
int socket_set_send_timeout(socket_t sock, uint32_t timeout_ms);

/**
 * Set socket connect timeout (platform-specific implementation)
 * @param sock Socket handle
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on failure
 */
int socket_set_connect_timeout(socket_t sock, uint32_t timeout_ms);

/**
 * Set TCP keepalive options
 * @param sock Socket handle
 * @param enable Enable/disable keepalive
 * @param idle_sec Idle time before sending probes
 * @param interval_sec Interval between probes
 * @param count Number of probes before disconnect
 * @return 0 on success, -1 on failure
 */
int socket_set_keepalive(socket_t sock, bool enable, int idle_sec, 
                         int interval_sec, int count);

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

#define TIMEOUT_CONNECT_MS   timeout_get(TIMEOUT_CONNECT)
#define TIMEOUT_READ_MS      timeout_get(TIMEOUT_READ)
#define TIMEOUT_WRITE_MS     timeout_get(TIMEOUT_WRITE)
#define TIMEOUT_IDLE_MS      timeout_get(TIMEOUT_IDLE)
#define TIMEOUT_QUERY_MS     timeout_get(TIMEOUT_QUERY)
#define TIMEOUT_TXN_MS       timeout_get(TIMEOUT_TXN)

#endif // INVENTIX_TIMEOUT_CONFIG_H
