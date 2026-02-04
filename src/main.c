#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "storage.h"
#include "executor.h"

void print_help() {
    printf("\n=== InventixDB Madad (Help) ===\n");
    printf("1. Create Table:  TABLE BANAO <name> (<col> <type>, ...);\n");
    printf("   Example:       TABLE BANAO users (id INT, name STRING);\n");
    printf("2. Insert Data:   INSERT KARO <table_name> VALUES (<val1>, <val2>, ...);\n");
    printf("   Example:       INSERT KARO users VALUES (1, \"ali\");\n");
    printf("3. Query Data:    SELECT <cols> FROM <table> [JAHAN <condition>];\n");
    printf("   Example:       SELECT name FROM users JAHAN id=1;\n");
    printf("4. General:       help / madad : Show this menu.\n");
    printf("                  exit         : Quit the CLI.\n");
    printf("===============================\n\n");
}

int main() {
    printf("InventixDB CLI (Phase 2)\nType 'exit' to quit, 'help' or 'madad' for commands.\n");
    char buffer[1024];

    while (1) {
        printf("inventix> ");
        if (!fgets(buffer, 1024, stdin)) break;
        
        buffer[strcspn(buffer, "\n")] = 0; // Remove newline
        if (strcmp(buffer, "exit") == 0) break;
        if (strlen(buffer) == 0) continue;
        
        if (strcmp(buffer, "help") == 0 || strcmp(buffer, "madad") == 0) {
            print_help();
            continue;
        }

        // Lexer support check
        // printf("[DEBUG] Input: %s\n", buffer);

        TokenList *tokens = tokenize(buffer);
        /*
        // Debug Tokens
        for (int i=0; i<tokens->count; i++) {
             printf("Tok: %s (%s)\n", tokens->tokens[i].value, token_type_to_string(tokens->tokens[i].type));
        }
        */

        if (tokens->count > 1) { // Not just EOF
            ASTNode *ast = parse(tokens);
            if (ast) {
                // printf("--- AST ---\n");
                // print_ast(ast);
                
                // Initialize store if not done (hack for single CLI)
                static KVStore *store = NULL;
                if (!store) store = kv_create();
                
                execute_query(ast, store, stdout);
                
                // free_ast(ast);
            }
        }
        
        free_token_list(tokens);
    }

    return 0;
}
