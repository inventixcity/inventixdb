#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "config.h"

// ============================================================================
// INVENTIX CONFIGURATION SYSTEM - IMPLEMENTATION
// ============================================================================

// Global configuration instance
InventixConfig g_config;

// ----------------------------------------------------------------------------
// Helper Functions
// ----------------------------------------------------------------------------

static void trim_whitespace(char *str) {
    if (!str) return;
    
    // Trim leading
    char *start = str;
    while (isspace((unsigned char)*start)) start++;
    
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    
    // Trim trailing
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
}

static void str_to_lower(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = tolower((unsigned char)str[i]);
    }
}

static int parse_bool(const char *value) {
    if (!value) return 0;
    char lower[16];
    strncpy(lower, value, 15);
    lower[15] = '\0';
    str_to_lower(lower);
    
    return (strcmp(lower, "true") == 0 || 
            strcmp(lower, "yes") == 0 || 
            strcmp(lower, "1") == 0 ||
            strcmp(lower, "on") == 0);
}

// ----------------------------------------------------------------------------
// Default Configuration Values
// ----------------------------------------------------------------------------

void config_init_defaults(InventixConfig *config) {
    if (!config) return;
    
    memset(config, 0, sizeof(InventixConfig));
    
    // Storage defaults
    config->storage.initial_capacity = 1024;
    config->storage.checkpoint_threshold_kb = 2048;  // 2MB
    strcpy(config->storage.log_file, "inventix.log");
    strcpy(config->storage.snap_file, "inventix.snap");
    strcpy(config->storage.data_dir, "./data");
    config->storage.skip_list_max_level = 6;
    config->storage.enable_aof = 1;
    config->storage.enable_snapshots = 1;
    config->storage.sync_on_write = 0;  // Async by default for speed
    
    // Buffer pool defaults
    config->buffer_pool.pool_size = 10;
    config->buffer_pool.page_size = 4096;
    config->buffer_pool.prefetch_enabled = 0;
    config->buffer_pool.eviction_policy = 0;  // LRU
    
    // Network defaults
    config->network.server_port = 8888;
    config->network.max_connections = 100;
    config->network.connection_timeout_ms = 30000;
    config->network.read_buffer_size = 4096;
    config->network.write_buffer_size = 4096;
    config->network.enable_tcp_nodelay = 1;
    config->network.backlog_size = 128;
    
    // Distributed defaults
    config->distributed.is_master = 0;
    config->distributed.worker_count = 2;
    config->distributed.replication_factor = 1;
    config->distributed.partition_strategy = 0;  // HASH
    config->distributed.heartbeat_interval_ms = 5000;
    config->distributed.worker_timeout_ms = 10000;
    config->distributed.enable_auto_failover = 0;
    
    // Default workers (can be overridden by config file)
    strcpy(config->distributed.workers[0].ip, "127.0.0.1");
    config->distributed.workers[0].port = 8889;
    config->distributed.workers[0].enabled = 1;
    strcpy(config->distributed.workers[0].name, "worker-1");
    
    strcpy(config->distributed.workers[1].ip, "127.0.0.1");
    config->distributed.workers[1].port = 8890;
    config->distributed.workers[1].enabled = 1;
    strcpy(config->distributed.workers[1].name, "worker-2");
    
    // Cluster defaults (Distributed System)
    strcpy(config->cluster.node_name, "node-1");
    config->cluster.node_id[0] = '\0';  // Auto-generate
    strcpy(config->cluster.cluster_name, "inventix-cluster");
    strcpy(config->cluster.bind_host, "0.0.0.0");
    config->cluster.bind_port = 9876;
    strcpy(config->cluster.advertise_host, "127.0.0.1");
    config->cluster.advertise_port = 9876;
    config->cluster.seed_node_count = 0;
    config->cluster.election_timeout_min_ms = 3000;
    config->cluster.election_timeout_max_ms = 5000;
    config->cluster.raft_log_max_entries = 10000;
    config->cluster.replication_factor = 2;
    config->cluster.replication_mode = 0;  // ASYNC
    config->cluster.min_sync_replicas = 1;
    config->cluster.partition_count = 16;
    config->cluster.partition_strategy = 0;  // HASH
    config->cluster.auto_rebalance = 1;
    config->cluster.rebalance_threshold_pct = 20;
    config->cluster.heartbeat_interval_ms = 1000;
    config->cluster.node_timeout_ms = 5000;
    config->cluster.health_check_interval_ms = 2000;
    config->cluster.max_clients = 1000;
    config->cluster.client_timeout_sec = 300;
    config->cluster.cluster_enabled = 0;  // Disabled by default

    // Security defaults
    config->security.enable_auth = 1;
    strcpy(config->security.default_admin_user, "admin");
    strcpy(config->security.default_admin_pass, "admin");  // Should be changed!
    config->security.password_min_length = 6;
    config->security.session_timeout_sec = 3600;
    config->security.max_login_attempts = 5;
    config->security.lockout_duration_sec = 300;
    
    // Logging defaults
    config->logging.log_level = 1;  // INFO
    strcpy(config->logging.log_file, "inventix_server.log");
    config->logging.max_log_size_mb = 100;
    config->logging.log_rotation_count = 5;
    config->logging.enable_query_logging = 0;
    config->logging.enable_slow_query_log = 1;
    config->logging.slow_query_threshold_ms = 1000;
    
    // Query defaults
    config->query.max_result_rows = 10000;
    config->query.query_timeout_sec = 60;
    config->query.enable_query_cache = 0;
    config->query.query_cache_size_mb = 64;
    config->query.max_memory_per_query_mb = 128;
    
    // Storage Engine defaults
    config->storage_engine.engine_type = 0;             // ROW_STORE
    config->storage_engine.compression_type = 0;        // NONE
    config->storage_engine.enable_slotted_pages = 1;    // Enable by default
    config->storage_engine.lsm_memtable_size_kb = 4096; // 4MB memtable
    config->storage_engine.lsm_level_count = 7;
    config->storage_engine.lsm_compaction_threshold = 4;
    config->storage_engine.column_chunk_size = 1024;
    config->storage_engine.enable_bloom_filter = 1;
    config->storage_engine.compression_threshold = 512;
    config->storage_engine.auto_compact = 1;
    
    config->config_loaded = 0;
}

