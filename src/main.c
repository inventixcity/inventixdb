#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "storage.h"
#include "executor.h"
#include "system.h"
#include "colors.h"
#include "config.h"       // Configuration System
#include "logger.h"       // Logging System
#include "transaction.h"  // Transaction System
#include "query_result.h" // Query Caching System
#include "security.h"     // Security System (PBKDF2, RBAC, Sessions)

// Forward-declare from auth.h (avoid User struct conflict with security.h)
void auth_init(KVStore *store);

#ifdef _WIN32
#define strcasecmp _stricmp
#include <conio.h>
#endif

void print_help() {
    printf("\n" ANSI_BOLD ANSI_MAGENTA "=== InventixDB Madad (Help) ===" ANSI_RESET "\n");
    printf(ANSI_YELLOW "1. Data Definition (DDL):" ANSI_RESET "\n");
    printf("   TABLE BANAO <name> (<col> <type>, ...);  - Create Table (Hinglish)\n");
    printf("   CREATE TABLE <name> (<col> <type>, ...); - Create Table (SQL)\n");
    printf("   CREATE INDEX ON <table> (<col>);         - Create Index\n");
    printf("   CREATE DATABASE <name>;                  - Create DB\n");
    printf("   DROP DATABASE <name>;                    - Drop DB (SQL)\n");
    printf("   DATABASE HATAO <name>;                   - Drop DB (Hinglish)\n");
    printf("   GIRAO DATABASE <name>;                   - Drop DB (Hinglish)\n");
    printf("   CREATE USER <name> PASSWORD '...';       - Create User\n");
    printf("   GIRAO TABLE <name>; / DROP TABLE <name>; - Drop Table\n");
    printf("\n");
    printf(ANSI_YELLOW "2. Data Manipulation (DML):" ANSI_RESET "\n");
    printf("   DALO <table> MAAN (val, ...);            - Insert Row (Hinglish)\n");
    printf("   INSERT INTO <table> VALUES (val, ...);   - Insert Row (SQL)\n");
    printf("   DIKHAO * SE <table> [JAHAN <cond>];      - Query Data (Hinglish)\n");
    printf("   SELECT * FROM <table> [WHERE <cond>];    - Query Data (SQL)\n");
    printf("   BADLO <t> RAKHO_YEH col='val' JAHAN ...; - Update (Hinglish)\n");
    printf("   UPDATE <t> SET col='val' WHERE ...;      - Update (SQL)\n");
    printf("   NIKALO SE <table> JAHAN <cond>;          - Delete Row (Hinglish)\n");
    printf("   DELETE FROM <table> WHERE <cond>;        - Delete Row (SQL)\n");
    printf("\n");
    printf(ANSI_YELLOW "3. NoSQL / Document Store:" ANSI_RESET "\n");
    printf("   RAKHO <col> <json>;                      - Insert Doc\n");
    printf("   MANGWAO <col>;                           - Get All Docs\n");
    printf("   DHUNDO <col> JAHAN id=<id>;              - Find Doc\n");
    printf("   HATAO <col> <id>;                        - Remove Doc\n");
    printf("\n");
    printf(ANSI_YELLOW "4. Transactions:" ANSI_RESET "\n");
    printf("   BEGIN; / SHURU;                          - Start Transaction\n");
    printf("   COMMIT; / PUKKA;                         - Commit Transaction\n");
    printf("   ROLLBACK; / WAPAS;                       - Rollback Transaction\n");
    printf("   SAVEPOINT <name>;                        - Create Savepoint\n");
    printf("   ROLLBACK TO <name>;                      - Rollback to Savepoint\n");
    printf("\n");
    printf(ANSI_YELLOW "5. Advanced Features:" ANSI_RESET "\n");
    printf("   Subqueries: SELECT ... JAHAN id = (SELECT id FROM ...)\n");
    printf("   \033[3m(Note: Only Scalar Subqueries supported.)\033[0m\n");
    printf("\n");
    printf(ANSI_YELLOW "6. System Commands:" ANSI_RESET "\n");
    printf("   USE <db_name>; / ISTEMAAL <db_name>;     - Switch DB\n");
    printf("   SHOW TABLES; / DEKHO TABLES;             - List Tables\n");
    printf("   SHOW DATABASES; / DEKHO DATABASES;       - List Databases\n");
    printf("   CHECKPOINT;                              - Force Save\n");
    printf("   madad / help                             - Show this menu\n");
    printf("   bahar / exit / quit / niklo              - Quit CLI\n");
    printf(ANSI_MAGENTA "===============================" ANSI_RESET "\n\n");
}

// Read password with masking (shows * instead of characters)
static void read_password(const char *prompt, char *buf, size_t bufsize) {
    printf("%s", prompt);
    fflush(stdout);
    size_t i = 0;
#ifdef _WIN32
    int ch;
    while (i < bufsize - 1) {
        ch = _getch();
        if (ch == '\r' || ch == '\n') break;
        if (ch == '\b' || ch == 127) {
            if (i > 0) { i--; printf("\b \b"); fflush(stdout); }
            continue;
        }
        buf[i++] = (char)ch;
        printf("*");
        fflush(stdout);
    }
#else
    // Fallback: read from stdin
    if (fgets(buf, (int)bufsize, stdin)) {
        buf[strcspn(buf, "\n")] = 0;
        i = strlen(buf);
    }
#endif
    buf[i] = '\0';
    printf("\n");
}

