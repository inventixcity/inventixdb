#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "logger.h"
#include "config.h"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

// ============================================================================
// INVENTIX LOGGING SYSTEM - IMPLEMENTATION
// ============================================================================

// Global logger instance
LoggerConfig g_logger;

// ----------------------------------------------------------------------------
// Windows Console Color Support
// ----------------------------------------------------------------------------
#ifdef _WIN32
static int g_win_colors_enabled = 0;

static void enable_windows_ansi(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    
    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) return;
    
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    g_win_colors_enabled = 1;
}
#endif

// ----------------------------------------------------------------------------
// Logger Initialization
// ----------------------------------------------------------------------------

void logger_init(void) {
    memset(&g_logger, 0, sizeof(LoggerConfig));
    
    // Load from global config if available
    if (g_config.config_loaded) {
        g_logger.console_level = (LogLevel)CFG_LOGGING.log_level;
        g_logger.file_level = (LogLevel)CFG_LOGGING.log_level;
        strncpy(g_logger.log_file, CFG_LOGGING.log_file, 255);
        g_logger.max_file_size = CFG_LOGGING.max_log_size_mb * 1024 * 1024;
        g_logger.max_rotations = CFG_LOGGING.log_rotation_count;
        g_logger.enable_file_logging = 1;
    } else {
        // Defaults
        g_logger.console_level = LOG_LEVEL_INFO;
        g_logger.file_level = LOG_LEVEL_DEBUG;
        strcpy(g_logger.log_file, "inventix_server.log");
        g_logger.max_file_size = 100 * 1024 * 1024; // 100MB
        g_logger.max_rotations = 5;
        g_logger.enable_file_logging = 0; // Off by default for CLI
    }
    
    g_logger.category_mask = LOG_CAT_ALL;
    g_logger.enable_colors = 1;
    g_logger.enable_timestamps = 1;
    g_logger.enable_source_loc = 0; // Off by default (verbose)
    g_logger.log_fp = NULL;
    g_logger.current_size = 0;
    
    // Enable ANSI colors on Windows
#ifdef _WIN32
    enable_windows_ansi();
#endif
    
    // Open log file if enabled
    if (g_logger.enable_file_logging && g_logger.log_file[0]) {
        g_logger.log_fp = fopen(g_logger.log_file, "a");
        if (g_logger.log_fp) {
            fseek(g_logger.log_fp, 0, SEEK_END);
            g_logger.current_size = ftell(g_logger.log_fp);
        }
    }
}

void logger_init_with_config(LogLevel console_level, LogLevel file_level,
                             const char *log_file, int enable_colors) {
    logger_init();
    g_logger.console_level = console_level;
    g_logger.file_level = file_level;
    g_logger.enable_colors = enable_colors;
    
    if (log_file && log_file[0]) {
        strncpy(g_logger.log_file, log_file, 255);
        g_logger.enable_file_logging = 1;
        
        if (g_logger.log_fp) fclose(g_logger.log_fp);
        g_logger.log_fp = fopen(g_logger.log_file, "a");
    }
}

void logger_shutdown(void) {
    if (g_logger.log_fp) {
        fflush(g_logger.log_fp);
        fclose(g_logger.log_fp);
        g_logger.log_fp = NULL;
    }
}

// ----------------------------------------------------------------------------
// Configuration
// ----------------------------------------------------------------------------

void logger_set_console_level(LogLevel level) {
    g_logger.console_level = level;
}

void logger_set_file_level(LogLevel level) {
    g_logger.file_level = level;
}

void logger_set_colors(int enabled) {
    g_logger.enable_colors = enabled;
}

void logger_set_category(LogCategory cat, int enabled) {
    if (enabled) {
        g_logger.category_mask |= cat;
    } else {
        g_logger.category_mask &= ~cat;
    }
}

// ----------------------------------------------------------------------------
// Level Names and Colors
// ----------------------------------------------------------------------------

const char* logger_level_name(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO ";
        case LOG_LEVEL_WARN:  return "WARN ";
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_FATAL: return "FATAL";
        default:              return "?????";
    }
}

const char* logger_level_color(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return LOG_COLOR_DIM LOG_COLOR_CYAN;
        case LOG_LEVEL_INFO:  return LOG_COLOR_BGREEN;
        case LOG_LEVEL_WARN:  return LOG_COLOR_BYELLOW;
        case LOG_LEVEL_ERROR: return LOG_COLOR_BRED;
        case LOG_LEVEL_FATAL: return LOG_COLOR_BOLD LOG_BG_RED LOG_COLOR_WHITE;
        default:              return LOG_COLOR_WHITE;
    }
}

