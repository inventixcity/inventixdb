#ifndef INVENTIX_LEXER_H
#define INVENTIX_LEXER_H

typedef enum {
    TOKEN_EOF,
    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_INT_LITERAL,
    TOKEN_FLOAT_LITERAL,
    
    // Keywords
    TOKEN_KW_TABLE,
    TOKEN_KW_BANAO,
    TOKEN_KW_INSERT,
    TOKEN_KW_KARO,
    TOKEN_KW_SELECT,
    TOKEN_KW_JAHAN,
    TOKEN_KW_RAKHO,
    TOKEN_KW_DHUNDO,
    TOKEN_KW_NIKALO,
    TOKEN_KW_FROM,
    TOKEN_KW_VALUES,
    TOKEN_KW_INT,
    TOKEN_KW_FLOAT,
    TOKEN_KW_STRING_TYPE,
    TOKEN_KW_TEXT_TYPE,
    TOKEN_KW_BOOL_TYPE,
    TOKEN_KW_AUTO, // For Auto Increment
    TOKEN_KW_PRIMARY,
    TOKEN_KW_KEY,
    TOKEN_KW_GIRAO, // DROP
    TOKEN_KW_MANGWAO, // GET (NoSQL)
    TOKEN_KW_HATAO,   // DELETE (NoSQL)
    TOKEN_KW_CHECKPOINT, // Manual Snapshot

    TOKEN_KW_INDEX, // INDEX
    TOKEN_KW_ON,    // ON

    // Auth & System
    TOKEN_KW_CREATE,  // CREATE
    TOKEN_KW_USER,    // USER
    TOKEN_KW_PASSWORD, // PASSWORD
    TOKEN_KW_DATABASE, // DATABASE
    TOKEN_KW_show,     // SHOW
    TOKEN_KW_TABLES,   // TABLES
    TOKEN_KW_USE,      // USE

    // Logical & Grouping (Step 10)
    TOKEN_KW_AND,      // AUR
    TOKEN_KW_OR,       // YA
    TOKEN_KW_GROUP,    // SAMOOH
    TOKEN_KW_BY,       // DWARA
    TOKEN_KW_COUNT,    // GINO
    TOKEN_KW_SUM,      // JODO
    TOKEN_KW_AVG,      // AUSAAT
    TOKEN_KW_MIN,      // SABSE_CHOTA
    TOKEN_KW_MAX,      // SABSE_BADA

    // Symbols
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_COMMA,
    TOKEN_EQUALS,
    TOKEN_GT,
    TOKEN_LT,
    TOKEN_SEMICOLON,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_COLON,
    TOKEN_STAR // *
} LexerTokenType;

typedef struct {
    LexerTokenType type;
    char *value;
    int line;
} Token;

typedef struct {
    Token *tokens;
    int count;
    int capacity;
} TokenList;

TokenList* tokenize(const char *input);
void free_token_list(TokenList *list);
const char* token_type_to_string(LexerTokenType type);

#endif
