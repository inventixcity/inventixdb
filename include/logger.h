#ifndef INVENTIX_LOGGER_H
#define INVENTIX_LOGGER_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

// ============================================================================
// INVENTIX LOGGING SYSTEM
// ============================================================================
// Structured logging system with:
// - Multiple log levels (DEBUG, INFO, WARN, ERROR, FATAL)
// - Colored console output
// - File logging with rotation
// - Query audit logging
// - Performance timing
// ============================================================================

// ----------------------------------------------------------------------------
// Log Levels
// ----------------------------------------------------------------------------
typedef enum {
    LOG_LEVEL_DEBUG = 0,    // Detailed debugging information
    LOG_LEVEL_INFO  = 1,    // General operational messages
    LOG_LEVEL_WARN  = 2,    // Warning conditions
    LOG_LEVEL_ERROR = 3,    // Error conditions
    LOG_LEVEL_FATAL = 4,    // Critical errors (system will exit)
    LOG_LEVEL_OFF   = 5     // Disable all logging
} LogLevel;

// ----------------------------------------------------------------------------
// Log Categories (for filtering)
// ----------------------------------------------------------------------------
typedef enum {
    LOG_CAT_GENERAL   = 0x01,   // General messages
    LOG_CAT_STORAGE   = 0x02,   // Storage engine
    LOG_CAT_QUERY     = 0x04,   // Query execution
    LOG_CAT_NETWORK   = 0x08,   // Network/server
    LOG_CAT_AUTH      = 0x10,   // Authentication
    LOG_CAT_DIST      = 0x20,   // Distributed system
    LOG_CAT_PERF      = 0x40,   // Performance metrics
    LOG_CAT_ALL       = 0xFF    // All categories
} LogCategory;

// ----------------------------------------------------------------------------
// ANSI Color Codes for Console Output
// ----------------------------------------------------------------------------
#define LOG_COLOR_RESET     "\033[0m"
#define LOG_COLOR_BOLD      "\033[1m"
#define LOG_COLOR_DIM       "\033[2m"
#define LOG_COLOR_ITALIC    "\033[3m"
#define LOG_COLOR_UNDERLINE "\033[4m"

// Foreground Colors
#define LOG_COLOR_BLACK     "\033[30m"
#define LOG_COLOR_RED       "\033[31m"
#define LOG_COLOR_GREEN     "\033[32m"
#define LOG_COLOR_YELLOW    "\033[33m"
#define LOG_COLOR_BLUE      "\033[34m"
#define LOG_COLOR_MAGENTA   "\033[35m"
#define LOG_COLOR_CYAN      "\033[36m"
#define LOG_COLOR_WHITE     "\033[37m"

// Bright/Bold Colors
#define LOG_COLOR_BRED      "\033[91m"
#define LOG_COLOR_BGREEN    "\033[92m"
#define LOG_COLOR_BYELLOW   "\033[93m"
#define LOG_COLOR_BBLUE     "\033[94m"
#define LOG_COLOR_BMAGENTA  "\033[95m"
#define LOG_COLOR_BCYAN     "\033[96m"
#define LOG_COLOR_BWHITE    "\033[97m"

// Background Colors
#define LOG_BG_RED          "\033[41m"
#define LOG_BG_GREEN        "\033[42m"
#define LOG_BG_YELLOW       "\033[43m"
#define LOG_BG_BLUE         "\033[44m"

// ----------------------------------------------------------------------------
// Log Entry Structure
// ----------------------------------------------------------------------------
typedef struct {
    LogLevel level;
    LogCategory category;
    const char *file;
    int line;
    const char *func;
    time_t timestamp;
    char message[1024];
} LogEntry;

// ----------------------------------------------------------------------------
// Logger Configuration
// ----------------------------------------------------------------------------
typedef struct {
    LogLevel console_level;     // Minimum level for console output
    LogLevel file_level;        // Minimum level for file output
    int category_mask;          // Enabled categories (bitmask)
    int enable_colors;          // Enable ANSI colors in console
    int enable_file_logging;    // Enable file logging
    int enable_timestamps;      // Show timestamps
    int enable_source_loc;      // Show file:line
    char log_file[256];         // Log file path
    FILE *log_fp;               // Log file pointer
    long max_file_size;         // Max log file size before rotation
    int max_rotations;          // Number of rotated files to keep
    long current_size;          // Current log file size
} LoggerConfig;

