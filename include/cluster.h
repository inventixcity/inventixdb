/**
 * InventixDB Cluster Management System
 * 
 * Distributed architecture with:
 * - Dynamic cluster membership (Raft consensus)
 * - Heartbeat & health monitoring
 * - Primary-Replica replication (sync/async)
 * - Hash & Range partitioning
 * - Auto-rebalancing
 * 
 * Architecture:
 *   Client --> Leader Node --> Follower Nodes (replicas)
 *                          --> Partition shards
 */

#ifndef INVENTIX_CLUSTER_H
#define INVENTIX_CLUSTER_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
typedef int socket_t;
#define INVALID_SOCK -1
#define closesocket close
#endif

// ============================================================================
// CONSTANTS & LIMITS
// ============================================================================

#define CLUSTER_MAX_NODES          32
#define CLUSTER_MAX_PARTITIONS     256
#define CLUSTER_HEARTBEAT_MS       1000      // 1 second heartbeat
#define CLUSTER_ELECTION_TIMEOUT   5000      // 5 seconds before election
#define CLUSTER_NODE_TIMEOUT       10000     // 10 seconds before marking dead
#define CLUSTER_REPLICATION_TIMEOUT 5000     // 5 seconds for sync replication
#define CLUSTER_MAX_LOG_ENTRIES    10000
#define CLUSTER_PROTOCOL_VERSION   1
#define CLUSTER_MAGIC              0x494E5658  // "INVX"

// ============================================================================
// ENUMS
// ============================================================================

// Node roles in Raft consensus
typedef enum {
    NODE_ROLE_FOLLOWER = 0,
    NODE_ROLE_CANDIDATE,
    NODE_ROLE_LEADER
} NodeRole;

// Node health status
typedef enum {
    NODE_STATUS_UNKNOWN = 0,
    NODE_STATUS_HEALTHY,
    NODE_STATUS_DEGRADED,
    NODE_STATUS_UNREACHABLE,
    NODE_STATUS_DEAD
} NodeStatus;

// Replication mode
typedef enum {
    REPL_MODE_ASYNC = 0,      // Fire and forget (fast, less consistent)
    REPL_MODE_SYNC,           // Wait for majority (balanced)
    REPL_MODE_SYNC_ALL        // Wait for all replicas (slow, most consistent)
} ReplicationMode;

// Partitioning strategy
typedef enum {
    PARTITION_HASH = 0,       // Consistent hashing
    PARTITION_RANGE           // Range-based partitioning
} PartitionStrategy;

// Message types for cluster communication
typedef enum {
    MSG_HEARTBEAT = 1,
    MSG_HEARTBEAT_ACK,
    MSG_VOTE_REQUEST,
    MSG_VOTE_RESPONSE,
    MSG_APPEND_ENTRIES,
    MSG_APPEND_RESPONSE,
    MSG_JOIN_REQUEST,
    MSG_JOIN_RESPONSE,
    MSG_LEAVE_NOTIFY,
    MSG_QUERY_FORWARD,
    MSG_QUERY_RESPONSE,
    MSG_REPLICATE_DATA,
    MSG_REPLICATE_ACK,
    MSG_PARTITION_ASSIGN,
    MSG_PARTITION_TRANSFER,
    MSG_GOSSIP_STATE,
    MSG_CLIENT_REQUEST,
    MSG_CLIENT_RESPONSE
} ClusterMessageType;

// ============================================================================
// STRUCTURES
// ============================================================================

// Network address
typedef struct {
    char host[64];
    uint16_t port;
} NodeAddress;

// Cluster node information
typedef struct ClusterNode {
    uint32_t node_id;              // Unique node identifier
    char name[32];                 // Human-readable name
    NodeAddress address;           // Network address
    NodeRole role;                 // Current role (follower/candidate/leader)
    NodeStatus status;             // Health status
    time_t last_heartbeat;         // Last heartbeat received
    time_t join_time;              // When node joined cluster
    uint64_t term;                 // Current Raft term
    uint32_t voted_for;            // Vote in current term
    int partition_count;           // Number of partitions owned
    uint32_t *partitions;          // Array of partition IDs
    
    // Statistics
    uint64_t queries_processed;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint32_t active_connections;
    double avg_latency_ms;
} ClusterNode;

