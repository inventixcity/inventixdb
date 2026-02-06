/**
 * InventixDB Backup & Restore System
 * 
 * Features:
 * - Full database backup (BACKUP / SURAKSHA)
 * - Point-in-time restore (RESTORE / WAPAS_LAO)
 * - Table export (EXPORT / BHEJO)
 * - Table import (IMPORT / LAAO)
 * - Multiple formats: Binary, SQL, JSON, CSV
 * - Compression support
 */

#ifndef INVENTIX_BACKUP_H
#define INVENTIX_BACKUP_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "storage.h"

// Backup format types
typedef enum {
    BACKUP_FORMAT_BINARY = 0,   // Native binary format (fastest)
    BACKUP_FORMAT_SQL    = 1,   // SQL dump format
    BACKUP_FORMAT_JSON   = 2,   // JSON format
    BACKUP_FORMAT_CSV    = 3    // CSV format (tables only)
} BackupFormat;

// Compression types
typedef enum {
    BACKUP_COMPRESS_NONE = 0,
    BACKUP_COMPRESS_GZIP = 1
} BackupCompression;

// Backup header structure
typedef struct {
    char magic[8];              // "INVXBKP\0"
    uint32_t version;           // Backup format version
    uint32_t format;            // BackupFormat
    uint32_t compression;       // BackupCompression
    uint64_t timestamp;         // Backup creation time
    uint64_t db_count;          // Number of databases
    uint64_t table_count;       // Total tables
    uint64_t row_count;         // Total rows
    uint64_t data_size;         // Uncompressed data size
    char checksum[64];          // SHA-256 checksum
} BackupHeader;

// Backup options
typedef struct {
    BackupFormat format;
    BackupCompression compression;
    bool include_schema;        // Include CREATE TABLE statements
    bool include_data;          // Include INSERT statements
    bool include_indexes;       // Include CREATE INDEX statements
    bool include_users;         // Include user data
    char *specific_db;          // NULL = all databases
    char *specific_table;       // NULL = all tables
    bool verbose;               // Print progress
    FILE *log_file;             // Optional log file
} BackupOptions;

// Restore options
typedef struct {
    bool drop_existing;         // Drop existing tables before restore
    bool ignore_errors;         // Continue on errors
    bool restore_users;         // Restore user data
    char *target_db;            // Override target database name
    bool verbose;               // Print progress
    FILE *log_file;             // Optional log file
} RestoreOptions;

// Backup result
typedef struct {
    bool success;
    uint64_t tables_backed;
    uint64_t rows_backed;
    uint64_t bytes_written;
    double elapsed_seconds;
    char error_message[256];
} BackupResult;

// Restore result
typedef struct {
    bool success;
    uint64_t tables_restored;
    uint64_t rows_restored;
    uint64_t bytes_read;
    double elapsed_seconds;
    int errors_count;
    char error_message[256];
} RestoreResult;

// -----------------------------------------------------------------------------
// Backup Functions
// -----------------------------------------------------------------------------

/**
 * Backup entire database or specific tables
 * 
 * Hinglish: SURAKSHA DATABASE "path/to/backup.invx"
 *           SURAKSHA TABLE users "path/to/users.json" JSON
 */
BackupResult backup_database(KVStore *store, const char *path, BackupOptions *opts);

/**
 * Export single table to file
 * 
 * Hinglish: BHEJO users "users.csv" CSV
 *           BHEJO orders "orders.json" JSON
 */
BackupResult backup_export_table(KVStore *store, const char *db, 
                                  const char *table, const char *path,
                                  BackupFormat format);

// -----------------------------------------------------------------------------
// Restore Functions
// -----------------------------------------------------------------------------

/**
 * Restore database from backup
 * 
 * Hinglish: WAPAS_LAO DATABASE "path/to/backup.invx"
 */
RestoreResult restore_database(KVStore *store, const char *path, RestoreOptions *opts);

/**
 * Import table from file
 * 
 * Hinglish: LAAO users "users.csv" CSV
 *           LAAO orders "orders.json" JSON
 */
RestoreResult restore_import_table(KVStore *store, const char *db,
                                    const char *table, const char *path,
                                    BackupFormat format);

// -----------------------------------------------------------------------------
// Helper Functions
// -----------------------------------------------------------------------------

/**
 * Get backup info without restoring
 */
int backup_get_info(const char *path, BackupHeader *header);

/**
 * Verify backup integrity
 */
bool backup_verify(const char *path);

/**
 * List contents of backup file
 */
void backup_list_contents(const char *path, FILE *out);

/**
 * Initialize backup options with defaults
 */
void backup_options_init(BackupOptions *opts);

/**
 * Initialize restore options with defaults
 */
void restore_options_init(RestoreOptions *opts);

#endif // INVENTIX_BACKUP_H
