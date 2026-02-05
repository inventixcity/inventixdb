#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "parser.h"

// Helper context for parsing
typedef struct {
    TokenList *tokens;
    int current;
    jmp_buf env;
} ParserContext;

Token* current_token(ParserContext *ctx) {
    if (ctx->current < ctx->tokens->count) {
        return &ctx->tokens->tokens[ctx->current];
    }
    return &ctx->tokens->tokens[ctx->tokens->count - 1]; // EOF
}

Token* advance(ParserContext *ctx) {
    if (ctx->current < ctx->tokens->count) {
        ctx->current++;
    }
    return current_token(ctx);
}

int match(ParserContext *ctx, LexerTokenType type) {
    if (current_token(ctx)->type == type) {
        advance(ctx);
        return 1;
    }
    return 0;
}

Token* consume(ParserContext *ctx, LexerTokenType type, const char *message) {
    if (current_token(ctx)->type == type) {
        Token *t = current_token(ctx);
        advance(ctx);
        return t;
    }
    printf("Parse Error: %s at line %d. Found: %s\n", message, current_token(ctx)->line, token_type_to_string(current_token(ctx)->type));
    longjmp(ctx->env, 1);
    return NULL;
}

// Forward declarations
ASTNode* parse_select(ParserContext *ctx, int is_subquery);
ASTNode* parse_expression(ParserContext *ctx);

ASTNode* create_node(ASTNodeType type) {
    ASTNode *node = malloc(sizeof(ASTNode));
    memset(node, 0, sizeof(ASTNode));
    node->type = type;
    return node;
}

// Parsing Functions

// 11. Create Index: CREATE INDEX ON table (col)
ASTNode* parse_create_table(ParserContext *ctx); // Fwd Decl

ASTNode* parse_create_user(ParserContext *ctx) {
    consume(ctx, TOKEN_KW_USER, "Expected 'USER'");
    
    if (current_token(ctx)->type == TOKEN_KW_BANAO) {
         consume(ctx, TOKEN_KW_BANAO, "BANAO");
    }

    Token *usr = consume(ctx, TOKEN_IDENTIFIER, "Username");
    
    // Check for WITH PASSWORD or just PASSWORD
    consume(ctx, TOKEN_KW_PASSWORD, "Expected 'PASSWORD'");
    Token *pw = consume(ctx, TOKEN_STRING, "Password String");
    
    ASTNode *node = create_node(NODE_CMD_CREATE_USER);
    node->data.create_user.username = strdup(usr->value);
    node->data.create_user.password = strdup(pw->value);
    
    consume(ctx, TOKEN_SEMICOLON, ";");
    return node;
}

ASTNode* parse_create_db(ParserContext *ctx) {
    consume(ctx, TOKEN_KW_DATABASE, "Expected 'DATABASE'");
    
    if (current_token(ctx)->type == TOKEN_KW_BANAO) {
         consume(ctx, TOKEN_KW_BANAO, "BANAO");
    }

    Token *db = consume(ctx, TOKEN_IDENTIFIER, "Database Name");
    
    ASTNode *node = create_node(NODE_CMD_CREATE_DB);
    node->data.create_db.db_name = strdup(db->value);
    
    consume(ctx, TOKEN_SEMICOLON, ";");
    return node;
}

ASTNode* parse_show_tables(ParserContext *ctx) {
    consume(ctx, TOKEN_KW_show, "Expected 'SHOW'");
    consume(ctx, TOKEN_KW_TABLES, "Expected 'TABLES'");
    
    ASTNode *node = create_node(NODE_CMD_SHOW_TABLES);
    
    consume(ctx, TOKEN_SEMICOLON, ";");
    return node;
}

ASTNode* parse_create_index(ParserContext *ctx) {
    if (current_token(ctx)->type != TOKEN_KW_INDEX) {
         // Should be here if called from CREATE handler
         consume(ctx, TOKEN_KW_INDEX, "Expected 'INDEX'");
    } else {
         consume(ctx, TOKEN_KW_INDEX, "Expected 'INDEX'");
    }

    consume(ctx, TOKEN_KW_ON, "Expected 'ON'");
    
    ASTNode *node = create_node(NODE_CMD_CREATE_INDEX);
    Token *tbl = consume(ctx, TOKEN_IDENTIFIER, "Table Name");
    node->data.create_index.table_name = strdup(tbl->value);
    
    consume(ctx, TOKEN_LPAREN, "(");
    Token *col = consume(ctx, TOKEN_IDENTIFIER, "Column Name");
    node->data.create_index.col_name = strdup(col->value);
    consume(ctx, TOKEN_RPAREN, ")");
    
    consume(ctx, TOKEN_SEMICOLON, ";");
    return node;
}

