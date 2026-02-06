/**
 * InventixDB Cluster Management Implementation
 * 
 * Implements:
 * - Raft consensus for leader election
 * - Heartbeat & health monitoring
 * - Primary-Replica replication
 * - Hash & Range partitioning
 * - Auto-rebalancing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")
#define sleep_ms(x) Sleep(x)
#define thread_t HANDLE
#define THREAD_RETURN DWORD WINAPI
#else
#include <unistd.h>
#include <pthread.h>
#define sleep_ms(x) usleep((x) * 1000)
#define thread_t pthread_t
#define THREAD_RETURN void*
#endif

#include "cluster.h"
#include "logger.h"
#include "config.h"

// ============================================================================
// GLOBAL STATE
// ============================================================================

ClusterState *g_cluster = NULL;
static ClientPool *g_client_pool = NULL;

// CRC32 lookup table
static uint32_t crc32_table[256];
static bool crc32_initialized = false;

// ============================================================================
// UTILITIES
// ============================================================================

static void init_crc32_table(void) {
    if (crc32_initialized) return;
    
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = true;
}

uint32_t cluster_crc32(const void *data, size_t len) {
    init_crc32_table();
    const uint8_t *bytes = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ bytes[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

uint64_t cluster_timestamp_ms(void) {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (t / 10000) - 11644473600000ULL;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

uint32_t generate_node_id(void) {
    // Combine timestamp with random for uniqueness
    srand((unsigned)time(NULL) ^ (unsigned)cluster_timestamp_ms());
    return (uint32_t)(time(NULL) ^ rand());
}

int random_election_delay(void) {
    if (!g_cluster) return 1000;
    int min = g_cluster->config.election_timeout_min;
    int max = g_cluster->config.election_timeout_max;
    return min + (rand() % (max - min + 1));
}

// ============================================================================
// NETWORK LAYER
// ============================================================================

socket_t net_create_listener(NodeAddress *address) {
    socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCK) {
        LOG_ERROR("Failed to create socket");
        return INVALID_SOCK;
    }
    
    // Set reuse address
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(address->port);
    
    if (strlen(address->host) == 0 || strcmp(address->host, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        addr.sin_addr.s_addr = inet_addr(address->host);
    }
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind to %s:%d", address->host, address->port);
        closesocket(sock);
        return INVALID_SOCK;
    }
    
    if (listen(sock, 128) < 0) {
        LOG_ERROR("Failed to listen on socket");
        closesocket(sock);
        return INVALID_SOCK;
    }
    
    LOG_INFO("Listening on %s:%d", address->host, address->port);
    return sock;
}

socket_t net_connect(NodeAddress *address) {
    socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCK) {
        return INVALID_SOCK;
    }
    
    // Set non-blocking with timeout
#ifdef _WIN32
    DWORD timeout = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv = {5, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(address->port);
    addr.sin_addr.s_addr = inet_addr(address->host);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        closesocket(sock);
        return INVALID_SOCK;
    }
    
    return sock;
}

int cluster_send_message(socket_t socket, ClusterMessageHeader *header, 
                     const void *payload) {
    if (socket == INVALID_SOCK) return -1;
    
    // Calculate checksum
    if (payload && header->payload_len > 0) {
        header->checksum = cluster_crc32(payload, header->payload_len);
    }
    
    // Send header
    int sent = send(socket, (const char*)header, sizeof(ClusterMessageHeader), 0);
    if (sent != sizeof(ClusterMessageHeader)) {
        return -1;
    }
    
    // Send payload if present
    if (payload && header->payload_len > 0) {
        sent = send(socket, (const char*)payload, header->payload_len, 0);
        if (sent != (int)header->payload_len) {
            return -1;
        }
        return sizeof(ClusterMessageHeader) + header->payload_len;
    }
    
    return sizeof(ClusterMessageHeader);
}

int net_receive_message(socket_t socket, ClusterMessageHeader *header,
                        void **payload) {
    if (socket == INVALID_SOCK) return -1;
    
    // Receive header
    int received = recv(socket, (char*)header, sizeof(ClusterMessageHeader), MSG_WAITALL);
    if (received != sizeof(ClusterMessageHeader)) {
        return -1;
    }
    
    // Validate magic
    if (header->magic != CLUSTER_MAGIC) {
        LOG_WARN("Invalid message magic: 0x%X", header->magic);
        return -1;
    }
    
    // Receive payload
    if (header->payload_len > 0) {
        *payload = malloc(header->payload_len);
        received = recv(socket, (char*)*payload, header->payload_len, MSG_WAITALL);
        if (received != (int)header->payload_len) {
            free(*payload);
            *payload = NULL;
            return -1;
        }
        
        // Verify checksum
        uint32_t crc = cluster_crc32(*payload, header->payload_len);
        if (crc != header->checksum) {
            LOG_WARN("Checksum mismatch: expected 0x%X, got 0x%X", header->checksum, crc);
            free(*payload);
            *payload = NULL;
            return -1;
        }
        
        return sizeof(ClusterMessageHeader) + header->payload_len;
    }
    
    *payload = NULL;
    return sizeof(ClusterMessageHeader);
}

int net_broadcast(ClusterMessageHeader *header, const void *payload) {
    if (!g_cluster) return 0;
    
    int success_count = 0;
    for (int i = 0; i < g_cluster->node_count; i++) {
        ClusterNode *node = &g_cluster->nodes[i];
        if (node->node_id == g_cluster->config.self_node_id) continue;
        if (node->status == NODE_STATUS_DEAD) continue;
        
        socket_t sock = net_connect(&node->address);
        if (sock != INVALID_SOCK) {
            header->receiver_id = node->node_id;
            if (cluster_send_message(sock, header, payload) > 0) {
                success_count++;
            }
            closesocket(sock);
        }
    }
    
    return success_count;
}

// ============================================================================
// CLUSTER INITIALIZATION
// ============================================================================

int cluster_init(ClusterConfig *config) {
    if (g_cluster) {
        LOG_WARN("Cluster already initialized");
        return -1;
    }
    
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        LOG_ERROR("WSAStartup failed");
        return -1;
    }
#endif
    
    g_cluster = calloc(1, sizeof(ClusterState));
    if (!g_cluster) {
        LOG_ERROR("Failed to allocate cluster state");
        return -1;
    }
    
    // Copy configuration
    memcpy(&g_cluster->config, config, sizeof(ClusterConfig));
    
    // Set defaults if not specified
    if (g_cluster->config.heartbeat_interval <= 0) {
        g_cluster->config.heartbeat_interval = CLUSTER_HEARTBEAT_MS;
    }
    if (g_cluster->config.election_timeout_min <= 0) {
        g_cluster->config.election_timeout_min = 3000;
    }
    if (g_cluster->config.election_timeout_max <= 0) {
        g_cluster->config.election_timeout_max = 5000;
    }
    if (g_cluster->config.node_timeout <= 0) {
        g_cluster->config.node_timeout = CLUSTER_NODE_TIMEOUT;
    }
    if (g_cluster->config.replication_factor <= 0) {
        g_cluster->config.replication_factor = 2;
    }
    
    // Allocate node array
    g_cluster->node_capacity = 16;
    g_cluster->nodes = calloc(g_cluster->node_capacity, sizeof(ClusterNode));
    
    // Initialize self node
    g_cluster->self = &g_cluster->nodes[0];
    g_cluster->self->node_id = config->self_node_id;
    strncpy(g_cluster->self->name, config->self_name, sizeof(g_cluster->self->name) - 1);
    memcpy(&g_cluster->self->address, &config->self_address, sizeof(NodeAddress));
    g_cluster->self->role = NODE_ROLE_FOLLOWER;
    g_cluster->self->status = NODE_STATUS_HEALTHY;
    g_cluster->self->join_time = time(NULL);
    g_cluster->node_count = 1;
    
    // Initialize Raft state
    g_cluster->raft.current_term = 0;
    g_cluster->raft.voted_for = 0;
    g_cluster->raft.commit_index = 0;
    g_cluster->raft.last_applied = 0;
    g_cluster->raft.log_capacity = 1000;
    g_cluster->raft.log = calloc(g_cluster->raft.log_capacity, sizeof(RaftLogEntry));
    g_cluster->raft.election_deadline = time(NULL) + random_election_delay() / 1000;
    
    g_cluster->current_role = NODE_ROLE_FOLLOWER;
    g_cluster->running = false;
    g_cluster->cluster_start_time = time(NULL);
    
    LOG_INFO("Cluster initialized: node_id=%u, name=%s, address=%s:%d",
             g_cluster->config.self_node_id, g_cluster->config.self_name,
             g_cluster->config.self_address.host, g_cluster->config.self_address.port);
    
    return 0;
}

void cluster_shutdown(void) {
    if (!g_cluster) return;
    
    cluster_stop();
    
    // Close listening socket
    if (g_cluster->listen_socket != INVALID_SOCK) {
        closesocket(g_cluster->listen_socket);
    }
    
    // Free nodes
    free(g_cluster->nodes);
    
    // Free Raft log
    for (size_t i = 0; i < g_cluster->raft.log_count; i++) {
        free(g_cluster->raft.log[i].command);
    }
    free(g_cluster->raft.log);
    free(g_cluster->raft.next_index);
    free(g_cluster->raft.match_index);
    
    // Free partitions
    for (int i = 0; i < g_cluster->partition_count; i++) {
        free(g_cluster->partitions[i].replica_nodes);
        free(g_cluster->partitions[i].range_start);
        free(g_cluster->partitions[i].range_end);
    }
    free(g_cluster->partitions);
    
    free(g_cluster);
    g_cluster = NULL;
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    LOG_INFO("Cluster shutdown complete");
}

// ============================================================================
// HEARTBEAT THREAD
// ============================================================================

static THREAD_RETURN heartbeat_thread_func(void *arg) {
    (void)arg;
    LOG_INFO("Heartbeat thread started");
    
    while (g_cluster && g_cluster->running) {
        if (g_cluster->current_role == NODE_ROLE_LEADER) {
            heartbeat_send_all();
        }
        
        // Check node health
        health_check_nodes();
        
        // Check election timeout
        if (g_cluster->current_role == NODE_ROLE_FOLLOWER) {
            time_t now = time(NULL);
            if (now > g_cluster->raft.election_deadline) {
                LOG_INFO("Election timeout - starting election");
                raft_start_election();
            }
        }
        
        sleep_ms(g_cluster->config.heartbeat_interval);
    }
    
    LOG_INFO("Heartbeat thread stopped");
    return 0;
}

void heartbeat_send_all(void) {
    if (!g_cluster || g_cluster->current_role != NODE_ROLE_LEADER) return;
    
    HeartbeatMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.magic = CLUSTER_MAGIC;
    msg.header.version = CLUSTER_PROTOCOL_VERSION;
    msg.header.type = MSG_HEARTBEAT;
    msg.header.sender_id = g_cluster->config.self_node_id;
    msg.header.term = g_cluster->raft.current_term;
    msg.header.payload_len = sizeof(HeartbeatMessage) - sizeof(ClusterMessageHeader);
    msg.commit_index = g_cluster->raft.commit_index;
    msg.leader_id = g_cluster->config.self_node_id;
    msg.timestamp = cluster_timestamp_ms();
    
    for (int i = 0; i < g_cluster->node_count; i++) {
        ClusterNode *node = &g_cluster->nodes[i];
        if (node->node_id == g_cluster->config.self_node_id) continue;
        if (node->status == NODE_STATUS_DEAD) continue;
        
        socket_t sock = net_connect(&node->address);
        if (sock != INVALID_SOCK) {
            msg.header.receiver_id = node->node_id;
            cluster_send_message(sock, &msg.header, 
                            ((char*)&msg) + sizeof(ClusterMessageHeader));
            
            // Receive ACK
            ClusterMessageHeader ack_header;
            void *payload = NULL;
            if (net_receive_message(sock, &ack_header, &payload) > 0) {
                if (ack_header.type == MSG_HEARTBEAT_ACK) {
                    node->last_heartbeat = time(NULL);
                    node->status = NODE_STATUS_HEALTHY;
                }
                free(payload);
            }
            closesocket(sock);
        } else {
            // Failed to connect
            if (node->status == NODE_STATUS_HEALTHY) {
                node->status = NODE_STATUS_DEGRADED;
            }
        }
    }
    
    g_cluster->raft.last_heartbeat_sent = time(NULL);
}

void heartbeat_handle(HeartbeatMessage *msg) {
    if (!g_cluster || !msg) return;
    
    // Update term if needed
    if (msg->header.term > g_cluster->raft.current_term) {
        g_cluster->raft.current_term = msg->header.term;
        g_cluster->current_role = NODE_ROLE_FOLLOWER;
        g_cluster->raft.voted_for = 0;
    }
    
    // Acknowledge leader
    if (msg->header.term >= g_cluster->raft.current_term) {
        g_cluster->current_leader = msg->leader_id;
        g_cluster->raft.election_deadline = time(NULL) + random_election_delay() / 1000;
        
        // Update commit index
        if (msg->commit_index > g_cluster->raft.commit_index) {
            g_cluster->raft.commit_index = msg->commit_index;
        }
    }
}

void health_check_nodes(void) {
    if (!g_cluster) return;
    
    time_t now = time(NULL);
    int timeout_sec = g_cluster->config.node_timeout / 1000;
    
    for (int i = 0; i < g_cluster->node_count; i++) {
        ClusterNode *node = &g_cluster->nodes[i];
        if (node->node_id == g_cluster->config.self_node_id) continue;
        
        time_t elapsed = now - node->last_heartbeat;
        
        if (elapsed > timeout_sec * 2 && node->status != NODE_STATUS_DEAD) {
            LOG_WARN("Node %s (%u) marked as DEAD (no heartbeat for %ld sec)",
                     node->name, node->node_id, elapsed);
            node->status = NODE_STATUS_DEAD;
            
            // Trigger rebalancing if enabled
            if (g_cluster->config.auto_rebalance && 
                g_cluster->current_role == NODE_ROLE_LEADER) {
                rebalance_on_leave(node);
            }
        } else if (elapsed > timeout_sec && node->status == NODE_STATUS_HEALTHY) {
            node->status = NODE_STATUS_DEGRADED;
        } else if (elapsed <= timeout_sec / 2 && node->status == NODE_STATUS_DEGRADED) {
            node->status = NODE_STATUS_HEALTHY;
        }
    }
}

void cluster_health_summary(int *healthy_count, int *total_count) {
    if (!g_cluster) {
        *healthy_count = 0;
        *total_count = 0;
        return;
    }
    
    *total_count = g_cluster->node_count;
    *healthy_count = 0;
    
    for (int i = 0; i < g_cluster->node_count; i++) {
        if (g_cluster->nodes[i].status == NODE_STATUS_HEALTHY) {
            (*healthy_count)++;
        }
    }
}

// ============================================================================
// RAFT CONSENSUS
// ============================================================================

void raft_start_election(void) {
    if (!g_cluster) return;
    
    g_cluster->current_role = NODE_ROLE_CANDIDATE;
    g_cluster->raft.current_term++;
    g_cluster->raft.voted_for = g_cluster->config.self_node_id;
    g_cluster->raft.votes_received = 1;  // Vote for self
    g_cluster->raft.election_in_progress = true;
    g_cluster->total_elections++;
    
    LOG_INFO("Starting election for term %llu", 
             (unsigned long long)g_cluster->raft.current_term);
    
    // Request votes from all nodes
    VoteRequest request;
    memset(&request, 0, sizeof(request));
    request.header.magic = CLUSTER_MAGIC;
    request.header.version = CLUSTER_PROTOCOL_VERSION;
    request.header.type = MSG_VOTE_REQUEST;
    request.header.sender_id = g_cluster->config.self_node_id;
    request.header.term = g_cluster->raft.current_term;
    request.header.payload_len = sizeof(VoteRequest) - sizeof(ClusterMessageHeader);
    request.last_log_index = g_cluster->raft.log_count > 0 ? 
                             g_cluster->raft.log[g_cluster->raft.log_count - 1].index : 0;
    request.last_log_term = g_cluster->raft.log_count > 0 ?
                            g_cluster->raft.log[g_cluster->raft.log_count - 1].term : 0;
    
    int majority = (g_cluster->node_count / 2) + 1;
    
    for (int i = 0; i < g_cluster->node_count; i++) {
        ClusterNode *node = &g_cluster->nodes[i];
        if (node->node_id == g_cluster->config.self_node_id) continue;
        if (node->status == NODE_STATUS_DEAD) continue;
        
        socket_t sock = net_connect(&node->address);
        if (sock != INVALID_SOCK) {
            request.header.receiver_id = node->node_id;
            cluster_send_message(sock, &request.header,
                            ((char*)&request) + sizeof(ClusterMessageHeader));
            
            // Receive response
            ClusterMessageHeader resp_header;
            void *payload = NULL;
            if (net_receive_message(sock, &resp_header, &payload) > 0) {
                if (resp_header.type == MSG_VOTE_RESPONSE && payload) {
                    VoteResponse *resp = (VoteResponse*)payload;
                    if (resp->vote_granted) {
                        g_cluster->raft.votes_received++;
                        LOG_DEBUG("Received vote from node %u", node->node_id);
                    }
                }
                free(payload);
            }
            closesocket(sock);
        }
    }
    
    // Check if won election
    if (g_cluster->raft.votes_received >= majority) {
        g_cluster->current_role = NODE_ROLE_LEADER;
        g_cluster->current_leader = g_cluster->config.self_node_id;
        g_cluster->self->role = NODE_ROLE_LEADER;
        
        LOG_INFO("Won election for term %llu with %d votes (majority=%d)",
                 (unsigned long long)g_cluster->raft.current_term,
                 g_cluster->raft.votes_received, majority);
        
        // Initialize leader state
        if (g_cluster->raft.next_index) free(g_cluster->raft.next_index);
        if (g_cluster->raft.match_index) free(g_cluster->raft.match_index);
        
        g_cluster->raft.next_index = calloc(g_cluster->node_count, sizeof(uint64_t));
        g_cluster->raft.match_index = calloc(g_cluster->node_count, sizeof(uint64_t));
        
        for (int i = 0; i < g_cluster->node_count; i++) {
            g_cluster->raft.next_index[i] = g_cluster->raft.log_count + 1;
            g_cluster->raft.match_index[i] = 0;
        }
        
        // Send initial heartbeat
        heartbeat_send_all();
        
        // Notify callback
        if (g_cluster->on_leader_change) {
            g_cluster->on_leader_change(g_cluster->config.self_node_id);
        }
    } else {
        // Failed election, revert to follower
        g_cluster->current_role = NODE_ROLE_FOLLOWER;
        g_cluster->raft.election_deadline = time(NULL) + random_election_delay() / 1000;
        LOG_INFO("Lost election (got %d votes, needed %d)", 
                 g_cluster->raft.votes_received, majority);
    }
    
    g_cluster->raft.election_in_progress = false;
}

void raft_handle_vote_request(VoteRequest *request, VoteResponse *response) {
    if (!g_cluster || !request || !response) return;
    
    response->header.magic = CLUSTER_MAGIC;
    response->header.version = CLUSTER_PROTOCOL_VERSION;
    response->header.type = MSG_VOTE_RESPONSE;
    response->header.sender_id = g_cluster->config.self_node_id;
    response->header.receiver_id = request->header.sender_id;
    response->header.term = g_cluster->raft.current_term;
    response->header.payload_len = sizeof(bool);
    response->vote_granted = false;
    
    // If request term is less than our term, reject
    if (request->header.term < g_cluster->raft.current_term) {
        return;
    }
    
    // If request term is greater, update our term and become follower
    if (request->header.term > g_cluster->raft.current_term) {
        g_cluster->raft.current_term = request->header.term;
        g_cluster->current_role = NODE_ROLE_FOLLOWER;
        g_cluster->raft.voted_for = 0;
    }
    
    // Check if we can vote for this candidate
    if (g_cluster->raft.voted_for == 0 || 
        g_cluster->raft.voted_for == request->header.sender_id) {
        // Check log is at least as up-to-date
        uint64_t our_last_term = g_cluster->raft.log_count > 0 ?
                                 g_cluster->raft.log[g_cluster->raft.log_count - 1].term : 0;
        uint64_t our_last_index = g_cluster->raft.log_count > 0 ?
                                  g_cluster->raft.log[g_cluster->raft.log_count - 1].index : 0;
        
        if (request->last_log_term > our_last_term ||
            (request->last_log_term == our_last_term && 
             request->last_log_index >= our_last_index)) {
            response->vote_granted = true;
            g_cluster->raft.voted_for = request->header.sender_id;
            g_cluster->raft.election_deadline = time(NULL) + random_election_delay() / 1000;
            LOG_DEBUG("Granted vote to node %u for term %llu",
                      request->header.sender_id, 
                      (unsigned long long)request->header.term);
        }
    }
    
    response->header.term = g_cluster->raft.current_term;
}

bool raft_handle_append_entries(AppendEntriesRequest *request) {
    if (!g_cluster || !request) return false;
    
    // If term is less, reject
    if (request->header.term < g_cluster->raft.current_term) {
        return false;
    }
    
    // Accept leader
    g_cluster->raft.current_term = request->header.term;
    g_cluster->current_role = NODE_ROLE_FOLLOWER;
    g_cluster->current_leader = request->header.sender_id;
    g_cluster->raft.election_deadline = time(NULL) + random_election_delay() / 1000;
    
    // Check previous log entry
    if (request->prev_log_index > 0) {
        if (request->prev_log_index > g_cluster->raft.log_count) {
            return false;  // Missing entries
        }
        if (g_cluster->raft.log[request->prev_log_index - 1].term != request->prev_log_term) {
            return false;  // Term mismatch
        }
    }
    
    // Append entries (handled by caller)
    
    // Update commit index
    if (request->leader_commit > g_cluster->raft.commit_index) {
        g_cluster->raft.commit_index = 
            (request->leader_commit < g_cluster->raft.log_count) ?
            request->leader_commit : g_cluster->raft.log_count;
    }
    
    return true;
}

int64_t raft_append_log(const char *command, size_t command_len) {
    if (!g_cluster || g_cluster->current_role != NODE_ROLE_LEADER) {
        return -1;
    }
    
    // Grow log if needed
    if (g_cluster->raft.log_count >= g_cluster->raft.log_capacity) {
        g_cluster->raft.log_capacity *= 2;
        g_cluster->raft.log = realloc(g_cluster->raft.log,
                                       g_cluster->raft.log_capacity * sizeof(RaftLogEntry));
    }
    
    RaftLogEntry *entry = &g_cluster->raft.log[g_cluster->raft.log_count];
    entry->index = g_cluster->raft.log_count + 1;
    entry->term = g_cluster->raft.current_term;
    entry->command = malloc(command_len + 1);
    memcpy(entry->command, command, command_len);
    entry->command[command_len] = '\0';
    entry->command_len = command_len;
    entry->timestamp = time(NULL);
    entry->committed = false;
    
    g_cluster->raft.log_count++;
    
    return entry->index;
}

void raft_commit_to(uint64_t index) {
    if (!g_cluster) return;
    
    while (g_cluster->raft.last_applied < index && 
           g_cluster->raft.last_applied < g_cluster->raft.log_count) {
        g_cluster->raft.last_applied++;
        RaftLogEntry *entry = &g_cluster->raft.log[g_cluster->raft.last_applied - 1];
        entry->committed = true;
        
        // Execute command locally
        LOG_DEBUG("Applying log entry %llu: %s", 
                  (unsigned long long)entry->index, entry->command);
    }
    
    g_cluster->raft.commit_index = index;
}

// ============================================================================
// REPLICATION
// ============================================================================

int replicate_data(uint32_t partition_id, const void *data, 
                   size_t data_len, ReplicationMode mode) {
    if (!g_cluster || g_cluster->current_role != NODE_ROLE_LEADER) {
        return -1;
    }
    
    // Find partition
    PartitionInfo *partition = NULL;
    for (int i = 0; i < g_cluster->partition_count; i++) {
        if (g_cluster->partitions[i].partition_id == partition_id) {
            partition = &g_cluster->partitions[i];
            break;
        }
    }
    
    if (!partition) return -1;
    
    // Prepare replication message
    ReplicateData *msg = malloc(sizeof(ReplicateData) + data_len);
    memset(msg, 0, sizeof(ReplicateData));
    msg->header.magic = CLUSTER_MAGIC;
    msg->header.version = CLUSTER_PROTOCOL_VERSION;
    msg->header.type = MSG_REPLICATE_DATA;
    msg->header.sender_id = g_cluster->config.self_node_id;
    msg->header.term = g_cluster->raft.current_term;
    msg->header.payload_len = sizeof(ReplicateData) - sizeof(ClusterMessageHeader) + data_len;
    msg->partition_id = partition_id;
    msg->sequence_num = cluster_timestamp_ms();
    msg->data_len = data_len;
    
    int success_count = 0;
    int required = 0;
    
    switch (mode) {
        case REPL_MODE_ASYNC:
            required = 0;  // Fire and forget
            break;
        case REPL_MODE_SYNC:
            required = g_cluster->config.min_sync_replicas;
            if (required == 0) required = partition->replica_count / 2;
            break;
        case REPL_MODE_SYNC_ALL:
            required = partition->replica_count;
            break;
    }
    
    // Send to all replicas
    for (int i = 0; i < partition->replica_count; i++) {
        uint32_t replica_id = partition->replica_nodes[i];
        ClusterNode *node = cluster_get_node(replica_id);
        if (!node || node->status == NODE_STATUS_DEAD) continue;
        
        socket_t sock = net_connect(&node->address);
        if (sock != INVALID_SOCK) {
            msg->header.receiver_id = replica_id;
            
            // Send message with data
            send(sock, (const char*)msg, sizeof(ReplicateData), 0);
            send(sock, (const char*)data, data_len, 0);
            
            if (mode != REPL_MODE_ASYNC) {
                // Wait for ACK
                ClusterMessageHeader ack;
                void *payload = NULL;
                if (net_receive_message(sock, &ack, &payload) > 0) {
                    if (ack.type == MSG_REPLICATE_ACK) {
                        success_count++;
                    }
                    free(payload);
                }
            } else {
                success_count++;
            }
            
            closesocket(sock);
        }
    }
    
    free(msg);
    
    if (mode != REPL_MODE_ASYNC && success_count < required) {
        LOG_WARN("Replication failed: got %d ACKs, needed %d", success_count, required);
        return -1;
    }
    
    return success_count;
}

int handle_replication(ReplicateData *msg) {
    if (!g_cluster || !msg) return -1;
    
    // Apply the replicated data locally
    LOG_DEBUG("Received replication for partition %u, %u bytes",
              msg->partition_id, msg->data_len);
    
    // TODO: Apply to local storage
    
    return 0;
}

int sync_partition(uint32_t partition_id, uint32_t target_node) {
    if (!g_cluster) return -1;
    
    LOG_INFO("Syncing partition %u to node %u", partition_id, target_node);
    
    // TODO: Full partition sync implementation
    
    return 0;
}

// ============================================================================
// PARTITIONING
// ============================================================================

static uint32_t hash_djb2(const char *str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

void partition_init(int num_partitions, PartitionStrategy strategy) {
    if (!g_cluster) return;
    
    g_cluster->partitions = calloc(num_partitions, sizeof(PartitionInfo));
    g_cluster->partition_count = num_partitions;
    
    for (int i = 0; i < num_partitions; i++) {
        PartitionInfo *p = &g_cluster->partitions[i];
        p->partition_id = i;
        p->replica_nodes = calloc(g_cluster->config.replication_factor, sizeof(uint32_t));
        p->replica_count = 0;
        
        if (strategy == PARTITION_RANGE) {
            // Divide key space into ranges
            char start[32], end[32];
            int range_size = 256 / num_partitions;
            snprintf(start, sizeof(start), "%c", (char)('A' + i * range_size / 10));
            snprintf(end, sizeof(end), "%c", (char)('A' + (i + 1) * range_size / 10));
            p->range_start = strdup(start);
            p->range_end = strdup(end);
        }
    }
    
    LOG_INFO("Initialized %d partitions with %s strategy",
             num_partitions,
             strategy == PARTITION_HASH ? "HASH" : "RANGE");
}

uint32_t partition_for_key(const char *key) {
    if (!g_cluster || !key || g_cluster->partition_count == 0) return 0;
    return hash_djb2(key) % g_cluster->partition_count;
}

uint32_t partition_for_range(const char *value) {
    if (!g_cluster || !value) return 0;
    
    for (int i = 0; i < g_cluster->partition_count; i++) {
        PartitionInfo *p = &g_cluster->partitions[i];
        if (p->range_start && p->range_end) {
            if (strcmp(value, p->range_start) >= 0 && 
                strcmp(value, p->range_end) < 0) {
                return p->partition_id;
            }
        }
    }
    
    return g_cluster->partition_count - 1;  // Last partition for overflow
}

uint32_t partition_get_owner(uint32_t partition_id) {
    if (!g_cluster || partition_id >= (uint32_t)g_cluster->partition_count) return 0;
    return g_cluster->partitions[partition_id].primary_node;
}

void partition_assign(uint32_t partition_id, uint32_t node_id, bool is_primary) {
    if (!g_cluster || partition_id >= (uint32_t)g_cluster->partition_count) return;
    
    PartitionInfo *p = &g_cluster->partitions[partition_id];
    
    if (is_primary) {
        p->primary_node = node_id;
    } else {
        // Add as replica
        if (p->replica_count < g_cluster->config.replication_factor) {
            p->replica_nodes[p->replica_count++] = node_id;
        }
    }
    
    // Update node's partition list
    ClusterNode *node = cluster_get_node(node_id);
    if (node) {
        node->partitions = realloc(node->partitions, 
                                   (node->partition_count + 1) * sizeof(uint32_t));
        node->partitions[node->partition_count++] = partition_id;
    }
}

void partition_set_range(uint32_t partition_id, const char *start, const char *end) {
    if (!g_cluster || partition_id >= (uint32_t)g_cluster->partition_count) return;
    
    PartitionInfo *p = &g_cluster->partitions[partition_id];
    free(p->range_start);
    free(p->range_end);
    p->range_start = start ? strdup(start) : NULL;
    p->range_end = end ? strdup(end) : NULL;
}

// ============================================================================
// AUTO-REBALANCING
// ============================================================================

bool rebalance_needed(void) {
    if (!g_cluster || g_cluster->node_count <= 1) return false;
    
    int min_partitions = INT32_MAX;
    int max_partitions = 0;
    
    for (int i = 0; i < g_cluster->node_count; i++) {
        ClusterNode *node = &g_cluster->nodes[i];
        if (node->status == NODE_STATUS_DEAD) continue;
        
        if (node->partition_count < min_partitions) {
            min_partitions = node->partition_count;
        }
        if (node->partition_count > max_partitions) {
            max_partitions = node->partition_count;
        }
    }
    
    if (min_partitions == 0) return true;
    
    int imbalance = ((max_partitions - min_partitions) * 100) / max_partitions;
    return imbalance > g_cluster->config.rebalance_threshold_percent;
}

int rebalance_partitions(void) {
    if (!g_cluster || g_cluster->current_role != NODE_ROLE_LEADER) return 0;
    
    LOG_INFO("Starting partition rebalancing");
    g_cluster->total_rebalances++;
    
    int healthy_nodes = 0;
    for (int i = 0; i < g_cluster->node_count; i++) {
        if (g_cluster->nodes[i].status != NODE_STATUS_DEAD) {
            healthy_nodes++;
        }
    }
    
    if (healthy_nodes == 0) return 0;
    
    int target_per_node = g_cluster->partition_count / healthy_nodes;
    int moved = 0;
    
    // Find overloaded and underloaded nodes
    for (int i = 0; i < g_cluster->node_count; i++) {
        ClusterNode *source = &g_cluster->nodes[i];
        if (source->status == NODE_STATUS_DEAD) continue;
        
        while (source->partition_count > target_per_node + 1) {
            // Find underloaded node
            for (int j = 0; j < g_cluster->node_count; j++) {
                ClusterNode *target = &g_cluster->nodes[j];
                if (target->status == NODE_STATUS_DEAD) continue;
                if (target->partition_count >= target_per_node) continue;
                
                // Move a partition
                uint32_t partition_id = source->partitions[source->partition_count - 1];
                source->partition_count--;
                
                partition_assign(partition_id, target->node_id, true);
                sync_partition(partition_id, target->node_id);
                moved++;
                
                LOG_DEBUG("Moved partition %u from node %u to node %u",
                          partition_id, source->node_id, target->node_id);
                break;
            }
        }
    }
    
    LOG_INFO("Rebalancing complete: moved %d partitions", moved);
    return moved;
}

void rebalance_on_join(ClusterNode *new_node) {
    if (!g_cluster || !new_node || g_cluster->current_role != NODE_ROLE_LEADER) return;
    
    LOG_INFO("Rebalancing for new node %s (%u)", new_node->name, new_node->node_id);
    rebalance_partitions();
}

void rebalance_on_leave(ClusterNode *left_node) {
    if (!g_cluster || !left_node || g_cluster->current_role != NODE_ROLE_LEADER) return;
    
    LOG_INFO("Rebalancing after node %s (%u) left", left_node->name, left_node->node_id);
    
    // Reassign partitions from left node
    for (int i = 0; i < left_node->partition_count; i++) {
        uint32_t partition_id = left_node->partitions[i];
        PartitionInfo *p = &g_cluster->partitions[partition_id];
        
        // Promote a replica to primary
        if (p->replica_count > 0) {
            p->primary_node = p->replica_nodes[0];
            memmove(p->replica_nodes, p->replica_nodes + 1, 
                    (p->replica_count - 1) * sizeof(uint32_t));
            p->replica_count--;
            
            LOG_INFO("Partition %u: promoted node %u to primary",
                     partition_id, p->primary_node);
        }
    }
    
    rebalance_partitions();
}

// ============================================================================
// NODE MANAGEMENT
// ============================================================================

int cluster_add_node(NodeAddress *address, const char *name) {
    if (!g_cluster) return -1;
    
    // Check capacity
    if (g_cluster->node_count >= g_cluster->node_capacity) {
        g_cluster->node_capacity *= 2;
        g_cluster->nodes = realloc(g_cluster->nodes,
                                   g_cluster->node_capacity * sizeof(ClusterNode));
    }
    
    ClusterNode *node = &g_cluster->nodes[g_cluster->node_count];
    memset(node, 0, sizeof(ClusterNode));
    node->node_id = generate_node_id();
    strncpy(node->name, name, sizeof(node->name) - 1);
    memcpy(&node->address, address, sizeof(NodeAddress));
    node->role = NODE_ROLE_FOLLOWER;
    node->status = NODE_STATUS_HEALTHY;
    node->join_time = time(NULL);
    node->last_heartbeat = time(NULL);
    
    g_cluster->node_count++;
    
    LOG_INFO("Added node %s (%u) at %s:%d",
             node->name, node->node_id, address->host, address->port);
    
    // Notify callback
    if (g_cluster->on_node_join) {
        g_cluster->on_node_join(node);
    }
    
    // Trigger rebalancing
    if (g_cluster->config.auto_rebalance && g_cluster->current_role == NODE_ROLE_LEADER) {
        rebalance_on_join(node);
    }
    
    return node->node_id;
}

int cluster_remove_node(uint32_t node_id) {
    if (!g_cluster) return -1;
    
    for (int i = 0; i < g_cluster->node_count; i++) {
        if (g_cluster->nodes[i].node_id == node_id) {
            ClusterNode *node = &g_cluster->nodes[i];
            
            // Notify callback
            if (g_cluster->on_node_leave) {
                g_cluster->on_node_leave(node);
            }
            
            LOG_INFO("Removing node %s (%u)", node->name, node->node_id);
            
            // Shift remaining nodes
            memmove(&g_cluster->nodes[i], &g_cluster->nodes[i + 1],
                    (g_cluster->node_count - i - 1) * sizeof(ClusterNode));
            g_cluster->node_count--;
            
            return 0;
        }
    }
    
    return -1;
}

ClusterNode* cluster_get_node(uint32_t node_id) {
    if (!g_cluster) return NULL;
    
    for (int i = 0; i < g_cluster->node_count; i++) {
        if (g_cluster->nodes[i].node_id == node_id) {
            return &g_cluster->nodes[i];
        }
    }
    
    return NULL;
}

ClusterNode* cluster_get_leader(void) {
    if (!g_cluster) return NULL;
    return cluster_get_node(g_cluster->current_leader);
}

bool cluster_is_leader(void) {
    return g_cluster && g_cluster->current_role == NODE_ROLE_LEADER;
}

int cluster_get_healthy_nodes(ClusterNode **out_nodes, int max_nodes) {
    if (!g_cluster || !out_nodes) return 0;
    
    int count = 0;
    for (int i = 0; i < g_cluster->node_count && count < max_nodes; i++) {
        if (g_cluster->nodes[i].status == NODE_STATUS_HEALTHY) {
            out_nodes[count++] = &g_cluster->nodes[i];
        }
    }
    
    return count;
}

// ============================================================================
// CLUSTER LIFECYCLE
// ============================================================================

int cluster_start(void) {
    if (!g_cluster) return -1;
    if (g_cluster->running) return 0;
    
    // Create listening socket
    g_cluster->listen_socket = net_create_listener(&g_cluster->config.self_address);
    if (g_cluster->listen_socket == INVALID_SOCK) {
        LOG_ERROR("Failed to create listener");
        return -1;
    }
    
    g_cluster->running = true;
    
    // Start heartbeat thread
#ifdef _WIN32
    g_cluster->heartbeat_thread = CreateThread(NULL, 0, heartbeat_thread_func, NULL, 0, NULL);
#else
    pthread_create((pthread_t*)&g_cluster->heartbeat_thread, NULL, 
                   heartbeat_thread_func, NULL);
#endif
    
    LOG_INFO("Cluster started: listening on %s:%d",
             g_cluster->config.self_address.host,
             g_cluster->config.self_address.port);
    
    return 0;
}

void cluster_stop(void) {
    if (!g_cluster || !g_cluster->running) return;
    
    g_cluster->running = false;
    
    // Wait for threads
#ifdef _WIN32
    if (g_cluster->heartbeat_thread) {
        WaitForSingleObject((HANDLE)g_cluster->heartbeat_thread, 5000);
        CloseHandle((HANDLE)g_cluster->heartbeat_thread);
    }
#else
    if (g_cluster->heartbeat_thread) {
        pthread_join((pthread_t)g_cluster->heartbeat_thread, NULL);
    }
#endif
    
    LOG_INFO("Cluster stopped");
}

int cluster_join(void) {
    if (!g_cluster) return -1;
    
    // Try to contact seed nodes
    for (int i = 0; i < g_cluster->config.seed_node_count; i++) {
        NodeAddress *seed = &g_cluster->config.seed_nodes[i];
        socket_t sock = net_connect(seed);
        
        if (sock != INVALID_SOCK) {
            // Send join request
            JoinRequest request;
            memset(&request, 0, sizeof(request));
            request.header.magic = CLUSTER_MAGIC;
            request.header.version = CLUSTER_PROTOCOL_VERSION;
            request.header.type = MSG_JOIN_REQUEST;
            request.header.sender_id = g_cluster->config.self_node_id;
            request.header.payload_len = sizeof(JoinRequest) - sizeof(ClusterMessageHeader);
            memcpy(&request.address, &g_cluster->config.self_address, sizeof(NodeAddress));
            strncpy(request.name, g_cluster->config.self_name, sizeof(request.name) - 1);
            
            cluster_send_message(sock, &request.header,
                            ((char*)&request) + sizeof(ClusterMessageHeader));
            
            // Wait for response
            ClusterMessageHeader resp_header;
            void *payload = NULL;
            if (net_receive_message(sock, &resp_header, &payload) > 0) {
                if (resp_header.type == MSG_JOIN_RESPONSE) {
                    LOG_INFO("Joined cluster via seed %s:%d", seed->host, seed->port);
                    free(payload);
                    closesocket(sock);
                    return 0;
                }
                free(payload);
            }
            closesocket(sock);
        }
    }
    
    LOG_WARN("Failed to join cluster via seed nodes - starting as standalone");
    return -1;
}

void cluster_leave(void) {
    if (!g_cluster) return;
    
    // Notify other nodes
    ClusterMessageHeader msg;
    memset(&msg, 0, sizeof(msg));
    msg.magic = CLUSTER_MAGIC;
    msg.version = CLUSTER_PROTOCOL_VERSION;
    msg.type = MSG_LEAVE_NOTIFY;
    msg.sender_id = g_cluster->config.self_node_id;
    msg.term = g_cluster->raft.current_term;
    
    net_broadcast(&msg, NULL);
    
    LOG_INFO("Left cluster");
}

// ============================================================================
// CLIENT MANAGEMENT
// ============================================================================

int client_pool_init(int max_clients) {
    g_client_pool = calloc(1, sizeof(ClientPool));
    g_client_pool->max_clients = max_clients;
    g_client_pool->client_capacity = 64;
    g_client_pool->clients = calloc(g_client_pool->client_capacity, sizeof(ClientConnection));
    
#ifdef _WIN32
    g_client_pool->lock = CreateMutex(NULL, FALSE, NULL);
#else
    g_client_pool->lock = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init((pthread_mutex_t*)g_client_pool->lock, NULL);
#endif
    
    return 0;
}

int client_accept(socket_t socket, NodeAddress *address) {
    if (!g_client_pool) return -1;
    
#ifdef _WIN32
    WaitForSingleObject((HANDLE)g_client_pool->lock, INFINITE);
#else
    pthread_mutex_lock((pthread_mutex_t*)g_client_pool->lock);
#endif
    
    if (g_client_pool->client_count >= g_client_pool->max_clients) {
#ifdef _WIN32
        ReleaseMutex((HANDLE)g_client_pool->lock);
#else
        pthread_mutex_unlock((pthread_mutex_t*)g_client_pool->lock);
#endif
        return -1;
    }
    
    // Find free slot
    int slot = -1;
    for (int i = 0; i < g_client_pool->client_capacity; i++) {
        if (g_client_pool->clients[i].socket == 0) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        // Grow capacity
        int old_cap = g_client_pool->client_capacity;
        g_client_pool->client_capacity *= 2;
        g_client_pool->clients = realloc(g_client_pool->clients,
                                         g_client_pool->client_capacity * sizeof(ClientConnection));
        memset(&g_client_pool->clients[old_cap], 0,
               (g_client_pool->client_capacity - old_cap) * sizeof(ClientConnection));
        slot = old_cap;
    }
    
    ClientConnection *client = &g_client_pool->clients[slot];
    client->client_id = generate_node_id();
    client->socket = socket;
    memcpy(&client->address, address, sizeof(NodeAddress));
    client->connect_time = time(NULL);
    client->last_activity = time(NULL);
    strcpy(client->current_db, "public");
    client->authenticated = false;
    
    g_client_pool->client_count++;
    
#ifdef _WIN32
    ReleaseMutex((HANDLE)g_client_pool->lock);
#else
    pthread_mutex_unlock((pthread_mutex_t*)g_client_pool->lock);
#endif
    
    LOG_INFO("Client %u connected from %s:%d", 
             client->client_id, address->host, address->port);
    
    return client->client_id;
}

void client_disconnect(uint32_t client_id) {
    if (!g_client_pool) return;
    
#ifdef _WIN32
    WaitForSingleObject((HANDLE)g_client_pool->lock, INFINITE);
#else
    pthread_mutex_lock((pthread_mutex_t*)g_client_pool->lock);
#endif
    
    for (int i = 0; i < g_client_pool->client_capacity; i++) {
        if (g_client_pool->clients[i].client_id == client_id) {
            ClientConnection *client = &g_client_pool->clients[i];
            LOG_INFO("Client %u disconnected", client_id);
            closesocket(client->socket);
            memset(client, 0, sizeof(ClientConnection));
            g_client_pool->client_count--;
            break;
        }
    }
    
#ifdef _WIN32
    ReleaseMutex((HANDLE)g_client_pool->lock);
#else
    pthread_mutex_unlock((pthread_mutex_t*)g_client_pool->lock);
#endif
}

ClientConnection* client_get(uint32_t client_id) {
    if (!g_client_pool) return NULL;
    
    for (int i = 0; i < g_client_pool->client_capacity; i++) {
        if (g_client_pool->clients[i].client_id == client_id) {
            return &g_client_pool->clients[i];
        }
    }
    
    return NULL;
}

int client_cleanup_inactive(int timeout_sec) {
    if (!g_client_pool) return 0;
    
    time_t now = time(NULL);
    int cleaned = 0;
    
    for (int i = 0; i < g_client_pool->client_capacity; i++) {
        ClientConnection *client = &g_client_pool->clients[i];
        if (client->socket != 0 && (now - client->last_activity) > timeout_sec) {
            client_disconnect(client->client_id);
            cleaned++;
        }
    }
    
    return cleaned;
}

// ============================================================================
// QUERY ROUTING
// ============================================================================

int cluster_route_query(const char *query, size_t query_len,
                        char *response, size_t response_capacity) {
    if (!g_cluster || !query || !response) return -1;
    (void)query_len;  // Silence unused parameter warning
    
    // If we're the leader, execute locally or route to appropriate partition
    if (g_cluster->current_role == NODE_ROLE_LEADER) {
        // For now, execute locally
        // TODO: Parse query to determine partition and route
        return cluster_execute_local(query, &response) == 0 ? (int)strlen(response) : -1;
    } else {
        // Forward to leader
        char *resp = NULL;
        int result = cluster_forward_to_leader(query, &resp);
        if (result == 0 && resp) {
            strncpy(response, resp, response_capacity - 1);
            free(resp);
            return strlen(response);
        }
        return -1;
    }
}

int cluster_forward_to_leader(const char *query, char **response) {
    if (!g_cluster || !query || !response) return -1;
    
    ClusterNode *leader = cluster_get_leader();
    if (!leader) {
        *response = strdup("Error: No leader available\n");
        return -1;
    }
    
    socket_t sock = net_connect(&leader->address);
    if (sock == INVALID_SOCK) {
        *response = strdup("Error: Cannot connect to leader\n");
        return -1;
    }
    
    // Send query
    QueryForward *msg = malloc(sizeof(QueryForward) + strlen(query) + 1);
    memset(msg, 0, sizeof(QueryForward));
    msg->header.magic = CLUSTER_MAGIC;
    msg->header.version = CLUSTER_PROTOCOL_VERSION;
    msg->header.type = MSG_QUERY_FORWARD;
    msg->header.sender_id = g_cluster->config.self_node_id;
    msg->header.receiver_id = leader->node_id;
    msg->header.term = g_cluster->raft.current_term;
    msg->query_len = strlen(query);
    msg->header.payload_len = sizeof(QueryForward) - sizeof(ClusterMessageHeader) + msg->query_len;
    
    send(sock, (const char*)msg, sizeof(QueryForward), 0);
    send(sock, query, strlen(query), 0);
    free(msg);
    
    // Receive response
    char buffer[8192];
    int total = 0;
    int len;
    
    while ((len = recv(sock, buffer + total, sizeof(buffer) - total - 1, 0)) > 0) {
        total += len;
        if (strstr(buffer, "<<EOF>>")) break;
    }
    buffer[total] = '\0';
    
    // Remove EOF marker
    char *marker = strstr(buffer, "<<EOF>>");
    if (marker) *marker = '\0';
    
    closesocket(sock);
    
    *response = strdup(buffer);
    return 0;
}

int cluster_execute_local(const char *query, char **response) {
    (void)query;
    // TODO: Integrate with executor
    *response = strdup("OK\n");
    return 0;
}

// ============================================================================
// STATUS & DEBUGGING
// ============================================================================

void cluster_print_status(void) {
    if (!g_cluster) {
        printf("Cluster not initialized\n");
        return;
    }
    
    printf("\n========== Cluster Status ==========\n");
    printf("Node ID: %u\n", g_cluster->config.self_node_id);
    printf("Name: %s\n", g_cluster->config.self_name);
    printf("Address: %s:%d\n", 
           g_cluster->config.self_address.host,
           g_cluster->config.self_address.port);
    printf("Role: %s\n",
           g_cluster->current_role == NODE_ROLE_LEADER ? "LEADER" :
           g_cluster->current_role == NODE_ROLE_CANDIDATE ? "CANDIDATE" : "FOLLOWER");
    printf("Term: %llu\n", (unsigned long long)g_cluster->raft.current_term);
    printf("Leader: %u\n", g_cluster->current_leader);
    printf("Running: %s\n", g_cluster->running ? "Yes" : "No");
    printf("\n--- Nodes (%d) ---\n", g_cluster->node_count);
    
    for (int i = 0; i < g_cluster->node_count; i++) {
        ClusterNode *node = &g_cluster->nodes[i];
        printf("  [%u] %s @ %s:%d - %s (%s)\n",
               node->node_id, node->name,
               node->address.host, node->address.port,
               node->role == NODE_ROLE_LEADER ? "LEADER" :
               node->role == NODE_ROLE_CANDIDATE ? "CANDIDATE" : "FOLLOWER",
               node->status == NODE_STATUS_HEALTHY ? "HEALTHY" :
               node->status == NODE_STATUS_DEGRADED ? "DEGRADED" :
               node->status == NODE_STATUS_UNREACHABLE ? "UNREACHABLE" : "DEAD");
    }
    
    printf("\n--- Partitions (%d) ---\n", g_cluster->partition_count);
    for (int i = 0; i < g_cluster->partition_count && i < 10; i++) {
        PartitionInfo *p = &g_cluster->partitions[i];
        printf("  [%u] Primary=%u, Replicas=%d\n",
               p->partition_id, p->primary_node, p->replica_count);
    }
    if (g_cluster->partition_count > 10) {
        printf("  ... and %d more\n", g_cluster->partition_count - 10);
    }
    
    printf("\n--- Statistics ---\n");
    printf("Total Elections: %llu\n", (unsigned long long)g_cluster->total_elections);
    printf("Total Rebalances: %llu\n", (unsigned long long)g_cluster->total_rebalances);
    printf("Uptime: %lld seconds\n", (long long)(time(NULL) - g_cluster->cluster_start_time));
    printf("=====================================\n\n");
}

void cluster_get_stats(char *buffer, size_t capacity) {
    if (!g_cluster || !buffer) return;
    
    int healthy = 0, total = 0;
    cluster_health_summary(&healthy, &total);
    
    snprintf(buffer, capacity,
             "Cluster Stats:\n"
             "  Node ID: %u\n"
             "  Role: %s\n"
             "  Term: %llu\n"
             "  Nodes: %d/%d healthy\n"
             "  Partitions: %d\n"
             "  Elections: %llu\n"
             "  Rebalances: %llu\n",
             g_cluster->config.self_node_id,
             g_cluster->current_role == NODE_ROLE_LEADER ? "LEADER" : "FOLLOWER",
             (unsigned long long)g_cluster->raft.current_term,
             healthy, total,
             g_cluster->partition_count,
             (unsigned long long)g_cluster->total_elections,
             (unsigned long long)g_cluster->total_rebalances);
}