// ----------------------------------------------------------------------------
// Configuration File Parser
// ----------------------------------------------------------------------------

static void parse_config_line(InventixConfig *config, const char *section, 
                              const char *key, const char *value) {
    if (!config || !section || !key || !value) return;
    
    // --- Storage Section ---
    if (strcmp(section, "storage") == 0) {
        if (strcmp(key, "initial_capacity") == 0) {
            config->storage.initial_capacity = atoi(value);
        } else if (strcmp(key, "checkpoint_threshold_kb") == 0) {
            config->storage.checkpoint_threshold_kb = atoi(value);
        } else if (strcmp(key, "log_file") == 0) {
            strncpy(config->storage.log_file, value, MAX_PATH_LENGTH - 1);
        } else if (strcmp(key, "snap_file") == 0) {
            strncpy(config->storage.snap_file, value, MAX_PATH_LENGTH - 1);
        } else if (strcmp(key, "data_dir") == 0) {
            strncpy(config->storage.data_dir, value, MAX_PATH_LENGTH - 1);
        } else if (strcmp(key, "skip_list_max_level") == 0) {
            config->storage.skip_list_max_level = atoi(value);
        } else if (strcmp(key, "enable_aof") == 0) {
            config->storage.enable_aof = parse_bool(value);
        } else if (strcmp(key, "enable_snapshots") == 0) {
            config->storage.enable_snapshots = parse_bool(value);
        } else if (strcmp(key, "sync_on_write") == 0) {
            config->storage.sync_on_write = parse_bool(value);
        }
    }
    // --- Buffer Pool Section ---
    else if (strcmp(section, "buffer_pool") == 0) {
        if (strcmp(key, "pool_size") == 0) {
            config->buffer_pool.pool_size = atoi(value);
        } else if (strcmp(key, "page_size") == 0) {
            config->buffer_pool.page_size = atoi(value);
        } else if (strcmp(key, "prefetch_enabled") == 0) {
            config->buffer_pool.prefetch_enabled = parse_bool(value);
        } else if (strcmp(key, "eviction_policy") == 0) {
            if (strcmp(value, "LRU") == 0) config->buffer_pool.eviction_policy = 0;
            else if (strcmp(value, "CLOCK") == 0) config->buffer_pool.eviction_policy = 1;
            else if (strcmp(value, "LFU") == 0) config->buffer_pool.eviction_policy = 2;
        }
    }
    // --- Network Section ---
    else if (strcmp(section, "network") == 0) {
        if (strcmp(key, "server_port") == 0) {
            config->network.server_port = atoi(value);
        } else if (strcmp(key, "max_connections") == 0) {
            config->network.max_connections = atoi(value);
        } else if (strcmp(key, "connection_timeout_ms") == 0) {
            config->network.connection_timeout_ms = atoi(value);
        } else if (strcmp(key, "read_buffer_size") == 0) {
            config->network.read_buffer_size = atoi(value);
        } else if (strcmp(key, "write_buffer_size") == 0) {
            config->network.write_buffer_size = atoi(value);
        } else if (strcmp(key, "enable_tcp_nodelay") == 0) {
            config->network.enable_tcp_nodelay = parse_bool(value);
        } else if (strcmp(key, "backlog_size") == 0) {
            config->network.backlog_size = atoi(value);
        }
    }
    // --- Distributed Section ---
    else if (strcmp(section, "distributed") == 0) {
        if (strcmp(key, "is_master") == 0) {
            config->distributed.is_master = parse_bool(value);
        } else if (strcmp(key, "worker_count") == 0) {
            config->distributed.worker_count = atoi(value);
        } else if (strcmp(key, "replication_factor") == 0) {
            config->distributed.replication_factor = atoi(value);
        } else if (strcmp(key, "partition_strategy") == 0) {
            if (strcmp(value, "HASH") == 0) config->distributed.partition_strategy = 0;
            else if (strcmp(value, "RANGE") == 0) config->distributed.partition_strategy = 1;
        } else if (strcmp(key, "heartbeat_interval_ms") == 0) {
            config->distributed.heartbeat_interval_ms = atoi(value);
        } else if (strcmp(key, "worker_timeout_ms") == 0) {
            config->distributed.worker_timeout_ms = atoi(value);
        } else if (strcmp(key, "enable_auto_failover") == 0) {
            config->distributed.enable_auto_failover = parse_bool(value);
        }
    }
    // --- Cluster Section (Distributed System) ---
    else if (strcmp(section, "cluster") == 0) {
        if (strcmp(key, "enabled") == 0) {
            config->cluster.cluster_enabled = parse_bool(value);
        } else if (strcmp(key, "node_name") == 0) {
            strncpy(config->cluster.node_name, value, 63);
        } else if (strcmp(key, "node_id") == 0) {
            strncpy(config->cluster.node_id, value, 63);
        } else if (strcmp(key, "cluster_name") == 0) {
            strncpy(config->cluster.cluster_name, value, 63);
        } else if (strcmp(key, "bind_host") == 0) {
            strncpy(config->cluster.bind_host, value, 63);
        } else if (strcmp(key, "bind_port") == 0) {
            config->cluster.bind_port = atoi(value);
        } else if (strcmp(key, "advertise_host") == 0) {
            strncpy(config->cluster.advertise_host, value, 63);
        } else if (strcmp(key, "advertise_port") == 0) {
            config->cluster.advertise_port = atoi(value);
        } else if (strcmp(key, "election_timeout_min_ms") == 0) {
            config->cluster.election_timeout_min_ms = atoi(value);
        } else if (strcmp(key, "election_timeout_max_ms") == 0) {
            config->cluster.election_timeout_max_ms = atoi(value);
        } else if (strcmp(key, "raft_log_max_entries") == 0) {
            config->cluster.raft_log_max_entries = atoi(value);
        } else if (strcmp(key, "replication_factor") == 0) {
            config->cluster.replication_factor = atoi(value);
        } else if (strcmp(key, "replication_mode") == 0) {
            if (strcmp(value, "ASYNC") == 0) config->cluster.replication_mode = 0;
            else if (strcmp(value, "SYNC") == 0) config->cluster.replication_mode = 1;
            else if (strcmp(value, "SYNC_ALL") == 0) config->cluster.replication_mode = 2;
        } else if (strcmp(key, "min_sync_replicas") == 0) {
            config->cluster.min_sync_replicas = atoi(value);
        } else if (strcmp(key, "partition_count") == 0) {
            config->cluster.partition_count = atoi(value);
        } else if (strcmp(key, "partition_strategy") == 0) {
            if (strcmp(value, "HASH") == 0) config->cluster.partition_strategy = 0;
            else if (strcmp(value, "RANGE") == 0) config->cluster.partition_strategy = 1;
        } else if (strcmp(key, "auto_rebalance") == 0) {
            config->cluster.auto_rebalance = parse_bool(value);
        } else if (strcmp(key, "rebalance_threshold_pct") == 0) {
            config->cluster.rebalance_threshold_pct = atoi(value);
        } else if (strcmp(key, "heartbeat_interval_ms") == 0) {
            config->cluster.heartbeat_interval_ms = atoi(value);
        } else if (strcmp(key, "node_timeout_ms") == 0) {
            config->cluster.node_timeout_ms = atoi(value);
        } else if (strcmp(key, "health_check_interval_ms") == 0) {
            config->cluster.health_check_interval_ms = atoi(value);
        } else if (strcmp(key, "max_clients") == 0) {
            config->cluster.max_clients = atoi(value);
        } else if (strcmp(key, "client_timeout_sec") == 0) {
            config->cluster.client_timeout_sec = atoi(value);
        }
    }
    // --- Seed Nodes (seed.0, seed.1, etc.) ---
    else if (strncmp(section, "seed.", 5) == 0) {
        int idx = atoi(section + 5);
        if (idx >= 0 && idx < 8) {
            if (strcmp(key, "address") == 0) {
                strncpy(config->cluster.seed_nodes[idx], value, 127);
                if (idx >= config->cluster.seed_node_count) {
                    config->cluster.seed_node_count = idx + 1;
                }
            }
        }
    }
    // --- Worker Configuration (worker.0, worker.1, etc.) ---
    else if (strncmp(section, "worker.", 7) == 0) {
        int idx = atoi(section + 7);
        if (idx >= 0 && idx < MAX_CONFIG_WORKERS) {
            if (strcmp(key, "ip") == 0) {
                strncpy(config->distributed.workers[idx].ip, value, 63);
            } else if (strcmp(key, "port") == 0) {
                config->distributed.workers[idx].port = atoi(value);
            } else if (strcmp(key, "enabled") == 0) {
                config->distributed.workers[idx].enabled = parse_bool(value);
            } else if (strcmp(key, "name") == 0) {
                strncpy(config->distributed.workers[idx].name, value, 31);
            }
        }
    }
    // --- Security Section ---
    else if (strcmp(section, "security") == 0) {
        if (strcmp(key, "enable_auth") == 0) {
            config->security.enable_auth = parse_bool(value);
        } else if (strcmp(key, "default_admin_user") == 0) {
            strncpy(config->security.default_admin_user, value, 63);
        } else if (strcmp(key, "default_admin_pass") == 0) {
            strncpy(config->security.default_admin_pass, value, 63);
        } else if (strcmp(key, "password_min_length") == 0) {
            config->security.password_min_length = atoi(value);
        } else if (strcmp(key, "session_timeout_sec") == 0) {
            config->security.session_timeout_sec = atoi(value);
        } else if (strcmp(key, "max_login_attempts") == 0) {
            config->security.max_login_attempts = atoi(value);
        } else if (strcmp(key, "lockout_duration_sec") == 0) {
            config->security.lockout_duration_sec = atoi(value);
        }
    }
    // --- Logging Section ---
    else if (strcmp(section, "logging") == 0) {
        if (strcmp(key, "log_level") == 0) {
            if (strcmp(value, "DEBUG") == 0) config->logging.log_level = 0;
            else if (strcmp(value, "INFO") == 0) config->logging.log_level = 1;
            else if (strcmp(value, "WARN") == 0) config->logging.log_level = 2;
            else if (strcmp(value, "ERROR") == 0) config->logging.log_level = 3;
            else config->logging.log_level = atoi(value);
        } else if (strcmp(key, "log_file") == 0) {
            strncpy(config->logging.log_file, value, MAX_PATH_LENGTH - 1);
        } else if (strcmp(key, "max_log_size_mb") == 0) {
            config->logging.max_log_size_mb = atoi(value);
        } else if (strcmp(key, "log_rotation_count") == 0) {
            config->logging.log_rotation_count = atoi(value);
        } else if (strcmp(key, "enable_query_logging") == 0) {
            config->logging.enable_query_logging = parse_bool(value);
        } else if (strcmp(key, "enable_slow_query_log") == 0) {
            config->logging.enable_slow_query_log = parse_bool(value);
        } else if (strcmp(key, "slow_query_threshold_ms") == 0) {
            config->logging.slow_query_threshold_ms = atoi(value);
        }
    }
    // --- Query Section ---
    else if (strcmp(section, "query") == 0) {
        if (strcmp(key, "max_result_rows") == 0) {
            config->query.max_result_rows = atoi(value);
        } else if (strcmp(key, "query_timeout_sec") == 0) {
            config->query.query_timeout_sec = atoi(value);
        } else if (strcmp(key, "enable_query_cache") == 0) {
            config->query.enable_query_cache = parse_bool(value);
        } else if (strcmp(key, "query_cache_size_mb") == 0) {
            config->query.query_cache_size_mb = atoi(value);
        } else if (strcmp(key, "max_memory_per_query_mb") == 0) {
            config->query.max_memory_per_query_mb = atoi(value);
        }
    }
    // --- Storage Engine Section ---
    else if (strcmp(section, "storage_engine") == 0) {
        if (strcmp(key, "engine_type") == 0) {
            if (strcmp(value, "ROW_STORE") == 0) config->storage_engine.engine_type = 0;
            else if (strcmp(value, "COLUMN_STORE") == 0) config->storage_engine.engine_type = 1;
            else if (strcmp(value, "LSM_TREE") == 0) config->storage_engine.engine_type = 2;
            else config->storage_engine.engine_type = atoi(value);
        } else if (strcmp(key, "compression_type") == 0) {
            if (strcmp(value, "NONE") == 0) config->storage_engine.compression_type = 0;
            else if (strcmp(value, "LZ4") == 0) config->storage_engine.compression_type = 1;
            else if (strcmp(value, "RLE") == 0) config->storage_engine.compression_type = 2;
            else config->storage_engine.compression_type = atoi(value);
        } else if (strcmp(key, "enable_slotted_pages") == 0) {
            config->storage_engine.enable_slotted_pages = parse_bool(value);
        } else if (strcmp(key, "lsm_memtable_size_kb") == 0) {
            config->storage_engine.lsm_memtable_size_kb = atoi(value);
        } else if (strcmp(key, "lsm_level_count") == 0) {
            config->storage_engine.lsm_level_count = atoi(value);
        } else if (strcmp(key, "lsm_compaction_threshold") == 0) {
            config->storage_engine.lsm_compaction_threshold = atoi(value);
        } else if (strcmp(key, "column_chunk_size") == 0) {
            config->storage_engine.column_chunk_size = atoi(value);
        } else if (strcmp(key, "enable_bloom_filter") == 0) {
            config->storage_engine.enable_bloom_filter = parse_bool(value);
        } else if (strcmp(key, "compression_threshold") == 0) {
            config->storage_engine.compression_threshold = atoi(value);
        } else if (strcmp(key, "auto_compact") == 0) {
            config->storage_engine.auto_compact = parse_bool(value);
        }
    }
}