// 1. Create Table: TABLE banao name (cols...) OR CREATE TABLE name ...
ASTNode* parse_create_table(ParserContext *ctx) {
    // If we came from 'CREATE', we might have consumed 'CREATE', and current is 'TABLE'.
    // If we came from 'TABLE BANAO', current is 'TABLE'.
    
    if (current_token(ctx)->type == TOKEN_KW_TABLE) {
        consume(ctx, TOKEN_KW_TABLE, "Expected 'TABLE'");
    }

    // Hinglish 'BANAO' check (Optional for standard SQL)
    if (current_token(ctx)->type == TOKEN_KW_BANAO) {
        consume(ctx, TOKEN_KW_BANAO, "Expected 'banao'");
    }
    
    ASTNode *node = create_node(NODE_CMD_CREATE_TABLE);
    Token *name = consume(ctx, TOKEN_IDENTIFIER, "Expected table name");
    node->data.create_table.table_name = strdup(name->value);

    consume(ctx, TOKEN_LPAREN, "Expected '('");

    // Parse columns
    // Simple fixed size for now or linked list. The struct uses array pointer.
    // Let's use a temporary dynamic array strategy or just one or two cols for this demo?
    // Let's implement properly with a realloc loop.
    int cap = 5;
    node->data.create_table.columns = malloc(sizeof(ColumnDef) * cap);
    node->data.create_table.col_count = 0;

    int active = 1;
    while (active) {
        if (node->data.create_table.col_count >= cap) {
            cap *= 2;
            node->data.create_table.columns = realloc(node->data.create_table.columns, sizeof(ColumnDef) * cap);
        }

        Token *colName = consume(ctx, TOKEN_IDENTIFIER, "Expected column name");
        Token *colType = NULL;
        if (current_token(ctx)->type == TOKEN_KW_INT) colType = consume(ctx, TOKEN_KW_INT, "Type");
        else if (current_token(ctx)->type == TOKEN_KW_FLOAT) colType = consume(ctx, TOKEN_KW_FLOAT, "Type");
        else if (current_token(ctx)->type == TOKEN_KW_STRING_TYPE) colType = consume(ctx, TOKEN_KW_STRING_TYPE, "Type");
        else if (current_token(ctx)->type == TOKEN_KW_TEXT_TYPE) colType = consume(ctx, TOKEN_KW_TEXT_TYPE, "Type");
        else if (current_token(ctx)->type == TOKEN_KW_BOOL_TYPE) colType = consume(ctx, TOKEN_KW_BOOL_TYPE, "Type");
        else {
             printf("Error: Expected type INT, FLOAT, STRING, TEXT or BOOL\n"); 
             longjmp(ctx->env, 1);
        }

        // Check for PRIMARY KEY
        int is_pk = 0;
        if (match(ctx, TOKEN_KW_PRIMARY)) {
            consume(ctx, TOKEN_KW_KEY, "Expected 'KEY' after 'PRIMARY'");
            is_pk = 1;
        }

        node->data.create_table.columns[node->data.create_table.col_count].name = strdup(colName->value);
        node->data.create_table.columns[node->data.create_table.col_count].type = strdup(colType->value ? colType->value : "TYPE");
        node->data.create_table.columns[node->data.create_table.col_count].is_pk = is_pk;
        node->data.create_table.col_count++;

        if (match(ctx, TOKEN_COMMA)) {
            continue;
        } else {
            active = 0;
        }
    }
    consume(ctx, TOKEN_RPAREN, "Expected ')'");
    consume(ctx, TOKEN_SEMICOLON, "Expected ';'");
    return node;
}

