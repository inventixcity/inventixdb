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

// 1. Create Table: TABLE banao name (cols...)
ASTNode* parse_create_table(ParserContext *ctx) {
    consume(ctx, TOKEN_KW_TABLE, "Expected 'TABLE'");
    consume(ctx, TOKEN_KW_BANAO, "Expected 'banao'");
    
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

// 2. Insert: INSERT karo table VALUES (...)
ASTNode* parse_insert(ParserContext *ctx) {
    consume(ctx, TOKEN_KW_INSERT, "Expected 'INSERT'");
    consume(ctx, TOKEN_KW_KARO, "Expected 'karo'");
    ASTNode *node = create_node(NODE_CMD_INSERT);
    
    Token *name = consume(ctx, TOKEN_IDENTIFIER, "Expected table name");
    node->data.insert.table_name = strdup(name->value);

    consume(ctx, TOKEN_KW_VALUES, "Expected 'VALUES'");
    consume(ctx, TOKEN_LPAREN, "Expected '('");

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
    node->data.insert.values = head;

    consume(ctx, TOKEN_RPAREN, "Expected ')'");
    consume(ctx, TOKEN_SEMICOLON, "Expected ';'");
    return node;
}

// 3. Select: SELECT cols FROM table [JAHAN expr]
ASTNode* parse_select(ParserContext *ctx, int is_subquery) {
    consume(ctx, TOKEN_KW_SELECT, "Expected 'SELECT'");
    // Removed strict requirement for 'KARO' in SELECT to match user expectation (Syntax Step 1)
    if (current_token(ctx)->type == TOKEN_KW_KARO) {
        consume(ctx, TOKEN_KW_KARO, "Expected 'karo'");
    }
    
    ASTNode *node = create_node(NODE_CMD_SELECT);

    // Cols
    NodeList *head = NULL;
    NodeList *tail = NULL;
    
    // Check for * (not explicitly tokenized as STAR, but let's assume * is allowed or only identifiers)
    // My lexer doesn't have STAR token. Using "IDENTIFIER" for now or assume user types columns.
    // Let's assume user must type list of cols.
    
    int active = 1;
    while (active) {
        Token *col = consume(ctx, TOKEN_IDENTIFIER, "Expected column name");
        NodeList *item = malloc(sizeof(NodeList));
        item->value = strdup(col->value);
        item->next = NULL;
        if (!head) head = item;
        else tail->next = item;
        tail = item;

        if (match(ctx, TOKEN_COMMA)) continue;
        else active = 0;
    }
    node->data.select.columns = head;

    consume(ctx, TOKEN_KW_FROM, "Expected 'FROM'");
    Token *tbl = consume(ctx, TOKEN_IDENTIFIER, "Expected table name");
    node->data.select.table_name = strdup(tbl->value);

    // Optional WHERE
    if (match(ctx, TOKEN_KW_JAHAN)) {
        node->data.select.where_clause = parse_expression(ctx);
    }

    if (!is_subquery) {
        consume(ctx, TOKEN_SEMICOLON, "Expected ';'");
    }
    return node;
}

// 4. Expression: col OP value OR col OP (Subquery)
ASTNode* parse_expression(ParserContext *ctx) {
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

ASTNode* parse(TokenList *tokens) {
    ParserContext ctx = { tokens, 0 };
    
    if (setjmp(ctx.env) != 0) {
        // Error occurred
        return NULL;
    }

    Token *t = current_token(&ctx);

    if (t->type == TOKEN_KW_TABLE) {
        // Check next token to distinguish CREATE vs DROP
        if (ctx.tokens->count > ctx.current + 1) {
            Token *next = &ctx.tokens->tokens[ctx.current + 1];
            if (next->type == TOKEN_KW_GIRAO) return parse_drop_table(&ctx);
        }
        return parse_create_table(&ctx);
    } else if (t->type == TOKEN_KW_INSERT) {
        return parse_insert(&ctx);
    } else if (t->type == TOKEN_KW_SELECT) {
        return parse_select(&ctx, 0);
    } else if (t->type == TOKEN_KW_RAKHO) {
        return parse_doc_insert(&ctx);
    } else if (t->type == TOKEN_KW_NIKALO) {
        return parse_delete(&ctx);
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
        case NODE_CMD_INSERT:
            printf("INSERT INTO: %s\n", node->data.insert.table_name);
            NodeList *cur = node->data.insert.values;
            while(cur) {
                print_indent(level+1);
                printf("Val: %s\n", cur->value);
                cur = cur->next;
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
        default:
            printf("Unknown Node Type %d\n", node->type);
    }
}

void print_ast(ASTNode *node) {
    print_node(node, 0);
}
