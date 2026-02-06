#ifndef INVENTIX_CONFIG_H
#define INVENTIX_CONFIG_H

#include <stdint.h>

// ============================================================================
// INVENTIX CONFIGURATION SYSTEM
// ============================================================================
// Centralized configuration management for InventixDB
// Loads settings from inventix.conf file or uses sensible defaults
// ============================================================================

#define MAX_CONFIG_WORKERS 16
#define MAX_PATH_LENGTH 256
#define MAX_CONFIG_LINE 512

// ----------------------------------------------------------------------------
// Worker Node Configuration
// ----------------------------------------------------------------------------
typedef struct {
    char ip[64];
    int port;
    int enabled;
    char name[32];
} WorkerConfig;

// ----------------------------------------------------------------------------
// Storage Configuration
// ----------------------------------------------------------------------------
typedef struct {
    int initial_capacity;           // Initial hash table capacity
    int checkpoint_threshold_kb;    // Auto-checkpoint threshold in KB
    char log_file[MAX_PATH_LENGTH]; // AOF log file path
    char snap_file[MAX_PATH_LENGTH];// Snapshot file path
    char data_dir[MAX_PATH_LENGTH]; // Data directory
    int skip_list_max_level;        // Max level for skip list indexes
    int enable_aof;                 // Enable Append-Only File logging
    int enable_snapshots;           // Enable periodic snapshots
    int sync_on_write;              // fsync after each write (durability vs speed)
} StorageConfig;

// ----------------------------------------------------------------------------
// Advanced Storage Engine Configuration
// ----------------------------------------------------------------------------
typedef struct {
    int engine_type;                // 0=ROW_STORE, 1=COLUMN_STORE, 2=LSM_TREE
    int compression_type;           // 0=NONE, 1=LZ4, 2=RLE
    int enable_slotted_pages;       // Use slotted pages for variable-length rows
    int lsm_memtable_size_kb;       // LSM MemTable size before flush (KB)
    int lsm_level_count;            // Number of LSM levels
    int lsm_compaction_threshold;   // SSTables per level before compaction
    int column_chunk_size;          // Rows per column chunk
    int enable_bloom_filter;        // Enable bloom filters for LSM
    int compression_threshold;      // Min bytes before compression kicks in
    int auto_compact;               // Enable automatic compaction
} StorageEngineConfig;

// ----------------------------------------------------------------------------
// Buffer Pool Configuration
// ----------------------------------------------------------------------------
typedef struct {
    int pool_size;                  // Number of pages in memory
    int page_size;                  // Page size in bytes
    int prefetch_enabled;           // Enable page prefetching
    int eviction_policy;            // 0=LRU, 1=CLOCK, 2=LFU
} BufferPoolConfig;

// ----------------------------------------------------------------------------
// Network Configuration
// ----------------------------------------------------------------------------
typedef struct {
    int server_port;                // Main server port
    int max_connections;            // Max concurrent connections
    int connection_timeout_ms;      // Connection timeout in milliseconds
    int read_buffer_size;           // Read buffer size
    int write_buffer_size;          // Write buffer size
    int enable_tcp_nodelay;         // Disable Nagle's algorithm
    int backlog_size;               // Listen backlog
} NetworkConfig;

// ----------------------------------------------------------------------------
// Distributed System Configuration
// ----------------------------------------------------------------------------
typedef struct {
    int is_master;                  // 1 if this node is master
    int worker_count;               // Number of workers
    WorkerConfig workers[MAX_CONFIG_WORKERS];
    int replication_factor;         // Number of replicas
    int partition_strategy;         // 0=HASH, 1=RANGE
    int heartbeat_interval_ms;      // Heartbeat interval
    int worker_timeout_ms;          // Worker timeout
    int enable_auto_failover;       // Auto failover on worker failure
} DistributedConfig;

// ----------------------------------------------------------------------------
// Cluster Configuration (Distributed System)
// ----------------------------------------------------------------------------
typedef struct {
    // Node Identity
    char node_name[64];             // Unique node name
    char node_id[64];               // Node ID (auto-generated if empty)
    char cluster_name[64];          // Cluster name
    
    // Network Binding
    char bind_host[64];             // Host to bind to (0.0.0.0 for all)
    int bind_port;                  // Port to bind to
    char advertise_host[64];        // Host to advertise to other nodes
    int advertise_port;             // Port to advertise
    
    // Seed Nodes for Discovery
    char seed_nodes[8][128];        // Up to 8 seed node addresses (host:port)
    int seed_node_count;            // Number of seed nodes
    
    // Raft Consensus
    int election_timeout_min_ms;    // Min election timeout (ms)
    int election_timeout_max_ms;    // Max election timeout (ms)
    int raft_log_max_entries;       // Max Raft log entries before compaction
    
    // Replication
    int replication_factor;         // Number of replicas per partition
    int replication_mode;           // 0=ASYNC, 1=SYNC, 2=SYNC_ALL
    int min_sync_replicas;          // Min sync replicas for SYNC mode
    
    // Partitioning
    int partition_count;            // Number of partitions
    int partition_strategy;         // 0=HASH, 1=RANGE
    int auto_rebalance;             // Enable auto rebalancing
    int rebalance_threshold_pct;    // Imbalance threshold for rebalancing
    
    // Health Checks
    int heartbeat_interval_ms;      // Heartbeat interval (ms)
    int node_timeout_ms;            // Node timeout before marked dead
    int health_check_interval_ms;   // Health check interval
    
    // Client Pool
    int max_clients;                // Max concurrent clients
    int client_timeout_sec;         // Client idle timeout
    
    // Enabled flag
    int cluster_enabled;            // Enable cluster mode
} ClusterConfig_Settings;  // Named differently to avoid collision with cluster.h