static const char* category_name(LogCategory cat) {
    switch (cat) {
        case LOG_CAT_STORAGE: return "STORAGE";
        case LOG_CAT_QUERY:   return "QUERY  ";
        case LOG_CAT_NETWORK: return "NETWORK";
        case LOG_CAT_AUTH:    return "AUTH   ";
        case LOG_CAT_DIST:    return "DIST   ";
        case LOG_CAT_PERF:    return "PERF   ";
        default:              return "GENERAL";
    }
}

static const char* category_color(LogCategory cat) {
    switch (cat) {
        case LOG_CAT_STORAGE: return LOG_COLOR_BLUE;
        case LOG_CAT_QUERY:   return LOG_COLOR_MAGENTA;
        case LOG_CAT_NETWORK: return LOG_COLOR_CYAN;
        case LOG_CAT_AUTH:    return LOG_COLOR_YELLOW;
        case LOG_CAT_DIST:    return LOG_COLOR_GREEN;
        case LOG_CAT_PERF:    return LOG_COLOR_BMAGENTA;
        default:              return LOG_COLOR_WHITE;
    }
}

// ----------------------------------------------------------------------------
// Log Rotation
// ----------------------------------------------------------------------------

void logger_rotate(void) {
    if (!g_logger.log_fp || !g_logger.enable_file_logging) return;
    
    fclose(g_logger.log_fp);
    g_logger.log_fp = NULL;
    
    // Rotate existing files
    char old_name[300], new_name[300];
    
    // Delete oldest
    snprintf(old_name, sizeof(old_name), "%s.%d", g_logger.log_file, g_logger.max_rotations);
    remove(old_name);
    
    // Shift others
    for (int i = g_logger.max_rotations - 1; i >= 1; i--) {
        snprintf(old_name, sizeof(old_name), "%s.%d", g_logger.log_file, i);
        snprintf(new_name, sizeof(new_name), "%s.%d", g_logger.log_file, i + 1);
        rename(old_name, new_name);
    }
    
    // Rename current to .1
    snprintf(new_name, sizeof(new_name), "%s.1", g_logger.log_file);
    rename(g_logger.log_file, new_name);
    
    // Open fresh log
    g_logger.log_fp = fopen(g_logger.log_file, "w");
    g_logger.current_size = 0;
    
    if (g_logger.log_fp) {
        fprintf(g_logger.log_fp, "=== Log rotated at %s ===\n", __DATE__);
    }
}

// ----------------------------------------------------------------------------
// Core Logging Function
// ----------------------------------------------------------------------------

void logger_log(LogLevel level, LogCategory cat, const char *file,
                int line, const char *func, const char *fmt, ...) {
    // Check if we should log this
    if (level < g_logger.console_level && level < g_logger.file_level) return;
    if (!(g_logger.category_mask & cat)) return;
    
    // Get timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Format message
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    
    // Extract just filename from path
    const char *filename = file;
    const char *slash = strrchr(file, '/');
    if (!slash) slash = strrchr(file, '\\');
    if (slash) filename = slash + 1;
    
    // Console output (with colors)
    if (level >= g_logger.console_level) {
        if (g_logger.enable_colors) {
            // Format: [TIME] [LEVEL] [CAT] message
            fprintf(stderr, "%s[%s]%s ", 
                    LOG_COLOR_DIM, timestamp, LOG_COLOR_RESET);
            
            fprintf(stderr, "%s[%s]%s ", 
                    logger_level_color(level), logger_level_name(level), LOG_COLOR_RESET);
            
            fprintf(stderr, "%s[%s]%s ", 
                    category_color(cat), category_name(cat), LOG_COLOR_RESET);
            
            // Color the message based on level
            fprintf(stderr, "%s%s%s", 
                    logger_level_color(level), message, LOG_COLOR_RESET);
            
            if (g_logger.enable_source_loc) {
                fprintf(stderr, " %s(%s:%d)%s", 
                        LOG_COLOR_DIM, filename, line, LOG_COLOR_RESET);
            }
            
            fprintf(stderr, "\n");
        } else {
            // Plain text
            fprintf(stderr, "[%s] [%s] [%s] %s",
                    timestamp, logger_level_name(level), category_name(cat), message);
            if (g_logger.enable_source_loc) {
                fprintf(stderr, " (%s:%d)", filename, line);
            }
            fprintf(stderr, "\n");
        }
    }
    
    // File output (no colors)
    if (g_logger.log_fp && level >= g_logger.file_level) {
        int written = fprintf(g_logger.log_fp, "[%s] [%s] [%s] %s",
                              timestamp, logger_level_name(level), 
                              category_name(cat), message);
        if (g_logger.enable_source_loc) {
            written += fprintf(g_logger.log_fp, " (%s:%d in %s)", filename, line, func);
        }
        written += fprintf(g_logger.log_fp, "\n");
        fflush(g_logger.log_fp);
        
        g_logger.current_size += written;
        
        // Check for rotation
        if (g_logger.current_size > g_logger.max_file_size) {
            logger_rotate();
        }
    }
    
    // Fatal errors exit the program
    if (level == LOG_LEVEL_FATAL) {
        logger_shutdown();
        exit(1);
    }
}