// 2. Insert: INSERT [karo] [INTO] table VALUES (...)
ASTNode* parse_insert(ParserContext *ctx) {
    consume(ctx, TOKEN_KW_INSERT, "Expected 'INSERT' or 'DAALO'");
    
    // Optional 'INTO' (Not tokenized explicitly, treated as identifier if we aren't careful)
    // Actually, SQL standard is INSERT INTO.
    // Lexer doesn't have INTO. It will be IDENTIFIER "INTO".
    // Hinglish "INSERT KARO".
    
    if (current_token(ctx)->type == TOKEN_KW_KARO) {
        consume(ctx, TOKEN_KW_KARO, "Expected 'karo'");
    } else if (current_token(ctx)->type == TOKEN_IDENTIFIER && strcasecmp(current_token(ctx)->value, "INTO") == 0) {
        consume(ctx, TOKEN_IDENTIFIER, "INTO");
    }

    ASTNode *node = create_node(NODE_CMD_INSERT);
    
    Token *name = consume(ctx, TOKEN_IDENTIFIER, "Expected table name");
    node->data.insert.table_name = strdup(name->value);

    consume(ctx, TOKEN_KW_VALUES, "Expected 'VALUES' or 'MAAN'");

// Loop for Multi-Row Values: (1, 'A'), (2, 'B'), ...
    RowValueList *row_head = NULL;
    RowValueList *row_tail = NULL;
    int row_active = 1;

    while (row_active) {
        consume(ctx, TOKEN_LPAREN, "Expected '('");
        
        // Parse One Row
        NodeList *head = NULL;
        NodeList *tail = NULL;
        int active = 1;
        while (active) {
            Token *val = NULL;
            int isAuto = 0;
            if (current_token(ctx)->type == TOKEN_STRING) val = consume(ctx, TOKEN_STRING, "Value");
            else if (current_token(ctx)->type == TOKEN_INT_LITERAL) val = consume(ctx, TOKEN_INT_LITERAL, "Value");
            else if (current_token(ctx)->type == TOKEN_FLOAT_LITERAL) val = consume(ctx, TOKEN_FLOAT_LITERAL, "Value");
            else if (current_token(ctx)->type == TOKEN_KW_AUTO) {
                 val = consume(ctx, TOKEN_KW_AUTO, "Auto");
                 isAuto = 1;
            }
            else {
                 printf("Error: Expected literal value or AUTO\n"); 
                 longjmp(ctx->env, 1);
            }

            NodeList *item = malloc(sizeof(NodeList));
            item->value = strdup(isAuto ? "AUTO" : val->value);
            item->next = NULL;

            if (!head) head = item;
            else tail->next = item;
            tail = item;

            if (match(ctx, TOKEN_COMMA)) continue;
            else active = 0;
        }
        consume(ctx, TOKEN_RPAREN, "Expected ')'");
        
        RowValueList *row = malloc(sizeof(RowValueList));
        row->values = head;
        row->next = NULL;
        if (!row_head) row_head = row;
        else row_tail->next = row;
        row_tail = row;

        if (match(ctx, TOKEN_COMMA)) continue;
        else row_active = 0;
    }
    
    node->data.insert.rows = row_head;

    consume(ctx, TOKEN_SEMICOLON, "Expected ';'");
    return node;
}

// 3. Select: SELECT cols FROM table [JAHAN expr] [SAMOOH DWARA col]
ASTNode* parse_select(ParserContext *ctx, int is_subquery) {
    if (current_token(ctx)->type == TOKEN_KW_DHUNDO)
        consume(ctx, TOKEN_KW_DHUNDO, "Expected 'DHUNDO'");
    else
        consume(ctx, TOKEN_KW_SELECT, "Expected 'SELECT'");

    if (current_token(ctx)->type == TOKEN_KW_KARO) {
        consume(ctx, TOKEN_KW_KARO, "Expected 'karo'");
    }
    
    ASTNode *node = create_node(NODE_CMD_SELECT);

    // Cols
    NodeList *head = NULL;
    NodeList *tail = NULL;
    
    if (current_token(ctx)->type == TOKEN_STAR) {
        consume(ctx, TOKEN_STAR, "*");
        NodeList *item = malloc(sizeof(NodeList));
        item->value = strdup("*");
        item->func_type = 0;
        item->next = NULL;
        head = item;
    } else {
        int active = 1;
        while (active) {
            // Check for Aggregates: COUNT(col)
            int func = 0;
            if (current_token(ctx)->type == TOKEN_KW_COUNT) { consume(ctx, TOKEN_KW_COUNT, "COUNT"); func = 1; consume(ctx, TOKEN_LPAREN, "("); }
            else if (current_token(ctx)->type == TOKEN_KW_SUM) { consume(ctx, TOKEN_KW_SUM, "SUM"); func = 2; consume(ctx, TOKEN_LPAREN, "("); }
            else if (current_token(ctx)->type == TOKEN_KW_AVG) { consume(ctx, TOKEN_KW_AVG, "AVG"); func = 3; consume(ctx, TOKEN_LPAREN, "("); }
            else if (current_token(ctx)->type == TOKEN_KW_MAX) { consume(ctx, TOKEN_KW_MAX, "MAX"); func = 4; consume(ctx, TOKEN_LPAREN, "("); }
            else if (current_token(ctx)->type == TOKEN_KW_MIN) { consume(ctx, TOKEN_KW_MIN, "MIN"); func = 5; consume(ctx, TOKEN_LPAREN, "("); }
            
            Token *col = NULL; 
            if (func == 1 && current_token(ctx)->type == TOKEN_STAR) {
                consume(ctx, TOKEN_STAR, "*");
                col = malloc(sizeof(Token)); col->value = "*"; // Hack
            } else {
                col = consume(ctx, TOKEN_IDENTIFIER, "Expected column name");
            }

            if (func > 0) consume(ctx, TOKEN_RPAREN, ")");

            NodeList *item = malloc(sizeof(NodeList));
            item->value = strdup(col->value);
            item->func_type = func;
            item->next = NULL;
            if (!head) head = item;
            else tail->next = item;
            tail = item;
    
            if (match(ctx, TOKEN_COMMA)) continue;
            else active = 0;
        }
    }
    node->data.select.columns = head;

    consume(ctx, TOKEN_KW_FROM, "Expected 'FROM'");
    Token *tbl = consume(ctx, TOKEN_IDENTIFIER, "Expected table name");
    node->data.select.table_name = strdup(tbl->value);

    // Optional WHERE
    if (match(ctx, TOKEN_KW_JAHAN)) {
        node->data.select.where_clause = parse_expression(ctx);
    }
    
    // Optional GROUP BY (SAMOOH DWARA)
    if (match(ctx, TOKEN_KW_GROUP)) {
        consume(ctx, TOKEN_KW_BY, "Expected 'BY/DWARA'");
        // Single column grouping for now
        Token *gcol = consume(ctx, TOKEN_IDENTIFIER, "Group Column");
        NodeList *g = malloc(sizeof(NodeList));
        g->value = strdup(gcol->value);
        g->next = NULL;
        node->data.select.group_by = g;
    }

    if (!is_subquery) {
        consume(ctx, TOKEN_SEMICOLON, "Expected ';'");
    }
    return node;
}

