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

void print_help() {
    printf("\n" ANSI_BOLD ANSI_MAGENTA "=== InventixDB Madad (Help) ===" ANSI_RESET "\n");
    printf(ANSI_YELLOW "1. Data Definition (DDL):" ANSI_RESET "\n");
    printf("   TABLE BANAO <name> (<col> <type>, ...);  - Create Table\n");
    printf("   CREATE INDEX ON <table> (<col>);         - Create Index\n");
    printf("   CREATE DATABASE <name>;                  - Create DB\n");
    printf("   CREATE USER <name> PASSWORD '...';       - Create User\n");
    printf("\n");
    printf(ANSI_YELLOW "2. Data Manipulation (DML):" ANSI_RESET "\n");
    printf("   INSERT KARO <table_name> VALUES (<v>..); - Insert Row\n");
    printf("   SELECT * FROM <table> [JAHAN <cond>];    - Query Data\n");
    printf("   DELETE FROM <table> WHERE <cond>;        - Delete Data\n");
    printf("   UPDATE ... (Coming Soon)\n");
    printf("\n");
    printf(ANSI_YELLOW "3. NoSQL / Document Store:" ANSI_RESET "\n");
    printf("   RAKHO <col> <json>;                      - Insert Doc\n");
    printf("   MANGWAO <col>;                           - Get All Docs\n");
    printf("   DHUNDO <col> WHERE id=<id>;              - Find Doc\n");
    printf("   HATAO <col> <id>;                        - Remove Doc\n");
    printf("\n");
    printf(ANSI_YELLOW "4. Advanced Features:" ANSI_RESET "\n");
    printf("   Subqueries: SELECT ... JAHAN id = (SELECT id FROM ...)\n");
    printf("   \033[3m(Note: Only Scalar Subqueries supported. Correlated/Multi-row unsupported.)\033[0m\n");
    printf("\n");
    printf(ANSI_YELLOW "5. System Commands:" ANSI_RESET "\n");
    printf("   USE <db_name>;                           - Switch DB\n");
    printf("   SHOW TABLES;                             - List Tables\n");
    printf("   CHECKPOINT;                              - Force Save\n");
    printf("   madad / help                             - Show this menu\n");
    printf("   exit                                     - Quit CLI\n");
    printf(ANSI_MAGENTA "===============================" ANSI_RESET "\n\n");
}

int main() {
    printf("InventixDB CLI (v0.3 - Multi-DB & Auth)\nType 'exit' to quit.\n");
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
        if (strcmp(buffer, "exit") == 0) break;
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
    
    // kv_close(store); // TODO: Implement cleanup
    return 0;
}