// ----------------------------------------------------------------------------
// Security Configuration
// ----------------------------------------------------------------------------
typedef struct {
    int enable_auth;                // Enable authentication
    char default_admin_user[64];    // Default admin username
    char default_admin_pass[64];    // Default admin password (hashed at runtime)
    int password_min_length;        // Minimum password length
    int session_timeout_sec;        // Session timeout in seconds
    int max_login_attempts;         // Max failed login attempts before lockout
    int lockout_duration_sec;       // Lockout duration after max attempts
} SecurityConfig;

// ----------------------------------------------------------------------------
// Logging Configuration
// ----------------------------------------------------------------------------
typedef struct {
    int log_level;                  // 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR
    char log_file[MAX_PATH_LENGTH]; // Log file path
    int max_log_size_mb;            // Max log file size before rotation
    int log_rotation_count;         // Number of rotated logs to keep
    int enable_query_logging;       // Log all queries
    int enable_slow_query_log;      // Log slow queries
    int slow_query_threshold_ms;    // Slow query threshold
} LoggingConfig;

// ----------------------------------------------------------------------------
// Query Execution Configuration
// ----------------------------------------------------------------------------
typedef struct {
    int max_result_rows;            // Max rows to return
    int query_timeout_sec;          // Query timeout
    int enable_query_cache;         // Enable query result caching
    int query_cache_size_mb;        // Query cache size
    int max_memory_per_query_mb;    // Max memory per query
} QueryConfig;

// ----------------------------------------------------------------------------
// Master Configuration Structure
// ----------------------------------------------------------------------------
typedef struct {
    char config_file[MAX_PATH_LENGTH];
    int config_loaded;
    
    StorageConfig storage;
    StorageEngineConfig storage_engine;
    BufferPoolConfig buffer_pool;
    NetworkConfig network;
    DistributedConfig distributed;
    ClusterConfig_Settings cluster;  // New cluster configuration
    SecurityConfig security;
    LoggingConfig logging;
    QueryConfig query;
} InventixConfig;

// ----------------------------------------------------------------------------
// Global Configuration Instance
// ----------------------------------------------------------------------------
extern InventixConfig g_config;

// ----------------------------------------------------------------------------
// Configuration API
// ----------------------------------------------------------------------------

/**
 * Initialize configuration with default values
 */
void config_init_defaults(InventixConfig *config);

/**
 * Load configuration from file
 * @param config Configuration structure to populate
 * @param filepath Path to configuration file
 * @return 1 on success, 0 on failure
 */
int config_load(InventixConfig *config, const char *filepath);

/**
 * Save current configuration to file
 * @param config Configuration to save
 * @param filepath Path to save to
 * @return 1 on success, 0 on failure
 */
int config_save(const InventixConfig *config, const char *filepath);

/**
 * Get a configuration value as string
 * @param section Section name (e.g., "storage", "network")
 * @param key Key name
 * @param default_val Default value if not found
 * @return Configuration value or default
 */
const char* config_get_string(const char *section, const char *key, const char *default_val);

/**
 * Get a configuration value as integer
 */
int config_get_int(const char *section, const char *key, int default_val);

/**
 * Set a configuration value
 * @param section Section name
 * @param key Key name
 * @param value Value to set
 */
void config_set(const char *section, const char *key, const char *value);

/**
 * Reload configuration from file
 * @return 1 on success, 0 on failure
 */
int config_reload(void);

/**
 * Print current configuration (for debugging)
 */
void config_print(const InventixConfig *config);

/**
 * Validate configuration values
 * @param config Configuration to validate
 * @return 1 if valid, 0 if invalid
 */
int config_validate(const InventixConfig *config);

// ----------------------------------------------------------------------------
// Convenience Macros for Accessing Global Config
// ----------------------------------------------------------------------------
#define CFG_STORAGE         (g_config.storage)
#define CFG_STORAGE_ENGINE  (g_config.storage_engine)
#define CFG_BUFFER_POOL     (g_config.buffer_pool)
#define CFG_NETWORK         (g_config.network)
#define CFG_DISTRIBUTED     (g_config.distributed)
#define CFG_CLUSTER         (g_config.cluster)
#define CFG_SECURITY        (g_config.security)
#define CFG_LOGGING         (g_config.logging)
#define CFG_QUERY           (g_config.query)

// Quick access macros
#define CFG_SERVER_PORT     (g_config.network.server_port)
#define CFG_POOL_SIZE       (g_config.buffer_pool.pool_size)
#define CFG_MAX_WORKERS     (g_config.distributed.worker_count)
#define CFG_IS_MASTER       (g_config.distributed.is_master)
#define CFG_ENGINE_TYPE     (g_config.storage_engine.engine_type)
#define CFG_COMPRESSION     (g_config.storage_engine.compression_type)
#define CFG_CLUSTER_ENABLED (g_config.cluster.cluster_enabled)

#endif // INVENTIX_CONFIG_H
