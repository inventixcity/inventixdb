#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "storage.h"
#include "executor.h"
#include "auth.h"
#include "system.h"
#include "colors.h"
#include "config.h"       // Configuration System
#include "logger.h"       // Logging System
#include "transaction.h"  // Transaction System
#include "query_result.h" // Query Caching System

void print_help() {
    printf("\n" ANSI_BOLD ANSI_MAGENTA "=== InventixDB Madad (Help) ===" ANSI_RESET "\n");
    printf(ANSI_YELLOW "1. Data Definition (DDL):" ANSI_RESET "\n");
    printf("   TABLE BANAO <name> (<col> <type>, ...);  - Create Table (Hinglish)\n");
    printf("   CREATE TABLE <name> (<col> <type>, ...); - Create Table (SQL)\n");
    printf("   CREATE INDEX ON <table> (<col>);         - Create Index\n");
    printf("   CREATE DATABASE <name>;                  - Create DB\n");
    printf("   CREATE USER <name> PASSWORD '...';       - Create User\n");
    printf("   GIRAO TABLE <name>; / DROP TABLE <name>; - Drop Table\n");
    printf("\n");
    printf(ANSI_YELLOW "2. Data Manipulation (DML):" ANSI_RESET "\n");
    printf("   DALO <table> MAAN (val, ...);            - Insert Row (Hinglish)\n");
    printf("   INSERT INTO <table> VALUES (val, ...);   - Insert Row (SQL)\n");
    printf("   DIKHAO * SE <table> [JAHAN <cond>];      - Query Data (Hinglish)\n");
    printf("   SELECT * FROM <table> [WHERE <cond>];    - Query Data (SQL)\n");
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
    printf("   \033[3m(Note: Only Scalar Subqueries supported. Correlated/Multi-row unsupported.)\033[0m\n");
    printf("\n");
    printf(ANSI_YELLOW "6. System Commands:" ANSI_RESET "\n");
    printf("   USE <db_name>; / ISTEMAAL <db_name>;     - Switch DB\n");
    printf("   SHOW TABLES; / DEKHO TABLES;             - List Tables\n");
    printf("   CHECKPOINT;                              - Force Save\n");
    printf("   madad / help                             - Show this menu\n");
    printf("   bahar / exit / quit / niklo              - Quit CLI\n");
    printf(ANSI_MAGENTA "===============================" ANSI_RESET "\n\n");
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
    
    // Initialize Session
    SessionContext ctx;
    strcpy(ctx.current_db, "public");
    strcpy(ctx.current_user, "admin"); 

    printf("Logged in as 'admin' (superuser).\n");

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
    query_cache_shutdown();
    // kv_close(store); // TODO: Implement cleanup
    LOG_INFO("InventixDB shutting down...");
    return 0;
}