// Forward decl
ASTNode* parse_logical_or(ParserContext *ctx);

// Main Entry for Expression Parsing (Lowest Precedence)
ASTNode* parse_expression(ParserContext *ctx) {
    return parse_logical_or(ctx);
}

// Parse Logical AND (Higher Precedence than OR)
 ASTNode* parse_comparison(ParserContext *ctx);

ASTNode* parse_logical_and(ParserContext *ctx) {
    ASTNode *left = parse_comparison(ctx);

    while (current_token(ctx)->type == TOKEN_KW_AND) {
        consume(ctx, TOKEN_KW_AND, "AND");
        ASTNode *right = parse_comparison(ctx);
        
        ASTNode *node = create_node(NODE_EXPR_BINARY); // Reusing BINARY for logical
        node->data.binary_expr.left = left;
        node->data.binary_expr.right = right;
        node->data.binary_expr.op = strdup("AND");
        left = node;
    }
    return left;
}

// Parse Logical OR
ASTNode* parse_logical_or(ParserContext *ctx) {
    ASTNode *left = parse_logical_and(ctx);

    while (current_token(ctx)->type == TOKEN_KW_OR) {
        consume(ctx, TOKEN_KW_OR, "OR");
        ASTNode *right = parse_logical_and(ctx);
        
        ASTNode *node = create_node(NODE_EXPR_BINARY);
        node->data.binary_expr.left = left;
        node->data.binary_expr.right = right;
        node->data.binary_expr.op = strdup("OR");
        left = node;
    }
    return left;
}

