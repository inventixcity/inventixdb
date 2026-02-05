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
    NODE_CMD_DOC_GET,    // MANGWAO (New)
    NODE_CMD_DOC_REMOVE, // HATAO (New)
    NODE_CMD_CHECKPOINT, // CHECKPOINT
    NODE_CMD_CREATE_INDEX,// CREATE INDEX
    NODE_CMD_CREATE_USER, // CREATE USER
    NODE_CMD_CREATE_DB,   // CREATE DATABASE
    NODE_CMD_SHOW_TABLES, // SHOW TABLES
    NODE_CMD_USE_DB,      // USE <db>

    // Expressions
    NODE_EXPR_BINARY,    // For WHERE clause (col = val)
    NODE_EXPR_LOGICAL,   // AND / OR
    NODE_EXPR_LITERAL,   // 1, "abc"
    NODE_EXPR_IDENTIFIER,// col_name
    NODE_EXPR_SUBQUERY,   // (SELECT ...)
    NODE_EXPR_GROUP_FUNC // COUNT(col), SUM(col)
} ASTNodeType;

typedef struct ASTNode ASTNode;

// Generic List for Columns/Values
typedef struct NodeList {
    char *value; // For column names or string values
    char *alias; // For Aggregations (e.g. COUNT(id))
    int func_type; // 0=None, 1=COUNT, etc.
    struct NodeList *next;
} NodeList;

// Multi-Row Insert Support
typedef struct RowValueList {
    NodeList *values;
    struct RowValueList *next;
} RowValueList;

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
            RowValueList *rows; // Support Multi-Row
        } insert;

        struct {
            char *table_name;
            NodeList *columns; // NULL means *
            ASTNode *where_clause;
            NodeList *group_by; // GROUP BY columns
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

        // Reusing doc_find for MANGWAO too? 
        // Or make a specific one to support simple key lookup
        struct {
            char *collection;
            char *doc_id; // Optional
        } doc_get;
        
        struct {
            char *collection;
            char *doc_id;
        } doc_remove;

        struct {
            char *table_name;
            char *col_name;
        } create_index;

        struct {
            char *username;
            char *password;
        } create_user;

        struct {
            char *db_name;
        } create_db;

        struct {
            char *db_name;
        } use_db;

        struct {
            char *collection;
            ASTNode *where_clause;
        } doc_find;

        // Expressions (Binary & Logical)
        struct {
            ASTNode *left;
            char *op; // =, >, <, AND, OR
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