// ----------------------------------------------------------------------------
// Query Audit Logging
// ----------------------------------------------------------------------------

void logger_query(const char *user, const char *db, const char *query,
                  int success, double exec_time_ms) {
    if (!(g_logger.category_mask & LOG_CAT_QUERY)) return;
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Truncate long queries for display
    char query_display[200];
    if (strlen(query) > 150) {
        strncpy(query_display, query, 147);
        strcpy(query_display + 147, "...");
    } else {
        strcpy(query_display, query);
    }
    
    if (g_logger.enable_colors) {
        const char *status_color = success ? LOG_COLOR_BGREEN : LOG_COLOR_BRED;
        const char *status_text = success ? "OK" : "ERR";
        
        fprintf(stderr, "%s[%s]%s ", LOG_COLOR_DIM, timestamp, LOG_COLOR_RESET);
        fprintf(stderr, "%s[QUERY]%s ", LOG_COLOR_BMAGENTA, LOG_COLOR_RESET);
        fprintf(stderr, "%s[%s]%s ", status_color, status_text, LOG_COLOR_RESET);
        fprintf(stderr, "%s%s%s@%s%s%s ", 
                LOG_COLOR_CYAN, user, LOG_COLOR_RESET,
                LOG_COLOR_BLUE, db, LOG_COLOR_RESET);
        fprintf(stderr, "%s%.2fms%s ", 
                LOG_COLOR_DIM, exec_time_ms, LOG_COLOR_RESET);
        fprintf(stderr, "%s\"%s\"%s\n", 
                LOG_COLOR_WHITE, query_display, LOG_COLOR_RESET);
    }
    
    // File logging
    if (g_logger.log_fp) {
        fprintf(g_logger.log_fp, "[%s] [QUERY] %s user=%s db=%s time=%.2fms query=\"%s\"\n",
                timestamp, success ? "OK" : "FAIL", user, db, exec_time_ms, query);
        fflush(g_logger.log_fp);
    }
}

// ----------------------------------------------------------------------------
// Performance Logging
// ----------------------------------------------------------------------------

void logger_perf(const char *operation, double duration_ms,
                 int rows_affected, const char *details) {
    if (!(g_logger.category_mask & LOG_CAT_PERF)) return;
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Color code based on performance
    const char *perf_color;
    const char *perf_indicator;
    
    if (duration_ms < 10) {
        perf_color = LOG_COLOR_BGREEN;
        perf_indicator = "FAST";
    } else if (duration_ms < 100) {
        perf_color = LOG_COLOR_GREEN;
        perf_indicator = "OK  ";
    } else if (duration_ms < 1000) {
        perf_color = LOG_COLOR_YELLOW;
        perf_indicator = "SLOW";
    } else {
        perf_color = LOG_COLOR_RED;
        perf_indicator = "WARN";
    }
    
    if (g_logger.enable_colors) {
        fprintf(stderr, "%s[%s]%s ", LOG_COLOR_DIM, timestamp, LOG_COLOR_RESET);
        fprintf(stderr, "%s[PERF ]%s ", LOG_COLOR_BMAGENTA, LOG_COLOR_RESET);
        fprintf(stderr, "%s%s%s ", perf_color, perf_indicator, LOG_COLOR_RESET);
        fprintf(stderr, "%s%-20s%s ", LOG_COLOR_CYAN, operation, LOG_COLOR_RESET);
        fprintf(stderr, "%s%8.2fms%s ", perf_color, duration_ms, LOG_COLOR_RESET);
        if (rows_affected >= 0) {
            fprintf(stderr, "%s(%d rows)%s ", LOG_COLOR_DIM, rows_affected, LOG_COLOR_RESET);
        }
        if (details && details[0]) {
            fprintf(stderr, "%s%s%s", LOG_COLOR_DIM, details, LOG_COLOR_RESET);
        }
        fprintf(stderr, "\n");
    }
    
    // File logging
    if (g_logger.log_fp) {
        fprintf(g_logger.log_fp, "[%s] [PERF] op=%s time=%.2fms rows=%d %s\n",
                timestamp, operation, duration_ms, rows_affected, 
                details ? details : "");
        fflush(g_logger.log_fp);
    }
}