// Raft log entry
typedef struct {
    uint64_t index;
    uint64_t term;
    char *command;                 // SQL/Query command
    size_t command_len;
    time_t timestamp;
    bool committed;
} RaftLogEntry;

// Raft state for consensus
typedef struct {
    uint64_t current_term;
    uint32_t voted_for;
    uint64_t commit_index;
    uint64_t last_applied;
    
    // Log
    RaftLogEntry *log;
    size_t log_count;
    size_t log_capacity;
    
    // Leader state (only valid if leader)
    uint64_t *next_index;          // For each follower
    uint64_t *match_index;         // For each follower
    
    // Timing
    time_t last_heartbeat_sent;
    time_t election_deadline;
    
    // Election
    int votes_received;
    bool election_in_progress;
} RaftState;

// Partition information
typedef struct {
    uint32_t partition_id;
    uint32_t primary_node;         // Node ID of primary
    uint32_t *replica_nodes;       // Node IDs of replicas
    int replica_count;
    
    // For range partitioning
    char *range_start;             // Start key (inclusive)
    char *range_end;               // End key (exclusive)
    
    // Statistics
    uint64_t row_count;
    uint64_t size_bytes;
    time_t last_modified;
} PartitionInfo;

// Message header for all cluster communication
typedef struct {
    uint32_t magic;                // CLUSTER_MAGIC
    uint16_t version;              // Protocol version
    uint16_t type;                 // ClusterMessageType
    uint32_t sender_id;            // Sender node ID
    uint32_t receiver_id;          // Target node ID (0 = broadcast)
    uint64_t term;                 // Raft term
    uint32_t payload_len;          // Length of payload
    uint32_t checksum;             // CRC32 of payload
} ClusterMessageHeader;

// Heartbeat message
typedef struct {
    ClusterMessageHeader header;
    uint64_t commit_index;
    uint32_t leader_id;
    uint64_t timestamp;
} HeartbeatMessage;

// Vote request (Raft)
typedef struct {
    ClusterMessageHeader header;
    uint64_t last_log_index;
    uint64_t last_log_term;
} VoteRequest;

// Vote response
typedef struct {
    ClusterMessageHeader header;
    bool vote_granted;
} VoteResponse;

// Append entries (Raft log replication)
typedef struct {
    ClusterMessageHeader header;
    uint64_t prev_log_index;
    uint64_t prev_log_term;
    uint64_t leader_commit;
    int entry_count;
    // Followed by entry_count RaftLogEntry structures
} AppendEntriesRequest;

// Join request
typedef struct {
    ClusterMessageHeader header;
    NodeAddress address;
    char name[32];
} JoinRequest;

// Query forward (for non-leader nodes)
typedef struct {
    ClusterMessageHeader header;
    uint32_t client_id;
    uint32_t query_id;
    uint32_t query_len;
    // Followed by query string
} QueryForward;

// Replication data message
typedef struct {
    ClusterMessageHeader header;
    uint32_t partition_id;
    uint64_t sequence_num;
    uint32_t data_len;
    // Followed by serialized data
} ReplicateData;

// Cluster configuration
typedef struct {
    uint32_t self_node_id;
    char self_name[32];
    NodeAddress self_address;
    
    // Cluster settings
    ReplicationMode replication_mode;
    PartitionStrategy partition_strategy;
    int replication_factor;        // Number of replicas per partition
    int min_sync_replicas;         // Minimum replicas for sync commit
    
    // Timeouts (ms)
    int heartbeat_interval;
    int election_timeout_min;
    int election_timeout_max;
    int node_timeout;
    
    // Discovery
    NodeAddress seed_nodes[16];
    int seed_node_count;
    
    // Auto-rebalancing
    bool auto_rebalance;
    int rebalance_threshold_percent;  // Trigger when imbalance exceeds this
} ClusterConfig;