// 4. Comparison: col OP value (Highest Precedence)
ASTNode* parse_comparison(ParserContext *ctx) {
    // Parenthesis check first
    if (match(ctx, TOKEN_LPAREN)) {
        ASTNode *node = parse_expression(ctx);
        consume(ctx, TOKEN_RPAREN, ")");
        return node;
    }

    ASTNode *node = create_node(NODE_EXPR_BINARY);
    
    // Left: Identifier
    Token *left = consume(ctx, TOKEN_IDENTIFIER, "Expected column in condition");
    ASTNode *leftNode = create_node(NODE_EXPR_IDENTIFIER);
    leftNode->data.literal.value = strdup(left->value); // Abuse literal struct for identifier
    node->data.binary_expr.left = leftNode;

    // Op
    Token *op = NULL;
    if (current_token(ctx)->type == TOKEN_EQUALS) op = consume(ctx, TOKEN_EQUALS, "=");
    else if (current_token(ctx)->type == TOKEN_GT) op = consume(ctx, TOKEN_GT, ">");
    else if (current_token(ctx)->type == TOKEN_LT) op = consume(ctx, TOKEN_LT, "<");
    else {
        printf("Error: Expected operator =, >, or <\n"); exit(1);
    }
    node->data.binary_expr.op = strdup(op->value);

    // Right: Literal or Subquery or Identifier
    // Check for Subquery: ( SELECT ... )
    if (current_token(ctx)->type == TOKEN_LPAREN) {
        // Lookahead to see if it's a subquery
        Token *peek = &ctx->tokens->tokens[ctx->current + 1]; // unsafe peek, but we assume valid buffer
        if (peek->type == TOKEN_KW_SELECT) {
            consume(ctx, TOKEN_LPAREN, "Expected '('");
            ASTNode *subquery = create_node(NODE_EXPR_SUBQUERY);
            subquery->data.subquery.subquery_stmt = parse_select(ctx, 1); // is_subquery=1
            consume(ctx, TOKEN_RPAREN, "Expected ')'");
            node->data.binary_expr.right = subquery;
        } else {
             // Paren expression? Not implemented yet
             printf("Error: Unexpected parenthesized expression.\n"); exit(1);
        }
    } else {
        // Literal
        Token *val = NULL;
        if (current_token(ctx)->type == TOKEN_INT_LITERAL) val = consume(ctx, TOKEN_INT_LITERAL, "val");
        else if (current_token(ctx)->type == TOKEN_STRING) val = consume(ctx, TOKEN_STRING, "val");
        else if (current_token(ctx)->type == TOKEN_FLOAT_LITERAL) val = consume(ctx, TOKEN_FLOAT_LITERAL, "val"); // Typo fix
        else {
             printf("Error: Expected value on RHS\n"); exit(1);
        }
        ASTNode *rightNode = create_node(NODE_EXPR_LITERAL);
        rightNode->data.literal.value = strdup(val->value);
        node->data.binary_expr.right = rightNode;
    }
    
    return node;
}

// 5. Document Insert: RAKHO collection json_string
ASTNode* parse_doc_insert(ParserContext *ctx) {
    consume(ctx, TOKEN_KW_RAKHO, "RAKHO");
    ASTNode *node = create_node(NODE_CMD_DOC_INSERT);
    
    Token *col = consume(ctx, TOKEN_IDENTIFIER, "Collection Name");
    node->data.doc_insert.collection = strdup(col->value);
    
    // JSON Payload - for now, expect a STRING token or { ... }
    // If Lexer tokenizes { id: 1 }, it's LBRACE IDENTIFIER COLON ...
    // The spec said "documents must be stored as serialized JSON strings".
    // If the user types: RAKHO logs "{\"a\":1}"; -> Single String token.
    // If they type: RAKHO logs { "a": 1 }; -> My lexer parses { "a" : 1 } as separate tokens.
    // For Phase 1, simpler to expect a STRING token for the JSON body, or we essentially concatenate tokens until semicolon.
    // Let's assume the user passes a STRING literal for the JSON.
    // Or we consume until semicolon and treat as "raw json string".
    
    // To support `RAKHO products {"id": 1};`
    // I need to parse the object structure or just grab it as a blob.
    // Let's try to consume a 'JSON-like' sequence.
    
    // Simplified: Expect a single STRING token if the user quotes it, OR start with { and end with }.
    if (current_token(ctx)->type == TOKEN_LBRACE) {
        // Reconstruct string from tokens until RBRACE?
        // This is tricky without a raw mode.
        // Let's just create a dummy string "{json}" for now or error.
        // Actually, let's just loop until RBRACE.
        char buffer[1024] = "{";
        consume(ctx, TOKEN_LBRACE, "{");
        while (current_token(ctx)->type != TOKEN_RBRACE && current_token(ctx)->type != TOKEN_EOF) {
            strcat(buffer, current_token(ctx)->value);
            // Add space?
            strcat(buffer, " ");
            advance(ctx);
        }
        consume(ctx, TOKEN_RBRACE, "}");
        strcat(buffer, "}");
        node->data.doc_insert.json_body = strdup(buffer);
    } else {
        Token *json = consume(ctx, TOKEN_STRING, "JSON String");
        node->data.doc_insert.json_body = strdup(json->value);
    }

    consume(ctx, TOKEN_SEMICOLON, ";");
    return node;
}