int config_load(InventixConfig *config, const char *filepath) {
    if (!config || !filepath) return 0;
    
    // Initialize with defaults first
    config_init_defaults(config);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        printf("[Config] Warning: Could not open '%s', using defaults.\n", filepath);
        return 0;
    }
    
    strncpy(config->config_file, filepath, MAX_PATH_LENGTH - 1);
    
    char line[MAX_CONFIG_LINE];
    char current_section[64] = "global";
    
    int line_num = 0;
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        trim_whitespace(line);
        
        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // Section header [section]
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) {
                *end = '\0';
                strncpy(current_section, line + 1, 63);
                current_section[63] = '\0';
                str_to_lower(current_section);
            }
            continue;
        }
        
        // Key = Value
        char *equals = strchr(line, '=');
        if (equals) {
            *equals = '\0';
            char *key = line;
            char *value = equals + 1;
            
            trim_whitespace(key);
            trim_whitespace(value);
            
            // Remove quotes from value
            if (value[0] == '"' || value[0] == '\'') {
                value++;
                size_t len = strlen(value);
                if (len > 0 && (value[len-1] == '"' || value[len-1] == '\'')) {
                    value[len-1] = '\0';
                }
            }
            
            str_to_lower(key);
            parse_config_line(config, current_section, key, value);
        }
    }
    
    fclose(fp);
    config->config_loaded = 1;
    printf("[Config] Loaded configuration from '%s'\n", filepath);
    
    return 1;
}