// Main cluster state
typedef struct {
    ClusterConfig config;
    
    // Nodes
    ClusterNode *nodes;
    int node_count;
    int node_capacity;
    
    // Self
    ClusterNode *self;
    NodeRole current_role;
    uint32_t current_leader;
    
    // Raft
    RaftState raft;
    
    // Partitions
    PartitionInfo *partitions;
    int partition_count;
    
    // Networking
    socket_t listen_socket;
    socket_t *peer_sockets;
    
    // Threading
    bool running;
    void *heartbeat_thread;
    void *election_thread;
    void *replication_thread;
    void *rebalance_thread;
    
    // Callbacks
    void (*on_leader_change)(uint32_t new_leader);
    void (*on_node_join)(ClusterNode *node);
    void (*on_node_leave)(ClusterNode *node);
    void (*on_partition_change)(PartitionInfo *partition);
    
    // Statistics
    uint64_t total_elections;
    uint64_t total_rebalances;
    time_t cluster_start_time;
} ClusterState;

// Client connection info
typedef struct {
    uint32_t client_id;
    socket_t socket;
    NodeAddress address;
    time_t connect_time;
    time_t last_activity;
    char current_db[64];
    char username[64];
    bool authenticated;
    uint64_t queries_executed;
} ClientConnection;

// Client pool for server
typedef struct {
    ClientConnection *clients;
    int client_count;
    int client_capacity;
    int max_clients;
    void *lock;                    // Mutex for thread safety
} ClientPool;

// ============================================================================
// GLOBAL STATE
// ============================================================================

extern ClusterState *g_cluster;

// ============================================================================
// INITIALIZATION & SHUTDOWN
// ============================================================================

/**
 * Initialize cluster subsystem
 * @param config Cluster configuration
 * @return 0 on success, -1 on error
 */
int cluster_init(ClusterConfig *config);

/**
 * Start cluster services (heartbeat, election, etc.)
 * @return 0 on success, -1 on error
 */
int cluster_start(void);

/**
 * Stop cluster services gracefully
 */
void cluster_stop(void);

/**
 * Shutdown and cleanup cluster
 */
void cluster_shutdown(void);

// ============================================================================
// NODE MANAGEMENT
// ============================================================================

/**
 * Join an existing cluster using seed nodes
 * @return 0 on success, -1 on error
 */
int cluster_join(void);

/**
 * Leave cluster gracefully
 */
void cluster_leave(void);

/**
 * Add a node to the cluster (leader only)
 * @param address Node address
 * @param name Node name
 * @return Node ID on success, -1 on error
 */
int cluster_add_node(NodeAddress *address, const char *name);

/**
 * Remove a node from the cluster (leader only)
 * @param node_id Node to remove
 * @return 0 on success, -1 on error
 */
int cluster_remove_node(uint32_t node_id);

/**
 * Get node by ID
 * @param node_id Node ID
 * @return Node pointer or NULL
 */
ClusterNode* cluster_get_node(uint32_t node_id);

/**
 * Get current leader node
 * @return Leader node pointer or NULL
 */
ClusterNode* cluster_get_leader(void);

/**
 * Check if this node is the leader
 * @return true if leader
 */
bool cluster_is_leader(void);

/**
 * Get all healthy nodes
 * @param out_nodes Output array
 * @param max_nodes Max nodes to return
 * @return Number of healthy nodes
 */
int cluster_get_healthy_nodes(ClusterNode **out_nodes, int max_nodes);

// ============================================================================
// RAFT CONSENSUS
// ============================================================================

/**
 * Start an election (called when election timeout)
 */
void raft_start_election(void);

/**
 * Handle vote request from candidate
 * @param request Vote request message
 * @param response Output vote response
 */
