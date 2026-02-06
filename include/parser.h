#ifndef INVENTIX_PARSER_H
#define INVENTIX_PARSER_H

#include "lexer.h"

typedef enum {
    NODE_CMD_CREATE_TABLE,
    NODE_CMD_INSERT,
    NODE_CMD_SELECT,
    NODE_CMD_DELETE,
    NODE_CMD_UPDATE,     // UPDATE table SET col=val WHERE ...
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

    // Transaction Commands
    NODE_CMD_BEGIN,      // BEGIN / START TRANSACTION
    NODE_CMD_COMMIT,     // COMMIT
    NODE_CMD_ROLLBACK,   // ROLLBACK
    NODE_CMD_SAVEPOINT,  // SAVEPOINT <name>
    NODE_CMD_RELEASE_SAVEPOINT, // RELEASE SAVEPOINT <name>
    NODE_CMD_ROLLBACK_TO,       // ROLLBACK TO <name>

    // Prepared Statement Commands
    NODE_CMD_PREPARE,    // PREPARE <name> AS <query>
    NODE_CMD_EXECUTE,    // EXECUTE <name> USING (params)
    NODE_CMD_DEALLOCATE, // DEALLOCATE <name>

    // Query Commands
    NODE_CMD_EXPLAIN,    // EXPLAIN [ANALYZE] <query>

    // ALTER TABLE Commands (Iteration 2)
    NODE_CMD_ALTER_TABLE,    // ALTER TABLE ...
    NODE_CMD_ADD_COLUMN,     // ALTER TABLE ADD COLUMN
    NODE_CMD_DROP_COLUMN,    // ALTER TABLE DROP COLUMN
    NODE_CMD_RENAME_COLUMN,  // ALTER TABLE RENAME COLUMN
    NODE_CMD_MODIFY_COLUMN,  // ALTER TABLE MODIFY COLUMN
    NODE_CMD_ADD_CONSTRAINT, // ALTER TABLE ADD CONSTRAINT
    NODE_CMD_DROP_CONSTRAINT,// ALTER TABLE DROP CONSTRAINT
    
    // Backup/Restore Commands
    NODE_CMD_BACKUP,     // BACKUP DATABASE
    NODE_CMD_RESTORE,    // RESTORE DATABASE
    NODE_CMD_EXPORT,     // EXPORT TABLE
    NODE_CMD_IMPORT,     // IMPORT TABLE
    
    // NoSQL Enhanced Commands
    NODE_CMD_DOC_UPSERT,     // UPSERT document
    NODE_CMD_DOC_AGGREGATE,  // Aggregation pipeline
    NODE_CMD_CREATE_COLLECTION, // Create collection

    // Expressions
    NODE_EXPR_BINARY,    // For WHERE clause (col = val)
    NODE_EXPR_LOGICAL,   // AND / OR
    NODE_EXPR_LITERAL,   // 1, "abc"
    NODE_EXPR_IDENTIFIER,// col_name
    NODE_EXPR_COLUMN_REF,// table.column reference
    NODE_EXPR_SUBQUERY,   // (SELECT ...)
    NODE_EXPR_GROUP_FUNC, // COUNT(col), SUM(col)
    NODE_EXPR_JOIN       // JOIN expression
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
            
            // JOIN support
            ASTNode *join_clause;    // JOIN expression tree
            char *from_table;        // Primary FROM table
            char **join_tables;      // Additional joined tables
            int join_table_count;
            
            // ORDER BY support
            char **order_columns;
            int *order_desc;         // 1 = DESC, 0 = ASC
            int order_by_count;
            
            // LIMIT / OFFSET
            int limit;
            int offset;
        } select;

        struct {
            char *table_name;
            ASTNode *where_clause;
        } delete_stmt;
        
        // UPDATE statement: UPDATE table SET col1=val1, col2=val2 WHERE ...
        struct {
            char *table_name;
            char **set_columns;      // Column names to update
            char **set_values;       // New values (as strings)
            int set_count;           // Number of SET assignments
            ASTNode *where_clause;   // WHERE condition (NULL = update all)
        } update_stmt;
        
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

        // Transaction Commands
        struct {
            char *savepoint_name;  // For SAVEPOINT, RELEASE, ROLLBACK TO
        } transaction;

        // Prepared Statement Commands
        struct {
            char *stmt_name;       // Statement name
            char *query_template;  // For PREPARE: the query with ? placeholders
            char *using_clause;    // For EXECUTE: parameter values "(1, 'test', ...)"
        } prepared;

        // EXPLAIN statement
        struct {
            ASTNode *query;        // The query to explain
            int analyze;           // 1 = EXPLAIN ANALYZE, 0 = just EXPLAIN
            int format;            // 0 = text, 1 = JSON, 2 = XML
        } explain;

        // JOIN expression
        struct {
            int join_type;         // INNER=0, LEFT=1, RIGHT=2, FULL=3, CROSS=4
            char *left_table;
            char *right_table;
            char *left_alias;
            char *right_alias;
            ASTNode *on_condition; // ON clause
            char **using_columns;  // USING clause columns
            int using_count;
        } join_expr;

        // Column reference (table.column)
        struct {
            char *table_name;      // May be NULL for unqualified
            char *column_name;
        } column_ref;

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
        
        // ALTER TABLE Commands (Iteration 2)
        struct {
            char *table_name;
            int alter_type;        // 0=ADD_COL, 1=DROP_COL, 2=RENAME_COL, 3=MODIFY_COL, 4=ADD_CONSTRAINT, 5=DROP_CONSTRAINT
            char *column_name;     // Column to add/drop/rename/modify
            char *new_name;        // For RENAME: new column name
            char *column_type;     // For ADD/MODIFY: column type
            char *default_value;   // DEFAULT value
            int is_nullable;       // 0=NOT NULL, 1=NULL allowed
            int is_unique;         // UNIQUE constraint
            // Foreign Key
            char *fk_constraint_name;
            char *fk_ref_table;    // REFERENCES table
            char *fk_ref_column;   // REFERENCES column
            int fk_on_delete;      // 0=RESTRICT, 1=CASCADE, 2=SET NULL
            int fk_on_update;      // 0=RESTRICT, 1=CASCADE, 2=SET NULL
        } alter_table;
        
        // Backup/Restore Commands
        struct {
            char *path;            // File path for backup/restore
            char *db_name;         // Database name (optional)
            char *table_name;      // Table name for export/import
            int format;            // 0=binary, 1=SQL, 2=JSON, 3=CSV
            int compression;       // 0=none, 1=gzip
        } backup_restore;
        
        // NoSQL Enhanced Commands
        struct {
            char *collection;
            char *doc_id;          // For upsert: document ID
            char *json_body;       // Document body
            char *filter;          // Query filter (JSON)
            char *update_expr;     // Update expression (JSON)
        } doc_upsert;
        
        struct {
            char *collection;
            char **pipeline_stages; // Array of JSON stage definitions
            int stage_count;
        } doc_aggregate;
    } data;
};

ASTNode* parse(TokenList *tokens);
void print_ast(ASTNode *node);
void free_ast(ASTNode *node);

#endif