// ----------------------------------------------------------------------------
// Configuration Save
// ----------------------------------------------------------------------------

int config_save(const InventixConfig *config, const char *filepath) {
    if (!config || !filepath) return 0;
    
    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        printf("[Config] Error: Could not write to '%s'\n", filepath);
        return 0;
    }
    
    fprintf(fp, "# ============================================================================\n");
    fprintf(fp, "# InventixDB Configuration File\n");
    fprintf(fp, "# Generated automatically - Edit with care\n");
    fprintf(fp, "# ============================================================================\n\n");
    
    // Storage Section
    fprintf(fp, "[storage]\n");
    fprintf(fp, "initial_capacity = %d\n", config->storage.initial_capacity);
    fprintf(fp, "checkpoint_threshold_kb = %d\n", config->storage.checkpoint_threshold_kb);
    fprintf(fp, "log_file = \"%s\"\n", config->storage.log_file);
    fprintf(fp, "snap_file = \"%s\"\n", config->storage.snap_file);
    fprintf(fp, "data_dir = \"%s\"\n", config->storage.data_dir);
    fprintf(fp, "skip_list_max_level = %d\n", config->storage.skip_list_max_level);
    fprintf(fp, "enable_aof = %s\n", config->storage.enable_aof ? "true" : "false");
    fprintf(fp, "enable_snapshots = %s\n", config->storage.enable_snapshots ? "true" : "false");
    fprintf(fp, "sync_on_write = %s\n", config->storage.sync_on_write ? "true" : "false");
    fprintf(fp, "\n");
    
    // Buffer Pool Section
    fprintf(fp, "[buffer_pool]\n");
    fprintf(fp, "pool_size = %d\n", config->buffer_pool.pool_size);
    fprintf(fp, "page_size = %d\n", config->buffer_pool.page_size);
    fprintf(fp, "prefetch_enabled = %s\n", config->buffer_pool.prefetch_enabled ? "true" : "false");
    const char *eviction_names[] = {"LRU", "CLOCK", "LFU"};
    fprintf(fp, "eviction_policy = %s\n", eviction_names[config->buffer_pool.eviction_policy % 3]);
    fprintf(fp, "\n");
    
    // Network Section
    fprintf(fp, "[network]\n");
    fprintf(fp, "server_port = %d\n", config->network.server_port);
    fprintf(fp, "max_connections = %d\n", config->network.max_connections);
    fprintf(fp, "connection_timeout_ms = %d\n", config->network.connection_timeout_ms);
    fprintf(fp, "read_buffer_size = %d\n", config->network.read_buffer_size);
    fprintf(fp, "write_buffer_size = %d\n", config->network.write_buffer_size);
    fprintf(fp, "enable_tcp_nodelay = %s\n", config->network.enable_tcp_nodelay ? "true" : "false");
    fprintf(fp, "backlog_size = %d\n", config->network.backlog_size);
    fprintf(fp, "\n");
    
    // Distributed Section
    fprintf(fp, "[distributed]\n");
    fprintf(fp, "is_master = %s\n", config->distributed.is_master ? "true" : "false");
    fprintf(fp, "worker_count = %d\n", config->distributed.worker_count);
    fprintf(fp, "replication_factor = %d\n", config->distributed.replication_factor);
    fprintf(fp, "partition_strategy = %s\n", config->distributed.partition_strategy == 0 ? "HASH" : "RANGE");
    fprintf(fp, "heartbeat_interval_ms = %d\n", config->distributed.heartbeat_interval_ms);
    fprintf(fp, "worker_timeout_ms = %d\n", config->distributed.worker_timeout_ms);
    fprintf(fp, "enable_auto_failover = %s\n", config->distributed.enable_auto_failover ? "true" : "false");
    fprintf(fp, "\n");
    
    // Worker Configurations
    for (int i = 0; i < config->distributed.worker_count && i < MAX_CONFIG_WORKERS; i++) {
        fprintf(fp, "[worker.%d]\n", i);
        fprintf(fp, "ip = \"%s\"\n", config->distributed.workers[i].ip);
        fprintf(fp, "port = %d\n", config->distributed.workers[i].port);
        fprintf(fp, "enabled = %s\n", config->distributed.workers[i].enabled ? "true" : "false");
        fprintf(fp, "name = \"%s\"\n", config->distributed.workers[i].name);
        fprintf(fp, "\n");
    }
    
    // Security Section
    fprintf(fp, "[security]\n");
    fprintf(fp, "enable_auth = %s\n", config->security.enable_auth ? "true" : "false");
    fprintf(fp, "default_admin_user = \"%s\"\n", config->security.default_admin_user);
    fprintf(fp, "# default_admin_pass = \"****\" # Hidden for security\n");
    fprintf(fp, "password_min_length = %d\n", config->security.password_min_length);
    fprintf(fp, "session_timeout_sec = %d\n", config->security.session_timeout_sec);
    fprintf(fp, "max_login_attempts = %d\n", config->security.max_login_attempts);
    fprintf(fp, "lockout_duration_sec = %d\n", config->security.lockout_duration_sec);
    fprintf(fp, "\n");
    
    // Logging Section
    fprintf(fp, "[logging]\n");
    const char *log_levels[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    fprintf(fp, "log_level = %s\n", log_levels[config->logging.log_level % 4]);
    fprintf(fp, "log_file = \"%s\"\n", config->logging.log_file);
    fprintf(fp, "max_log_size_mb = %d\n", config->logging.max_log_size_mb);
    fprintf(fp, "log_rotation_count = %d\n", config->logging.log_rotation_count);
    fprintf(fp, "enable_query_logging = %s\n", config->logging.enable_query_logging ? "true" : "false");
    fprintf(fp, "enable_slow_query_log = %s\n", config->logging.enable_slow_query_log ? "true" : "false");
    fprintf(fp, "slow_query_threshold_ms = %d\n", config->logging.slow_query_threshold_ms);
    fprintf(fp, "\n");
    
    // Query Section
    fprintf(fp, "[query]\n");
    fprintf(fp, "max_result_rows = %d\n", config->query.max_result_rows);
    fprintf(fp, "query_timeout_sec = %d\n", config->query.query_timeout_sec);
    fprintf(fp, "enable_query_cache = %s\n", config->query.enable_query_cache ? "true" : "false");
    fprintf(fp, "query_cache_size_mb = %d\n", config->query.query_cache_size_mb);
    fprintf(fp, "max_memory_per_query_mb = %d\n", config->query.max_memory_per_query_mb);
    
    fclose(fp);
    printf("[Config] Saved configuration to '%s'\n", filepath);
    return 1;
}