void raft_handle_vote_request(VoteRequest *request, VoteResponse *response);

/**
 * Handle append entries from leader
 * @param request Append entries request
 * @return Success/failure
 */
bool raft_handle_append_entries(AppendEntriesRequest *request);

/**
 * Append a new entry to the log (leader only)
 * @param command Command to append
 * @param command_len Command length
 * @return Log index on success, -1 on error
 */
int64_t raft_append_log(const char *command, size_t command_len);

/**
 * Commit entries up to specified index
 * @param index Commit index
 */
void raft_commit_to(uint64_t index);

// ============================================================================
// HEARTBEAT & HEALTH
// ============================================================================

/**
 * Send heartbeat to all followers (leader only)
 */
void heartbeat_send_all(void);

/**
 * Handle received heartbeat
 * @param msg Heartbeat message
 */
void heartbeat_handle(HeartbeatMessage *msg);

/**
 * Check node health and update status
 */
void health_check_nodes(void);

/**
 * Get cluster health summary
 * @param healthy_count Output healthy count
 * @param total_count Output total count
 */
void cluster_health_summary(int *healthy_count, int *total_count);

// ============================================================================
// REPLICATION
// ============================================================================

/**
 * Replicate data to followers
 * @param partition_id Partition being replicated
 * @param data Serialized data
 * @param data_len Data length
 * @param mode Replication mode
 * @return Number of successful replications
 */
int replicate_data(uint32_t partition_id, const void *data, 
                   size_t data_len, ReplicationMode mode);

/**
 * Handle incoming replication data
 * @param msg Replication message
 * @return 0 on success
 */
int handle_replication(ReplicateData *msg);

/**
 * Sync a partition to a new replica
 * @param partition_id Partition to sync
 * @param target_node Target node ID
 * @return 0 on success
 */
int sync_partition(uint32_t partition_id, uint32_t target_node);

// ============================================================================
// PARTITIONING
// ============================================================================

/**
 * Initialize partitions based on strategy
 * @param num_partitions Number of partitions
 * @param strategy Partitioning strategy
 */
void partition_init(int num_partitions, PartitionStrategy strategy);

/**
 * Get partition for a key (hash partitioning)
 * @param key Key to hash
 * @return Partition ID
 */
uint32_t partition_for_key(const char *key);

/**
 * Get partition for a value (range partitioning)
 * @param value Value to check
 * @return Partition ID
 */
uint32_t partition_for_range(const char *value);

/**
 * Get node responsible for a partition
 * @param partition_id Partition ID
 * @return Node ID of primary
 */
uint32_t partition_get_owner(uint32_t partition_id);

/**
 * Assign partition to a node
 * @param partition_id Partition ID
 * @param node_id Node to assign
 * @param is_primary Whether this is the primary
 */
void partition_assign(uint32_t partition_id, uint32_t node_id, bool is_primary);

/**
 * Set range boundaries for a partition
 * @param partition_id Partition ID
 * @param start Start key (inclusive)
 * @param end End key (exclusive)
 */
void partition_set_range(uint32_t partition_id, const char *start, const char *end);

// ============================================================================
// AUTO-REBALANCING
// ============================================================================

/**
 * Check if rebalancing is needed
 * @return true if rebalancing should occur
 */
bool rebalance_needed(void);

/**
 * Trigger partition rebalancing
 * @return Number of partitions moved
 */
int rebalance_partitions(void);

/**
 * Handle node join rebalancing
 * @param new_node Newly joined node
 */
void rebalance_on_join(ClusterNode *new_node);

/**
 * Handle node leave rebalancing
 * @param left_node Node that left
 */
void rebalance_on_leave(ClusterNode *left_node);

// ============================================================================
// QUERY ROUTING
// ============================================================================

/**
 * Route a query to the appropriate node(s)
 * @param query SQL query
 * @param query_len Query length
 * @param response Output response buffer
 * @param response_capacity Response buffer capacity
 * @return Response length or -1 on error
 */