// Global logger instance
extern LoggerConfig g_logger;

// ----------------------------------------------------------------------------
// Logger API
// ----------------------------------------------------------------------------

/**
 * Initialize the logging system
 */
void logger_init(void);

/**
 * Initialize logger with custom settings
 */
void logger_init_with_config(LogLevel console_level, LogLevel file_level, 
                             const char *log_file, int enable_colors);

/**
 * Shutdown the logging system
 */
void logger_shutdown(void);

/**
 * Set the minimum log level for console
 */
void logger_set_console_level(LogLevel level);

/**
 * Set the minimum log level for file
 */
void logger_set_file_level(LogLevel level);

/**
 * Enable/disable colors
 */
void logger_set_colors(int enabled);

/**
 * Enable/disable specific category
 */
void logger_set_category(LogCategory cat, int enabled);

/**
 * Core logging function (use macros instead)
 */
void logger_log(LogLevel level, LogCategory cat, const char *file, 
                int line, const char *func, const char *fmt, ...);

/**
 * Log a query for audit purposes
 */
void logger_query(const char *user, const char *db, const char *query, 
                  int success, double exec_time_ms);

/**
 * Log performance metrics
 */
void logger_perf(const char *operation, double duration_ms, 
                 int rows_affected, const char *details);

/**
 * Rotate log file if needed
 */
void logger_rotate(void);

/**
 * Flush log buffers
 */
void logger_flush(void);

/**
 * Get log level name
 */
const char* logger_level_name(LogLevel level);

/**
 * Get log level color
 */
const char* logger_level_color(LogLevel level);

// ----------------------------------------------------------------------------
// Convenience Macros
// ----------------------------------------------------------------------------

// Standard logging macros with source location
#define LOG_DEBUG(fmt, ...) \
    logger_log(LOG_LEVEL_DEBUG, LOG_CAT_GENERAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    logger_log(LOG_LEVEL_INFO, LOG_CAT_GENERAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    logger_log(LOG_LEVEL_WARN, LOG_CAT_GENERAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    logger_log(LOG_LEVEL_ERROR, LOG_CAT_GENERAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define LOG_FATAL(fmt, ...) \
    logger_log(LOG_LEVEL_FATAL, LOG_CAT_GENERAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

// Category-specific logging macros
#define LOG_STORAGE(level, fmt, ...) \
    logger_log(level, LOG_CAT_STORAGE, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define LOG_QUERY(level, fmt, ...) \
    logger_log(level, LOG_CAT_QUERY, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define LOG_NETWORK(level, fmt, ...) \
    logger_log(level, LOG_CAT_NETWORK, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define LOG_AUTH(level, fmt, ...) \
    logger_log(level, LOG_CAT_AUTH, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define LOG_DIST(level, fmt, ...) \
    logger_log(level, LOG_CAT_DIST, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define LOG_SECURITY(fmt, ...) \
    logger_log(LOG_LEVEL_WARN, LOG_CAT_AUTH, __FILE__, __LINE__, __func__, "[SECURITY] " fmt, ##__VA_ARGS__)

// Short aliases for common use
#define LOGD(fmt, ...) LOG_DEBUG(fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) LOG_INFO(fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LOG_WARN(fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) LOG_ERROR(fmt, ##__VA_ARGS__)
#define LOGF(fmt, ...) LOG_FATAL(fmt, ##__VA_ARGS__)

// ----------------------------------------------------------------------------
// Pretty Print Helpers
// ----------------------------------------------------------------------------

/**
 * Print a formatted header box
 */
void log_print_header(const char *title);

/**
 * Print a formatted section divider
 */
void log_print_divider(void);

/**
 * Print a key-value pair
 */
void log_print_kv(const char *key, const char *value);

/**
 * Print a success message
 */
void log_print_success(const char *message);

/**
 * Print an error message
 */
void log_print_error(const char *message);

/**
 * Print a warning message
 */
void log_print_warning(const char *message);

/**
 * Print a progress indicator
 */
void log_print_progress(const char *task, int current, int total);

/**
 * Print a table header
 */
void log_print_table_header(const char **columns, int count);

/**
 * Print a table row
 */
void log_print_table_row(const char **values, int count);

/**
 * Print table footer/separator
 */
void log_print_table_end(int col_count);

#endif // INVENTIX_LOGGER_H
