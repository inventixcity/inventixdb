#ifndef INVENTIX_PARSER_H
#define INVENTIX_PARSER_H

#include "lexer.h"

typedef enum {
    NODE_CMD_CREATE_TABLE,
    NODE_CMD_INSERT,
    NODE_CMD_SELECT,
    NODE_CMD_DELETE,
    NODE_CMD_DROP_TABLE, // GIRAO
    NODE_CMD_DOC_INSERT, // RAKHO
    NODE_CMD_DOC_FIND,   // DHUNDO
    
    // Expressions
    NODE_EXPR_BINARY,    // For WHERE clause (col = val)
    NODE_EXPR_LITERAL,   // 1, "abc"
    NODE_EXPR_IDENTIFIER,// col_name
    NODE_EXPR_SUBQUERY   // (SELECT ...)
} ASTNodeType;

typedef struct ASTNode ASTNode;

// Generic List for Columns/Values
typedef struct NodeList {
    char *value; // For column names or string values
    struct NodeList *next;
} NodeList;

// Structure definitions
typedef struct {
    char *name;
    char *type;
    int is_pk;
} ColumnDef;

struct ASTNode {
    ASTNodeType type;
    
    union {
        struct {
            char *table_name;
            ColumnDef *columns;
            int col_count;
        } create_table;

        struct {
            char *table_name;
            NodeList *values;
        } insert;

        struct {
            char *table_name;
            NodeList *columns; // NULL means *
            ASTNode *where_clause;
        } select;

        struct {
            char *table_name;
            ASTNode *where_clause;
        } delete_stmt;
        
        struct {
            char *table_name;
        } drop_table;

        struct {
            char *collection;
            char *json_body;
        } doc_insert;

        struct {
            char *collection;
            ASTNode *where_clause;
        } doc_find;

        // Expressions
        struct {
            ASTNode *left;
            char *op; // =, >, <
            ASTNode *right;
        } binary_expr;

        struct {
            char *value;
            int is_int; // Helper for type
        } literal;
        
        struct {
            ASTNode *subquery_stmt;
        } subquery;
    } data;
};

ASTNode* parse(TokenList *tokens);
void print_ast(ASTNode *node);
void free_ast(ASTNode *node);

#endif
