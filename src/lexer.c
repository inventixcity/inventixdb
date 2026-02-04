#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"

void add_token(TokenList *list, LexerTokenType type, const char *value, int line) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 10 : list->capacity * 2;
        list->tokens = realloc(list->tokens, sizeof(Token) * list->capacity);
    }
    list->tokens[list->count].type = type;
    list->tokens[list->count].value = value ? strdup(value) : NULL;
    list->tokens[list->count].line = line;
    list->count++;
}

LexerTokenType check_keyword(const char *str) {
    if (strcasecmp(str, "TABLE") == 0) return TOKEN_KW_TABLE;
    if (strcasecmp(str, "BANAO") == 0) return TOKEN_KW_BANAO;
    if (strcasecmp(str, "INSERT") == 0) return TOKEN_KW_INSERT;
    if (strcasecmp(str, "KARO") == 0) return TOKEN_KW_KARO;
    if (strcasecmp(str, "SELECT") == 0) return TOKEN_KW_SELECT;
    if (strcasecmp(str, "JAHAN") == 0) return TOKEN_KW_JAHAN;
    if (strcasecmp(str, "RAKHO") == 0) return TOKEN_KW_RAKHO;
    if (strcasecmp(str, "DHUNDO") == 0) return TOKEN_KW_DHUNDO;
    if (strcasecmp(str, "NIKALO") == 0) return TOKEN_KW_NIKALO;
    if (strcasecmp(str, "FROM") == 0) return TOKEN_KW_FROM;
    if (strcasecmp(str, "VALUES") == 0) return TOKEN_KW_VALUES;
    if (strcasecmp(str, "INT") == 0) return TOKEN_KW_INT;
    if (strcasecmp(str, "INTEGER") == 0) return TOKEN_KW_INT;
    if (strcasecmp(str, "FLOAT") == 0) return TOKEN_KW_FLOAT;
    if (strcasecmp(str, "DOUBLE") == 0) return TOKEN_KW_FLOAT;
    if (strcasecmp(str, "STRING") == 0) return TOKEN_KW_STRING_TYPE;
    if (strcasecmp(str, "TEXT") == 0) return TOKEN_KW_TEXT_TYPE;
    if (strcasecmp(str, "BOOL") == 0) return TOKEN_KW_BOOL_TYPE;
    if (strcasecmp(str, "BOOLEAN") == 0) return TOKEN_KW_BOOL_TYPE;
    if (strcasecmp(str, "AUTO") == 0) return TOKEN_KW_AUTO;
    if (strcasecmp(str, "PRIMARY") == 0) return TOKEN_KW_PRIMARY;
    if (strcasecmp(str, "KEY") == 0) return TOKEN_KW_KEY;
    if (strcasecmp(str, "GIRAO") == 0) return TOKEN_KW_GIRAO;
    if (strcasecmp(str, "MITAO") == 0) return TOKEN_KW_GIRAO; // Alias
    return TOKEN_IDENTIFIER;
}