int cluster_route_query(const char *query, size_t query_len,
                        char *response, size_t response_capacity);

/**
 * Forward query to leader (for non-leader nodes)
 * @param query SQL query
 * @param response Output response
 * @return 0 on success
 */
int cluster_forward_to_leader(const char *query, char **response);

/**
 * Execute query locally
 * @param query SQL query
 * @param response Output response
 * @return 0 on success
 */
int cluster_execute_local(const char *query, char **response);

// ============================================================================
// CLIENT MANAGEMENT
// ============================================================================

/**
 * Initialize client pool
 * @param max_clients Maximum concurrent clients
 * @return 0 on success
 */
int client_pool_init(int max_clients);

/**
 * Accept a new client connection
 * @param socket Client socket
 * @param address Client address
 * @return Client ID or -1 on error
 */
int client_accept(socket_t socket, NodeAddress *address);

/**
 * Disconnect a client
 * @param client_id Client to disconnect
 */
void client_disconnect(uint32_t client_id);

/**
 * Get client by ID
 * @param client_id Client ID
 * @return Client connection or NULL
 */
ClientConnection* client_get(uint32_t client_id);

/**
 * Cleanup inactive clients
 * @param timeout_sec Inactivity timeout
 * @return Number of clients cleaned up
 */
int client_cleanup_inactive(int timeout_sec);

// ============================================================================
// NETWORKING
// ============================================================================

/**
 * Create listening socket
 * @param address Address to bind
 * @return Socket or INVALID_SOCK on error
 */
socket_t net_create_listener(NodeAddress *address);

/**
 * Connect to a node
 * @param address Target address
 * @return Socket or INVALID_SOCK on error
 */
socket_t net_connect(NodeAddress *address);

/**
 * Send message to a node
 * @param socket Target socket
 * @param header Message header
 * @param payload Payload data
 * @return Bytes sent or -1 on error
 */
int net_send_message(socket_t socket, ClusterMessageHeader *header, 
                     const void *payload);

/**
 * Receive message from a node
 * @param socket Source socket
 * @param header Output header
 * @param payload Output payload (caller frees)
 * @return Bytes received or -1 on error
 */
int net_receive_message(socket_t socket, ClusterMessageHeader *header,
                        void **payload);

/**
 * Broadcast message to all nodes
 * @param header Message header
 * @param payload Payload data
 * @return Number of successful sends
 */
int net_broadcast(ClusterMessageHeader *header, const void *payload);

// ============================================================================
// UTILITIES
// ============================================================================

/**
 * Generate unique node ID
 * @return Node ID
 */
uint32_t generate_node_id(void);

/**
 * Calculate CRC32 checksum
 * @param data Data to checksum
 * @param len Data length
 * @return CRC32 value
 */
uint32_t cluster_crc32(const void *data, size_t len);

/**
 * Get current timestamp in milliseconds
 * @return Timestamp
 */
uint64_t cluster_timestamp_ms(void);

/**
 * Random delay for election timeout (jitter)
 * @return Random delay in ms
 */
int random_election_delay(void);

/**
 * Print cluster status
 */
void cluster_print_status(void);

/**
 * Get cluster statistics as string
 * @param buffer Output buffer
 * @param capacity Buffer capacity
 */
void cluster_get_stats(char *buffer, size_t capacity);

// ============================================================================
// HINGLISH ALIASES
// ============================================================================

#define samuh_shuru         cluster_start         // Start cluster (Samuh = Group)
#define samuh_band          cluster_stop          // Stop cluster  
#define samuh_judo          cluster_join          // Join cluster (Judo = Join)
#define samuh_chhodo        cluster_leave         // Leave cluster (Chhodo = Leave)
#define samuh_neta          cluster_get_leader    // Get leader (Neta = Leader)
#define samuh_sehat         cluster_health_summary // Health check (Sehat = Health)

#endif // INVENTIX_CLUSTER_H