// ----------------------------------------------------------------------------
// Utility Functions
// ----------------------------------------------------------------------------

void logger_flush(void) {
    fflush(stderr);
    if (g_logger.log_fp) fflush(g_logger.log_fp);
}

// ----------------------------------------------------------------------------
// Pretty Print Helpers
// ----------------------------------------------------------------------------

void log_print_header(const char *title) {
    int len = strlen(title);
    int total = 60;
    int padding = (total - len - 2) / 2;
    
    if (g_logger.enable_colors) {
        fprintf(stderr, "\n%s%s", LOG_COLOR_BOLD, LOG_COLOR_CYAN);
        fprintf(stderr, "+");
        for (int i = 0; i < total - 2; i++) fprintf(stderr, "=");
        fprintf(stderr, "+\n");
        
        fprintf(stderr, "|");
        for (int i = 0; i < padding; i++) fprintf(stderr, " ");
        fprintf(stderr, "%s%s%s", LOG_COLOR_BYELLOW, title, LOG_COLOR_CYAN);
        for (int i = 0; i < total - padding - len - 2; i++) fprintf(stderr, " ");
        fprintf(stderr, "|\n");
        
        fprintf(stderr, "+");
        for (int i = 0; i < total - 2; i++) fprintf(stderr, "=");
        fprintf(stderr, "+%s\n\n", LOG_COLOR_RESET);
    } else {
        fprintf(stderr, "\n");
        for (int i = 0; i < total; i++) fprintf(stderr, "=");
        fprintf(stderr, "\n");
        for (int i = 0; i < padding; i++) fprintf(stderr, " ");
        fprintf(stderr, "%s\n", title);
        for (int i = 0; i < total; i++) fprintf(stderr, "=");
        fprintf(stderr, "\n\n");
    }
}

void log_print_divider(void) {
    if (g_logger.enable_colors) {
        fprintf(stderr, "%s", LOG_COLOR_DIM);
        for (int i = 0; i < 60; i++) fprintf(stderr, "-");
        fprintf(stderr, "%s\n", LOG_COLOR_RESET);
    } else {
        for (int i = 0; i < 60; i++) fprintf(stderr, "-");
        fprintf(stderr, "\n");
    }
}

void log_print_kv(const char *key, const char *value) {
    if (g_logger.enable_colors) {
        fprintf(stderr, "  %s%-20s%s : %s%s%s\n",
                LOG_COLOR_CYAN, key, LOG_COLOR_RESET,
                LOG_COLOR_WHITE, value, LOG_COLOR_RESET);
    } else {
        fprintf(stderr, "  %-20s : %s\n", key, value);
    }
}

void log_print_success(const char *message) {
    if (g_logger.enable_colors) {
        fprintf(stderr, "  %s[OK]%s %s%s%s\n",
                LOG_COLOR_BGREEN, LOG_COLOR_RESET,
                LOG_COLOR_GREEN, message, LOG_COLOR_RESET);
    } else {
        fprintf(stderr, "  [OK] %s\n", message);
    }
}

void log_print_error(const char *message) {
    if (g_logger.enable_colors) {
        fprintf(stderr, "  %s[FAIL]%s %s%s%s\n",
                LOG_COLOR_BRED, LOG_COLOR_RESET,
                LOG_COLOR_RED, message, LOG_COLOR_RESET);
    } else {
        fprintf(stderr, "  [ERROR] %s\n", message);
    }
}

void log_print_warning(const char *message) {
    if (g_logger.enable_colors) {
        fprintf(stderr, "  %s[WARN]%s %s%s%s\n",
                LOG_COLOR_BYELLOW, LOG_COLOR_RESET,
                LOG_COLOR_YELLOW, message, LOG_COLOR_RESET);
    } else {
        fprintf(stderr, "  [WARN] %s\n", message);
    }
}