// Authenticate user via the security module
static int cli_authenticate(SessionContext *ctx) {
    char username[64], password[128];
    int attempts = 0;
    const int max_attempts = 3;
    
    printf("\n" ANSI_CYAN "=== InventixDB Authentication / Tasdiq ===" ANSI_RESET "\n");
    printf(ANSI_YELLOW "  Default: admin / Admin@123" ANSI_RESET "\n\n");
    
    while (attempts < max_attempts) {
        printf("Username: ");
        fflush(stdout);
        if (!fgets(username, sizeof(username), stdin)) return -1;
        username[strcspn(username, "\n")] = 0;
        
        if (strlen(username) == 0) {
            printf(ANSI_RED "Username cannot be empty.\n" ANSI_RESET);
            attempts++;
            continue;
        }
        
        read_password("Password: ", password, sizeof(password));
        
        // Try security module login
        char error_msg[256] = {0};
        Session *session = NULL;
        int result = security_login(username, password, "127.0.0.1", 0, &session, error_msg);
        
        // Clear password from memory immediately
        memset(password, 0, sizeof(password));
        
        if (result == 0 && session) {
            strncpy(ctx->current_user, session->username, sizeof(ctx->current_user) - 1);
            printf(ANSI_GREEN "\nLogin successful! Welcome, %s" ANSI_RESET " (role: %s)\n\n",
                   username, session->is_superuser ? "superadmin" : "user");
            LOG_SECURITY("CLI login: user '%s' authenticated successfully", username);
            return 0;
        }
        
        attempts++;
        printf(ANSI_RED "Login failed: %s" ANSI_RESET " (%d/%d attempts)\n", 
               error_msg, attempts, max_attempts);
        LOG_SECURITY("CLI login failed: user '%s' (%s), attempt %d/%d", 
                     username, error_msg, attempts, max_attempts);
    }
    
    printf(ANSI_RED "\nToo many failed login attempts. Exiting.\n" ANSI_RESET);
    LOG_SECURITY("CLI locked out after %d failed login attempts", max_attempts);
    return -1;
}

int main() {
    // Load configuration
    config_init_defaults(&g_config);
    config_load(&g_config, "inventix.conf");
    
    // Initialize logger
    logger_init();
    
    // Initialize transaction manager
    txn_manager_init();
    
    // Initialize query cache
    query_cache_init(64 * 1024 * 1024); // 64MB cache
    
    // Print startup header
    log_print_header("InventixDB v0.5");
    
    LOG_INFO("Starting InventixDB CLI...");
    
    if (g_config.config_loaded) {
        LOG_INFO("Configuration loaded from: %s", g_config.config_file);
    } else {
        LOG_WARN("No config file found, using defaults");
    }
    
    char buffer[1024];

    // Initialize Global Store
    KVStore *store = kv_create();
    sys_init(store);
    auth_init(store);
    
    // Initialize Security System (PBKDF2, RBAC, Sessions)
    security_init();
    security_load("security.dat");
    
    // Initialize Session
    SessionContext ctx;
    strcpy(ctx.current_db, "public");
    memset(ctx.current_user, 0, sizeof(ctx.current_user));

    // Authenticate user before entering REPL
    if (cli_authenticate(&ctx) != 0) {
        LOG_ERROR("Authentication failed — exiting.");
        security_shutdown();
        query_cache_shutdown();
        return 1;
    }

    while (1) {
        // Dynamic Prompt (from Session)
        const char *db = ctx.current_db;
        const char *user = ctx.current_user;
        
        // Colors: User=Green, DB=Blue
        printf("\033[1;36m%s\033[0m@\033[1;32m%s\033[0m> ", db, user);
        
        if (!fgets(buffer, 1024, stdin)) break;
        
        buffer[strcspn(buffer, "\n")] = 0; 
        if (strcmp(buffer, "exit") == 0 || strcasecmp(buffer, "bahar") == 0 || 
            strcasecmp(buffer, "quit") == 0 || strcasecmp(buffer, "niklo") == 0) break;
        if (strlen(buffer) == 0) continue;
        
        if (strcmp(buffer, "help") == 0 || strcmp(buffer, "madad") == 0) {
            print_help();
            continue;
        }

        TokenList *tokens = tokenize(buffer);

        if (tokens->count > 0 && tokens->tokens[0].type != TOKEN_EOF) {
            ASTNode *ast = parse(tokens);
            if (ast) {
                execute_query(ast, store, &ctx, stdout);
                // free_ast(ast); // TODO: Implement AST cleanup
            }
        }
        
        free_token_list(tokens);
    }
    
    // Cleanup
    security_save("security.dat");
    security_shutdown();
    query_cache_shutdown();
    // kv_close(store); // TODO: Implement cleanup
    LOG_INFO("InventixDB shutting down...");
    return 0;
}
