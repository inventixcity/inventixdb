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
    if (strcasecmp(str, "DAALO") == 0) return TOKEN_KW_INSERT; // Hinglish
    if (strcasecmp(str, "KARO") == 0) return TOKEN_KW_KARO;
    if (strcasecmp(str, "SELECT") == 0) return TOKEN_KW_SELECT;
    if (strcasecmp(str, "CHUNO") == 0) return TOKEN_KW_SELECT; // Hinglish alternate
    if (strcasecmp(str, "JAHAN") == 0) return TOKEN_KW_JAHAN;
    if (strcasecmp(str, "RAKHO") == 0) return TOKEN_KW_RAKHO;
    if (strcasecmp(str, "DHUNDO") == 0) return TOKEN_KW_DHUNDO;
    if (strcasecmp(str, "DHUNDHO") == 0) return TOKEN_KW_DHUNDO; // Alias
    if (strcasecmp(str, "NIKALO") == 0) return TOKEN_KW_NIKALO;
    if (strcasecmp(str, "MANGWAO") == 0) return TOKEN_KW_MANGWAO; // Get
    if (strcasecmp(str, "LAAO") == 0) return TOKEN_KW_MANGWAO;    // Get Alias
    if (strcasecmp(str, "HATAO") == 0) return TOKEN_KW_HATAO;     // Remove
    if (strcasecmp(str, "CHECKPOINT") == 0) return TOKEN_KW_CHECKPOINT;
    if (strcasecmp(str, "FROM") == 0) return TOKEN_KW_FROM;
    if (strcasecmp(str, "SE") == 0) return TOKEN_KW_FROM;         // Hinglish FROM
    if (strcasecmp(str, "VALUES") == 0) return TOKEN_KW_VALUES;
    if (strcasecmp(str, "MAAN") == 0) return TOKEN_KW_VALUES;     // Hinglish VALUES
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
    if (strcasecmp(str, "INDEX") == 0) return TOKEN_KW_INDEX;
    if (strcasecmp(str, "ON") == 0) return TOKEN_KW_ON;
    if (strcasecmp(str, "CREATE") == 0) return TOKEN_KW_CREATE;
    if (strcasecmp(str, "USER") == 0) return TOKEN_KW_USER;
    if (strcasecmp(str, "PASSWORD") == 0) return TOKEN_KW_PASSWORD;
    if (strcasecmp(str, "DATABASE") == 0) return TOKEN_KW_DATABASE;
    if (strcasecmp(str, "SHOW") == 0) return TOKEN_KW_show;
    if (strcasecmp(str, "TABLES") == 0) return TOKEN_KW_TABLES;
    if (strcasecmp(str, "USE") == 0) return TOKEN_KW_USE;
    if (strcasecmp(str, "ISTEMAAL") == 0) return TOKEN_KW_USE; // Hinglish Alias
    if (strcasecmp(str, "DELETE") == 0) return TOKEN_KW_NIKALO; 
    if (strcasecmp(str, "DROP") == 0) return TOKEN_KW_GIRAO;
    if (strcasecmp(str, "WHERE") == 0) return TOKEN_KW_JAHAN;
    
    // Logical & Grouping
    if (strcasecmp(str, "AND") == 0) return TOKEN_KW_AND;
    if (strcasecmp(str, "AUR") == 0) return TOKEN_KW_AND;
    if (strcasecmp(str, "OR") == 0) return TOKEN_KW_OR;
    if (strcasecmp(str, "YA") == 0) return TOKEN_KW_OR;
    
    if (strcasecmp(str, "GROUP") == 0) return TOKEN_KW_GROUP;
    if (strcasecmp(str, "SAMOOH") == 0) return TOKEN_KW_GROUP;
    if (strcasecmp(str, "BY") == 0) return TOKEN_KW_BY;
    if (strcasecmp(str, "DWARA") == 0) return TOKEN_KW_BY;
    
    if (strcasecmp(str, "COUNT") == 0) return TOKEN_KW_COUNT;
    if (strcasecmp(str, "GINO") == 0) return TOKEN_KW_COUNT;
    if (strcasecmp(str, "SUM") == 0) return TOKEN_KW_SUM;
    if (strcasecmp(str, "JODO") == 0) return TOKEN_KW_SUM;
    if (strcasecmp(str, "AVG") == 0) return TOKEN_KW_AVG;
    if (strcasecmp(str, "AUSAAT") == 0) return TOKEN_KW_AVG;
    if (strcasecmp(str, "MAX") == 0) return TOKEN_KW_MAX;
    if (strcasecmp(str, "SABSE_BADA") == 0) return TOKEN_KW_MAX;
    if (strcasecmp(str, "MIN") == 0) return TOKEN_KW_MIN;
    if (strcasecmp(str, "SABSE_CHOTA") == 0) return TOKEN_KW_MIN;

    if (strcasecmp(str, "FROM") == 0) return TOKEN_KW_FROM; // Already likely there?
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
        if (input[i] == '*') { add_token(list, TOKEN_STAR, "*", line); i++; continue; }

        // Strings with Escape Support
        if (input[i] == '"') {
            i++;
            // int start = i; // Unused
            // First pass: calculate length
            int len = 0;
            int temp_i = i;
            while(input[temp_i] != '\0') {
                if (input[temp_i] == '"') break;
                if (input[temp_i] == '\\' && input[temp_i+1] != '\0') {
                    temp_i++; // Skip backslash
                }
                len++;
                temp_i++;
            }
            
            char *str = malloc(len + 1);
            int k = 0;
            while (input[i] != '"' && input[i] != '\0') {
                if (input[i] == '\\' && input[i+1] != '\0') {
                    // Handle escape chars
                    i++;
                    if (input[i] == 'n') str[k++] = '\n';
                    else if (input[i] == 't') str[k++] = '\t';
                    else if (input[i] == '"') str[k++] = '"';
                    else if (input[i] == '\\') str[k++] = '\\';
                    else str[k++] = input[i]; // Literal otherwise
                } else {
                    str[k++] = input[i];
                }
                i++;
            }
            str[k] = '\0';
            
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
        case TOKEN_KW_MANGWAO: return "MANGWAO";
        case TOKEN_KW_HATAO: return "HATAO";
        case TOKEN_KW_CHECKPOINT: return "CHECKPOINT";
        case TOKEN_KW_INDEX: return "INDEX";
        case TOKEN_KW_ON: return "ON";
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
        case TOKEN_STAR: return "*";
        case TOKEN_SEMICOLON: return ";";
        default: return "UNKNOWN";
    }
}