void log_print_progress(const char *task, int current, int total) {
    int bar_width = 30;
    float progress = (float)current / total;
    int filled = (int)(progress * bar_width);
    
    if (g_logger.enable_colors) {
        fprintf(stderr, "\r  %s%-20s%s [", LOG_COLOR_CYAN, task, LOG_COLOR_RESET);
        fprintf(stderr, "%s", LOG_COLOR_BGREEN);
        for (int i = 0; i < filled; i++) fprintf(stderr, "█");
        fprintf(stderr, "%s", LOG_COLOR_DIM);
        for (int i = filled; i < bar_width; i++) fprintf(stderr, "░");
        fprintf(stderr, "%s] %s%3d%%%s",
                LOG_COLOR_RESET, LOG_COLOR_BYELLOW, (int)(progress * 100), LOG_COLOR_RESET);
    } else {
        fprintf(stderr, "\r  %-20s [", task);
        for (int i = 0; i < filled; i++) fprintf(stderr, "#");
        for (int i = filled; i < bar_width; i++) fprintf(stderr, ".");
        fprintf(stderr, "] %3d%%", (int)(progress * 100));
    }
    
    if (current >= total) fprintf(stderr, "\n");
    fflush(stderr);
}

void log_print_table_header(const char **columns, int count) {
    if (g_logger.enable_colors) {
        fprintf(stderr, "  %s+", LOG_COLOR_CYAN);
        for (int i = 0; i < count; i++) {
            for (int j = 0; j < 18; j++) fprintf(stderr, "-");
            if (i < count - 1) fprintf(stderr, "+");
        }
        fprintf(stderr, "+%s\n", LOG_COLOR_RESET);
        
        fprintf(stderr, "  %s|%s", LOG_COLOR_CYAN, LOG_COLOR_RESET);
        for (int i = 0; i < count; i++) {
            fprintf(stderr, " %s%s%-16s%s %s|%s",
                    LOG_COLOR_BOLD, LOG_COLOR_BYELLOW, 
                    columns[i], LOG_COLOR_RESET,
                    LOG_COLOR_CYAN, LOG_COLOR_RESET);
        }
        fprintf(stderr, "\n");
        
        fprintf(stderr, "  %s+", LOG_COLOR_CYAN);
        for (int i = 0; i < count; i++) {
            for (int j = 0; j < 18; j++) fprintf(stderr, "-");
            if (i < count - 1) fprintf(stderr, "+");
        }
        fprintf(stderr, "+%s\n", LOG_COLOR_RESET);
    } else {
        fprintf(stderr, "  +");
        for (int i = 0; i < count; i++) {
            for (int j = 0; j < 18; j++) fprintf(stderr, "-");
            fprintf(stderr, "+");
        }
        fprintf(stderr, "\n  |");
        for (int i = 0; i < count; i++) {
            fprintf(stderr, " %-16s |", columns[i]);
        }
        fprintf(stderr, "\n  +");
        for (int i = 0; i < count; i++) {
            for (int j = 0; j < 18; j++) fprintf(stderr, "-");
            fprintf(stderr, "+");
        }
        fprintf(stderr, "\n");
    }
}

void log_print_table_row(const char **values, int count) {
    if (g_logger.enable_colors) {
        fprintf(stderr, "  %s|%s", LOG_COLOR_CYAN, LOG_COLOR_RESET);
        for (int i = 0; i < count; i++) {
            fprintf(stderr, " %s%-16s%s %s|%s",
                    LOG_COLOR_WHITE, values[i], LOG_COLOR_RESET,
                    LOG_COLOR_CYAN, LOG_COLOR_RESET);
        }
        fprintf(stderr, "\n");
    } else {
        fprintf(stderr, "  |");
        for (int i = 0; i < count; i++) {
            fprintf(stderr, " %-16s |", values[i]);
        }
        fprintf(stderr, "\n");
    }
}

void log_print_table_end(int col_count) {
    if (g_logger.enable_colors) {
        fprintf(stderr, "  %s+", LOG_COLOR_CYAN);
        for (int i = 0; i < col_count; i++) {
            for (int j = 0; j < 18; j++) fprintf(stderr, "-");
            if (i < col_count - 1) fprintf(stderr, "+");
        }
        fprintf(stderr, "+%s\n", LOG_COLOR_RESET);
    } else {
        fprintf(stderr, "  +");
        for (int i = 0; i < col_count; i++) {
            for (int j = 0; j < 18; j++) fprintf(stderr, "-");
            fprintf(stderr, "+");
        }
        fprintf(stderr, "\n");
    }
}