TokenList* tokenize(const char *input) {
    TokenList *list = malloc(sizeof(TokenList));
    list->tokens = NULL;
    list->count = 0;
    list->capacity = 0;
    
    int line = 1;
    int i = 0;
    while (input[i] != '\0') {
        if (isspace(input[i])) {
            if (input[i] == '\n') line++;
            i++;
            continue;
        }

        // Parentheses and Symbols
        if (input[i] == '(') { add_token(list, TOKEN_LPAREN, "(", line); i++; continue; }
        if (input[i] == ')') { add_token(list, TOKEN_RPAREN, ")", line); i++; continue; }
        if (input[i] == '{') { add_token(list, TOKEN_LBRACE, "{", line); i++; continue; }
        if (input[i] == '}') { add_token(list, TOKEN_RBRACE, "}", line); i++; continue; }
        if (input[i] == ',') { add_token(list, TOKEN_COMMA, ",", line); i++; continue; }
        if (input[i] == ';') { add_token(list, TOKEN_SEMICOLON, ";", line); i++; continue; }
        if (input[i] == '=') { add_token(list, TOKEN_EQUALS, "=", line); i++; continue; }
        if (input[i] == ':') { add_token(list, TOKEN_COLON, ":", line); i++; continue; }
        if (input[i] == '>') { add_token(list, TOKEN_GT, ">", line); i++; continue; }
        if (input[i] == '<') { add_token(list, TOKEN_LT, "<", line); i++; continue; }

        // Strings
        if (input[i] == '"') {
            i++;
            int start = i;
            while (input[i] != '"' && input[i] != '\0') i++;
            int len = i - start;
            char *str = malloc(len + 1);
            strncpy(str, input + start, len);
            str[len] = '\0';
            add_token(list, TOKEN_STRING, str, line);
            free(str);
            if (input[i] == '"') i++;
            continue;
        }

        // Numbers (Int and Float)
        if (isdigit(input[i])) {
            int start = i;
            int isFloat = 0;
            while (isdigit(input[i]) || input[i] == '.') {
                if (input[i] == '.') isFloat = 1;
                i++;
            }
            int len = i - start;
            char *num = malloc(len + 1);
            strncpy(num, input + start, len);
            num[len] = '\0';
            add_token(list, isFloat ? TOKEN_FLOAT_LITERAL : TOKEN_INT_LITERAL, num, line);
            free(num);
            continue;
        }

        // Identifiers and Keywords
        if (isalpha(input[i]) || input[i] == '_') {
            int start = i;
            while (isalnum(input[i]) || input[i] == '_') i++;
            int len = i - start;
            char *word = malloc(len + 1);
            strncpy(word, input + start, len);
            word[len] = '\0';
            
            LexerTokenType type = check_keyword(word);
            add_token(list, type, (type == TOKEN_IDENTIFIER) ? word : NULL, line);
            free(word);
            continue;
        }

        // Unknown character
        printf("Unexpected character: %c at line %d\n", input[i], line);
        i++;
    }
    
    add_token(list, TOKEN_EOF, NULL, line);
    return list;
}

void free_token_list(TokenList *list) {
    for (int i = 0; i < list->count; i++) {
        if (list->tokens[i].value) free(list->tokens[i].value);
    }
    free(list->tokens);
    free(list);
}

const char* token_type_to_string(LexerTokenType type) {
    switch (type) {
        case TOKEN_EOF: return "EOF";
        case TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TOKEN_STRING: return "STRING";
        case TOKEN_INT_LITERAL: return "INT";
        case TOKEN_FLOAT_LITERAL: return "FLOAT";
        case TOKEN_KW_TABLE: return "TABLE";
        case TOKEN_KW_BANAO: return "BANAO";
        case TOKEN_KW_INSERT: return "INSERT";
        case TOKEN_KW_KARO: return "KARO";
        case TOKEN_KW_SELECT: return "SELECT";
        case TOKEN_KW_JAHAN: return "JAHAN";
        case TOKEN_KW_RAKHO: return "RAKHO";
        case TOKEN_KW_DHUNDO: return "DHUNDO";
        case TOKEN_KW_NIKALO: return "NIKALO";
        case TOKEN_KW_FROM: return "FROM";
        case TOKEN_KW_VALUES: return "VALUES";
        case TOKEN_KW_INT: return "INT";
        case TOKEN_KW_FLOAT: return "FLOAT";
        case TOKEN_KW_STRING_TYPE: return "STRING_TYPE";
        case TOKEN_KW_TEXT_TYPE: return "TEXT";
        case TOKEN_KW_BOOL_TYPE: return "BOOL";
        case TOKEN_KW_AUTO: return "AUTO";
        case TOKEN_KW_PRIMARY: return "PRIMARY";
        case TOKEN_KW_KEY: return "KEY";
        case TOKEN_KW_GIRAO: return "GIRAO";
        case TOKEN_LPAREN: return "(";
        case TOKEN_RPAREN: return ")";
        case TOKEN_COMMA: return ",";
        case TOKEN_EQUALS: return "=";
        case TOKEN_GT: return ">";
        case TOKEN_LT: return "<";
        case TOKEN_SEMICOLON: return ";";
        default: return "UNKNOWN";
    }
}