// 6. Delete From: NIKALO FROM table JAHAN ...
ASTNode* parse_delete(ParserContext *ctx) {
    consume(ctx, TOKEN_KW_NIKALO, "NIKALO");
    if (match(ctx, TOKEN_KW_FROM)) {
        // user typed NIKALO FROM table
    } 
    // If just NIKALO table? Let's strictly follow SQL-ish or Hinglish
    // User asked "delete from table".
    // Hinglish: NIKALO table-name SE ... but SE isn't keyword.
    // Let's support: NIKALO FROM table
    
    ASTNode *node = create_node(NODE_CMD_DELETE);
    Token *tbl = consume(ctx, TOKEN_IDENTIFIER, "Table name");
    node->data.delete_stmt.table_name = strdup(tbl->value);
    
    if (match(ctx, TOKEN_KW_JAHAN)) {
        node->data.delete_stmt.where_clause = parse_expression(ctx);
    }
    
    consume(ctx, TOKEN_SEMICOLON, ";");
    return node;
}

// 7. Drop Table: TABLE GIRAO name
ASTNode* parse_drop_table(ParserContext *ctx) {
    consume(ctx, TOKEN_KW_TABLE, "TABLE");
    consume(ctx, TOKEN_KW_GIRAO, "GIRAO");
    ASTNode *node = create_node(NODE_CMD_DROP_TABLE);
    Token *name = consume(ctx, TOKEN_IDENTIFIER, "Table Name");
    node->data.drop_table.table_name = strdup(name->value);
    consume(ctx, TOKEN_SEMICOLON, ";");
    return node;
}

// 8. Doc Get: MANGWAO collection [id]
ASTNode* parse_doc_get(ParserContext *ctx) {
    consume(ctx, TOKEN_KW_MANGWAO, "MANGWAO");
    ASTNode *node = create_node(NODE_CMD_DOC_GET);
    
    Token *col = consume(ctx, TOKEN_IDENTIFIER, "Collection Name");
    node->data.doc_get.collection = strdup(col->value);
    
    // Optional ID (String or Int)
    if (current_token(ctx)->type == TOKEN_STRING) {
        Token *val = consume(ctx, TOKEN_STRING, "ID");
        node->data.doc_get.doc_id = strdup(val->value);
    } else if (current_token(ctx)->type == TOKEN_INT_LITERAL) {
         Token *val = consume(ctx, TOKEN_INT_LITERAL, "ID");
         node->data.doc_get.doc_id = strdup(val->value);
    } else {
        node->data.doc_get.doc_id = NULL; // Get All
    }
    
    consume(ctx, TOKEN_SEMICOLON, ";");
    return node;
}

// 9. Doc Remove: HATAO collection id
ASTNode* parse_doc_remove(ParserContext *ctx) {
    consume(ctx, TOKEN_KW_HATAO, "HATAO");
    ASTNode *node = create_node(NODE_CMD_DOC_REMOVE);
    
    Token *col = consume(ctx, TOKEN_IDENTIFIER, "Collection Name");
    node->data.doc_remove.collection = strdup(col->value);
    
    // ID is mandatory for now? Or allow delete all?
    // Let's enforce ID for safety "like deleteOne"
    // "HATAO user 1"
    
    if (current_token(ctx)->type == TOKEN_STRING) {
        Token *val = consume(ctx, TOKEN_STRING, "ID");
        node->data.doc_remove.doc_id = strdup(val->value);
    } else if (current_token(ctx)->type == TOKEN_INT_LITERAL) {
         Token *val = consume(ctx, TOKEN_INT_LITERAL, "ID");
         node->data.doc_remove.doc_id = strdup(val->value);
    } else {
         printf("Error: Expected Document ID (String or Int)\n");
         longjmp(ctx->env, 1);
    }
    
    consume(ctx, TOKEN_SEMICOLON, ";");
    return node;
}

ASTNode* parse_checkpoint(ParserContext *ctx) {
    consume(ctx, TOKEN_KW_CHECKPOINT, "CHECKPOINT");
    ASTNode *node = create_node(NODE_CMD_CHECKPOINT);
    consume(ctx, TOKEN_SEMICOLON, ";");
    return node;
}

ASTNode* parse_use_db(ParserContext *ctx) {
    consume(ctx, TOKEN_KW_USE, "USE");
    ASTNode *node = malloc(sizeof(ASTNode)); // Explicit malloc to be safe
    node->type = NODE_CMD_USE_DB;
    
    // Allow 'DATABASE' optional
    if (current_token(ctx)->type == TOKEN_KW_DATABASE) advance(ctx);
    
    node->data.use_db.db_name = strdup(current_token(ctx)->value);
    consume(ctx, TOKEN_IDENTIFIER, "database name");
    if (current_token(ctx)->type == TOKEN_SEMICOLON) advance(ctx);
    return node;
}