// ----------------------------------------------------------------------------
// Configuration Validation
// ----------------------------------------------------------------------------

int config_validate(const InventixConfig *config) {
    if (!config) return 0;
    
    int valid = 1;
    
    // Validate storage
    if (config->storage.initial_capacity < 16) {
        printf("[Config] Warning: initial_capacity too low, minimum is 16\n");
        valid = 0;
    }
    
    // Validate buffer pool
    if (config->buffer_pool.pool_size < 2) {
        printf("[Config] Warning: pool_size too low, minimum is 2\n");
        valid = 0;
    }
    if (config->buffer_pool.page_size < 512) {
        printf("[Config] Warning: page_size too low, minimum is 512\n");
        valid = 0;
    }
    
    // Validate network
    if (config->network.server_port < 1 || config->network.server_port > 65535) {
        printf("[Config] Error: server_port must be between 1 and 65535\n");
        valid = 0;
    }
    if (config->network.max_connections < 1) {
        printf("[Config] Warning: max_connections must be at least 1\n");
        valid = 0;
    }
    
    // Validate distributed
    if (config->distributed.worker_count < 0 || config->distributed.worker_count > MAX_CONFIG_WORKERS) {
        printf("[Config] Error: worker_count must be between 0 and %d\n", MAX_CONFIG_WORKERS);
        valid = 0;
    }
    
    // Validate security
    if (config->security.password_min_length < 1) {
        printf("[Config] Warning: password_min_length should be at least 1\n");
    }
    
    return valid;
}