ASTNode* parse(TokenList *tokens) {
    ParserContext ctx;
    ctx.tokens = tokens;
    ctx.current = 0;
    // ctx.env is initialized by setjmp below
    
    if (setjmp(ctx.env) != 0) {
        // Error occurred
        return NULL;
    }

    Token *t = current_token(&ctx);

    if (t->type == TOKEN_KW_USE) return parse_use_db(&ctx);

    // SQL Standard CREATE HANDLING or Hinglish BANAO prefix
    if (t->type == TOKEN_KW_CREATE || t->type == TOKEN_KW_BANAO) {
        if (t->type == TOKEN_KW_CREATE) consume(&ctx, TOKEN_KW_CREATE, "CREATE"); 
        else consume(&ctx, TOKEN_KW_BANAO, "BANAO");
        
        // Look ahead
        if (current_token(&ctx)->type == TOKEN_KW_INDEX) return parse_create_index(&ctx);
        if (current_token(&ctx)->type == TOKEN_KW_USER) return parse_create_user(&ctx);
        if (current_token(&ctx)->type == TOKEN_KW_DATABASE) return parse_create_db(&ctx);
        // Table?
        if (current_token(&ctx)->type == TOKEN_KW_TABLE) return parse_create_table(&ctx);
        
        // If simply 'BANAO name ...' (Implicit table creation? No, stay strict)
        printf("Error: CREATE/BANAO [INDEX|USER|DATABASE|TABLE] expected.\n"); 
        return NULL;
    }
    
    if (t->type == TOKEN_KW_show) {
        return parse_show_tables(&ctx);
    }

    if (t->type == TOKEN_KW_KEY) { 
        // Handles 'PRIMARY KEY' if weirdly starts? No.
        printf("Unexpected KEY token\n"); return NULL; 
    }
    
    // Hinglish direct Object-Verb starts
    if (t->type == TOKEN_KW_USER) return parse_create_user(&ctx);
    if (t->type == TOKEN_KW_DATABASE) return parse_create_db(&ctx);

    if (t->type == TOKEN_KW_TABLE) {
        // Check next token to distinguish CREATE vs DROP
        if (ctx.tokens->count > ctx.current + 1) {
            Token *next = &ctx.tokens->tokens[ctx.current + 1];
            if (next->type == TOKEN_KW_GIRAO) return parse_drop_table(&ctx);
        }
        return parse_create_table(&ctx);
    } else if (t->type == TOKEN_KW_INSERT) {
        return parse_insert(&ctx);
    } else if (t->type == TOKEN_KW_SELECT || t->type == TOKEN_KW_DHUNDO) {
        return parse_select(&ctx, 0);
    } else if (t->type == TOKEN_KW_RAKHO) {
        return parse_doc_insert(&ctx);
    } else if (t->type == TOKEN_KW_MANGWAO) {
        return parse_doc_get(&ctx);
    } else if (t->type == TOKEN_KW_HATAO) {
        return parse_doc_remove(&ctx);
    } else if (t->type == TOKEN_KW_NIKALO) {
        return parse_delete(&ctx);
    } else if (t->type == TOKEN_KW_CHECKPOINT) {
        return parse_checkpoint(&ctx);
    } else if (t->type == TOKEN_KW_INDEX) {
        // "INDEX ON ..."
        return parse_create_index(&ctx); 
    } else if (t->type == TOKEN_IDENTIFIER && strcasecmp(t->value, "CREATE") == 0) {
        // Handle standard SQL "CREATE INDEX"
        consume(&ctx, TOKEN_IDENTIFIER, "CREATE");
        // Next token should be INDEX
        if (current_token(&ctx)->type == TOKEN_KW_INDEX) {
             // Let parse_create_index consume it
             return parse_create_index(&ctx);
        } else {
             printf("Error: Expected INDEX after CREATE\n");
             return NULL;
        }
    } else {
        printf("Unknown command or unexpected token: %s\n", t->value);
        return NULL;
    }
}

void print_indent(int level) {
    for (int i=0; i<level; i++) printf("  ");
}

void print_node(ASTNode *node, int level) {
    if (!node) return;
    print_indent(level);
    
    switch (node->type) {
        case NODE_CMD_CREATE_TABLE:
            printf("CREATE TABLE: %s\n", node->data.create_table.table_name);
            for (int i=0; i<node->data.create_table.col_count; i++) {
                print_indent(level+1);
                printf("Col: %s (%s)\n", node->data.create_table.columns[i].name, node->data.create_table.columns[i].type);
            }
            break;
        case NODE_CMD_CREATE_USER:
             printf("CREATE USER: %s\n", node->data.create_user.username);
             break;
        case NODE_CMD_CREATE_DB:
             printf("CREATE DATABASE: %s\n", node->data.create_db.db_name);
             break;
        case NODE_CMD_SHOW_TABLES:
             printf("SHOW TABLES\n");
             break;
        case NODE_CMD_USE_DB:
             printf("USE DB: %s\n", node->data.use_db.db_name);
             break;
        case NODE_CMD_INSERT:
            printf("INSERT INTO: %s\n", node->data.insert.table_name);
            RowValueList *row = node->data.insert.rows;
            int r = 1;
            while(row) {
                print_indent(level+1);
                printf("Row %d:\n", r++);
                NodeList *cur = row->values;
                while(cur) {
                    print_indent(level+2);
                    printf("Val: %s\n", cur->value);
                    cur = cur->next;
                }
                row = row->next;
            }
            break;
        case NODE_CMD_SELECT:
            printf("SELECT FROM: %s\n", node->data.select.table_name);
            if (node->data.select.where_clause) {
                print_indent(level+1);
                printf("WHERE:\n");
                print_node(node->data.select.where_clause, level+2);
            }
            break;
        case NODE_EXPR_BINARY:
            printf("Op: %s\n", node->data.binary_expr.op);
            print_node(node->data.binary_expr.left, level+1);
            print_node(node->data.binary_expr.right, level+1);
            break;
        case NODE_EXPR_IDENTIFIER:
            printf("ID: %s\n", node->data.literal.value);
            break;
        case NODE_EXPR_LITERAL:
            printf("Lit: %s\n", node->data.literal.value);
            break;
        case NODE_EXPR_SUBQUERY:
            printf("SUBQUERY:\n");
            print_node(node->data.subquery.subquery_stmt, level+1);
            break;
        case NODE_CMD_DOC_INSERT:
            printf("DOC INSERT: %s values %s\n", node->data.doc_insert.collection, node->data.doc_insert.json_body);
            break;
        case NODE_CMD_DOC_GET:
            printf("DOC GET: %s (ID: %s)\n", node->data.doc_get.collection, node->data.doc_get.doc_id ? node->data.doc_get.doc_id : "ALL");
            break;
        case NODE_CMD_DOC_REMOVE:
            printf("DOC REMOVE: %s (ID: %s)\n", node->data.doc_remove.collection, node->data.doc_remove.doc_id);
            break;
        default:
            printf("Unknown Node Type %d\n", node->type);
    }
}

void free_node_list(NodeList *list) {
    while (list) {
        NodeList *next = list->next;
        if (list->value) free(list->value);
        if (list->alias) free(list->alias);
        free(list);
        list = next;
    }
}

void free_row_list(RowValueList *rows) {
    while(rows) {
        RowValueList *next = rows->next;
        free_node_list(rows->values);
        free(rows);
        rows = next;
    }
}

void free_ast(ASTNode *node) {
    if (!node) return;
    
    switch(node->type) {
        case NODE_CMD_CREATE_TABLE:
            free(node->data.create_table.table_name);
            for(int i=0; i<node->data.create_table.col_count; i++) {
                free(node->data.create_table.columns[i].name);
                free(node->data.create_table.columns[i].type);
            }
            free(node->data.create_table.columns);
            break;
        case NODE_CMD_INSERT:
            free(node->data.insert.table_name);
            free_row_list(node->data.insert.rows);
            break;
        case NODE_CMD_SELECT:
            free(node->data.select.table_name);
            free_node_list(node->data.select.columns);
            free_node_list(node->data.select.group_by);
            free_ast(node->data.select.where_clause);
            break;
        case NODE_EXPR_BINARY:
            free(node->data.binary_expr.op);
            free_ast(node->data.binary_expr.left);
            free_ast(node->data.binary_expr.right);
            break;
        case NODE_EXPR_LOGICAL: // Reuses binary structure in union? 
            // Currently logical nodes are NODE_EXPR_BINARY with op="AND"
            // If they were distinct, we'd handle them here.
            break; 
        case NODE_EXPR_SUBQUERY:
            free_ast(node->data.subquery.subquery_stmt);
            break;
        case NODE_EXPR_IDENTIFIER:
        case NODE_EXPR_LITERAL:
            free(node->data.literal.value);
            break;
        // ... Add other cases as needed ...
        default:
            // Minimal cleanup for string types that are just pointers not deep structs
            break;
    }
    
    free(node);
}

void print_node(ASTNode *node, int level); // Fwd decl

void print_ast(ASTNode *node) {
    if (!node) return;
    print_node(node, 0);
}