// ----------------------------------------------------------------------------
// Configuration Print (Debug)
// ----------------------------------------------------------------------------

void config_print(const InventixConfig *config) {
    if (!config) return;
    
    printf("\n============ InventixDB Configuration ============\n");
    printf("Config File: %s\n", config->config_file[0] ? config->config_file : "(defaults)");
    printf("Loaded: %s\n\n", config->config_loaded ? "Yes" : "No (using defaults)");
    
    printf("[Storage]\n");
    printf("  initial_capacity: %d\n", config->storage.initial_capacity);
    printf("  checkpoint_threshold_kb: %d\n", config->storage.checkpoint_threshold_kb);
    printf("  log_file: %s\n", config->storage.log_file);
    printf("  snap_file: %s\n", config->storage.snap_file);
    printf("  skip_list_max_level: %d\n", config->storage.skip_list_max_level);
    
    printf("\n[Buffer Pool]\n");
    printf("  pool_size: %d pages\n", config->buffer_pool.pool_size);
    printf("  page_size: %d bytes\n", config->buffer_pool.page_size);
    
    printf("\n[Network]\n");
    printf("  server_port: %d\n", config->network.server_port);
    printf("  max_connections: %d\n", config->network.max_connections);
    
    printf("\n[Distributed]\n");
    printf("  is_master: %s\n", config->distributed.is_master ? "Yes" : "No");
    printf("  worker_count: %d\n", config->distributed.worker_count);
    for (int i = 0; i < config->distributed.worker_count && i < MAX_CONFIG_WORKERS; i++) {
        printf("    Worker %d: %s:%d (%s) [%s]\n", i,
               config->distributed.workers[i].ip,
               config->distributed.workers[i].port,
               config->distributed.workers[i].name,
               config->distributed.workers[i].enabled ? "enabled" : "disabled");
    }
    
    printf("\n[Security]\n");
    printf("  enable_auth: %s\n", config->security.enable_auth ? "Yes" : "No");
    printf("  default_admin_user: %s\n", config->security.default_admin_user);
    
    printf("\n[Logging]\n");
    const char *levels[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    printf("  log_level: %s\n", levels[config->logging.log_level % 4]);
    printf("  log_file: %s\n", config->logging.log_file);
    
    printf("===================================================\n\n");
}

// ----------------------------------------------------------------------------
// Global Config Reload
// ----------------------------------------------------------------------------

int config_reload(void) {
    if (g_config.config_file[0] == '\0') {
        printf("[Config] No config file loaded, cannot reload.\n");
        return 0;
    }
    return config_load(&g_config, g_config.config_file);
}
