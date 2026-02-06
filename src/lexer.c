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
    if (strcasecmp(str, "DALO") == 0) return TOKEN_KW_INSERT;  // Hinglish (single A)
    if (strcasecmp(str, "KARO") == 0) return TOKEN_KW_KARO;
    if (strcasecmp(str, "SELECT") == 0) return TOKEN_KW_SELECT;
    if (strcasecmp(str, "CHUNO") == 0) return TOKEN_KW_SELECT;  // Hinglish alternate
    if (strcasecmp(str, "DIKHAO") == 0) return TOKEN_KW_SELECT; // Hinglish (show me)
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
    if (strcasecmp(str, "DEKHO") == 0) return TOKEN_KW_show;    // Hinglish SHOW
    if (strcasecmp(str, "TABLES") == 0) return TOKEN_KW_TABLES;
    if (strcasecmp(str, "TABLED") == 0) return TOKEN_KW_TABLES;  // Hinglish TABLES
    if (strcasecmp(str, "USE") == 0) return TOKEN_KW_USE;
    if (strcasecmp(str, "ISTEMAAL") == 0) return TOKEN_KW_USE; // Hinglish Alias
    if (strcasecmp(str, "DELETE") == 0) return TOKEN_KW_DELETE; 
    if (strcasecmp(str, "DROP") == 0) return TOKEN_KW_DROP;
    if (strcasecmp(str, "WHERE") == 0) return TOKEN_KW_JAHAN;
    if (strcasecmp(str, "UPDATE") == 0) return TOKEN_KW_UPDATE;
    if (strcasecmp(str, "BADLO") == 0) return TOKEN_KW_UPDATE;   // Hinglish UPDATE
    if (strcasecmp(str, "SET") == 0) return TOKEN_KW_SET;
    if (strcasecmp(str, "RAKHO_YEH") == 0) return TOKEN_KW_SET;  // Hinglish SET
    
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
    
    // Transaction Keywords
    if (strcasecmp(str, "BEGIN") == 0) return TOKEN_KW_BEGIN;
    if (strcasecmp(str, "SHURU") == 0) return TOKEN_KW_BEGIN;     // Hinglish
    if (strcasecmp(str, "COMMIT") == 0) return TOKEN_KW_COMMIT;
    if (strcasecmp(str, "PUKKA") == 0) return TOKEN_KW_COMMIT;    // Hinglish
    if (strcasecmp(str, "ROLLBACK") == 0) return TOKEN_KW_ROLLBACK;
    if (strcasecmp(str, "WAPAS") == 0) return TOKEN_KW_ROLLBACK;  // Hinglish
    if (strcasecmp(str, "SAVEPOINT") == 0) return TOKEN_KW_SAVEPOINT;
    if (strcasecmp(str, "NISHAAN") == 0) return TOKEN_KW_SAVEPOINT; // Hinglish
    if (strcasecmp(str, "RELEASE") == 0) return TOKEN_KW_RELEASE;
    if (strcasecmp(str, "TRANSACTION") == 0) return TOKEN_KW_TRANSACTION;
    if (strcasecmp(str, "START") == 0) return TOKEN_KW_START;
    if (strcasecmp(str, "TO") == 0) return TOKEN_KW_TO;
    if (strcasecmp(str, "INTO") == 0) return TOKEN_KW_INTO;      // INTO for INSERT INTO
    if (strcasecmp(str, "MEIN") == 0) return TOKEN_KW_INTO;      // Hinglish INTO
    
    // Isolation Level Keywords
    if (strcasecmp(str, "ISOLATION") == 0) return TOKEN_KW_ISOLATION;
    if (strcasecmp(str, "LEVEL") == 0) return TOKEN_KW_LEVEL;
    if (strcasecmp(str, "READ") == 0) return TOKEN_KW_READ;
    if (strcasecmp(str, "PADHO") == 0) return TOKEN_KW_READ;     // Hinglish READ
    if (strcasecmp(str, "WRITE") == 0) return TOKEN_KW_WRITE;
    if (strcasecmp(str, "LIKHO") == 0) return TOKEN_KW_WRITE;    // Hinglish WRITE
    if (strcasecmp(str, "UNCOMMITTED") == 0) return TOKEN_KW_UNCOMMITTED;
    if (strcasecmp(str, "COMMITTED") == 0) return TOKEN_KW_COMMITTED;
    if (strcasecmp(str, "REPEATABLE") == 0) return TOKEN_KW_REPEATABLE;
    if (strcasecmp(str, "SERIALIZABLE") == 0) return TOKEN_KW_SERIALIZABLE;
    
    // Prepared Statement Keywords
    if (strcasecmp(str, "PREPARE") == 0) return TOKEN_KW_PREPARE;
    if (strcasecmp(str, "TAYYAR") == 0) return TOKEN_KW_PREPARE;  // Hinglish
    if (strcasecmp(str, "EXECUTE") == 0) return TOKEN_KW_EXECUTE;
    if (strcasecmp(str, "CHALAO") == 0) return TOKEN_KW_EXECUTE;  // Hinglish
    if (strcasecmp(str, "DEALLOCATE") == 0) return TOKEN_KW_DEALLOCATE;
    if (strcasecmp(str, "HATAO_TAYYAR") == 0) return TOKEN_KW_DEALLOCATE; // Hinglish
    if (strcasecmp(str, "AS") == 0) return TOKEN_KW_AS;
    if (strcasecmp(str, "JAISE") == 0) return TOKEN_KW_AS;       // Hinglish
    if (strcasecmp(str, "USING") == 0) return TOKEN_KW_USING;
    if (strcasecmp(str, "ISTEMAL") == 0) return TOKEN_KW_USING;  // Hinglish
    
    // JOIN Keywords
    if (strcasecmp(str, "JOIN") == 0) return TOKEN_KW_JOIN;
    if (strcasecmp(str, "MILAO") == 0) return TOKEN_KW_JOIN;     // Hinglish JOIN
    if (strcasecmp(str, "INNER") == 0) return TOKEN_KW_INNER;
    if (strcasecmp(str, "LEFT") == 0) return TOKEN_KW_LEFT;
    if (strcasecmp(str, "BAAYA") == 0) return TOKEN_KW_LEFT;     // Hinglish LEFT
    if (strcasecmp(str, "RIGHT") == 0) return TOKEN_KW_RIGHT;
    if (strcasecmp(str, "DAAYA") == 0) return TOKEN_KW_RIGHT;    // Hinglish RIGHT
    if (strcasecmp(str, "FULL") == 0) return TOKEN_KW_FULL;
    if (strcasecmp(str, "POORA") == 0) return TOKEN_KW_FULL;     // Hinglish FULL
    if (strcasecmp(str, "OUTER") == 0) return TOKEN_KW_OUTER;
    if (strcasecmp(str, "BAHAR") == 0) return TOKEN_KW_OUTER;    // Hinglish OUTER
    if (strcasecmp(str, "CROSS") == 0) return TOKEN_KW_CROSS;
    if (strcasecmp(str, "NATURAL") == 0) return TOKEN_KW_NATURAL;
    if (strcasecmp(str, "KUDRATI") == 0) return TOKEN_KW_NATURAL; // Hinglish NATURAL
    
    // EXPLAIN / ORDER BY / LIMIT
    if (strcasecmp(str, "EXPLAIN") == 0) return TOKEN_KW_EXPLAIN;
    if (strcasecmp(str, "SAMJHAO") == 0) return TOKEN_KW_EXPLAIN; // Hinglish EXPLAIN
    if (strcasecmp(str, "ANALYZE") == 0) return TOKEN_KW_ANALYZE;
    if (strcasecmp(str, "ORDER") == 0) return TOKEN_KW_ORDER;
    if (strcasecmp(str, "KRAM") == 0) return TOKEN_KW_ORDER;     // Hinglish ORDER
    if (strcasecmp(str, "ASC") == 0) return TOKEN_KW_ASC;
    if (strcasecmp(str, "CHADHTE") == 0) return TOKEN_KW_ASC;    // Hinglish ASC
    if (strcasecmp(str, "DESC") == 0) return TOKEN_KW_DESC;
    if (strcasecmp(str, "UTARTE") == 0) return TOKEN_KW_DESC;    // Hinglish DESC
    if (strcasecmp(str, "LIMIT") == 0) return TOKEN_KW_LIMIT;
    if (strcasecmp(str, "SEEMA") == 0) return TOKEN_KW_LIMIT;    // Hinglish LIMIT
    if (strcasecmp(str, "OFFSET") == 0) return TOKEN_KW_OFFSET;
    if (strcasecmp(str, "PAAR") == 0) return TOKEN_KW_OFFSET;     // Hinglish OFFSET
    if (strcasecmp(str, "WHERE") == 0) return TOKEN_KW_WHERE;
    
    // ALTER TABLE Keywords (Iteration 2)
    if (strcasecmp(str, "ALTER") == 0) return TOKEN_KW_ALTER;
    if (strcasecmp(str, "BADLO_TABLE") == 0) return TOKEN_KW_ALTER;  // Hinglish ALTER
    if (strcasecmp(str, "ADD") == 0) return TOKEN_KW_ADD;
    if (strcasecmp(str, "JODO_COLUMN") == 0) return TOKEN_KW_ADD;   // Hinglish ADD
    if (strcasecmp(str, "COLUMN") == 0) return TOKEN_KW_COLUMN;
    if (strcasecmp(str, "STAMBH") == 0) return TOKEN_KW_COLUMN;     // Hinglish COLUMN
    if (strcasecmp(str, "RENAME") == 0) return TOKEN_KW_RENAME;
    if (strcasecmp(str, "NAAM_BADLO") == 0) return TOKEN_KW_RENAME; // Hinglish RENAME
    if (strcasecmp(str, "MODIFY") == 0) return TOKEN_KW_MODIFY;
    if (strcasecmp(str, "SUDHAR") == 0) return TOKEN_KW_MODIFY;     // Hinglish MODIFY
    if (strcasecmp(str, "CONSTRAINT") == 0) return TOKEN_KW_CONSTRAINT;
    if (strcasecmp(str, "NIYAM") == 0) return TOKEN_KW_CONSTRAINT;  // Hinglish CONSTRAINT
    if (strcasecmp(str, "FOREIGN") == 0) return TOKEN_KW_FOREIGN;
    if (strcasecmp(str, "VIDESHI") == 0) return TOKEN_KW_FOREIGN;   // Hinglish FOREIGN
    if (strcasecmp(str, "REFERENCES") == 0) return TOKEN_KW_REFERENCES;
    if (strcasecmp(str, "SANDARBH") == 0) return TOKEN_KW_REFERENCES; // Hinglish REFERENCES
    if (strcasecmp(str, "CASCADE") == 0) return TOKEN_KW_CASCADE;
    if (strcasecmp(str, "JHARNA") == 0) return TOKEN_KW_CASCADE;    // Hinglish CASCADE
    if (strcasecmp(str, "RESTRICT") == 0) return TOKEN_KW_RESTRICT;
    if (strcasecmp(str, "ROKO") == 0) return TOKEN_KW_RESTRICT;     // Hinglish RESTRICT
    if (strcasecmp(str, "NULL") == 0) return TOKEN_KW_NULL;
    if (strcasecmp(str, "KHALI") == 0) return TOKEN_KW_NULL;        // Hinglish NULL
    if (strcasecmp(str, "NOT") == 0) return TOKEN_KW_NOT;
    if (strcasecmp(str, "NAHI") == 0) return TOKEN_KW_NOT;          // Hinglish NOT
    if (strcasecmp(str, "DEFAULT") == 0) return TOKEN_KW_DEFAULT;
    if (strcasecmp(str, "MOOL") == 0) return TOKEN_KW_DEFAULT;      // Hinglish DEFAULT
    if (strcasecmp(str, "UNIQUE") == 0) return TOKEN_KW_UNIQUE;
    if (strcasecmp(str, "ANOKHA") == 0) return TOKEN_KW_UNIQUE;     // Hinglish UNIQUE
    
    // Backup/Restore Keywords
    if (strcasecmp(str, "BACKUP") == 0) return TOKEN_KW_BACKUP;
    if (strcasecmp(str, "SURAKSHA") == 0) return TOKEN_KW_BACKUP;   // Hinglish BACKUP
    if (strcasecmp(str, "RESTORE") == 0) return TOKEN_KW_RESTORE;
    if (strcasecmp(str, "WAPAS_LAO") == 0) return TOKEN_KW_RESTORE; // Hinglish RESTORE
    if (strcasecmp(str, "EXPORT") == 0) return TOKEN_KW_EXPORT;
    if (strcasecmp(str, "BHEJO") == 0) return TOKEN_KW_EXPORT;      // Hinglish EXPORT
    if (strcasecmp(str, "IMPORT") == 0) return TOKEN_KW_IMPORT;
    if (strcasecmp(str, "LAAO") == 0) return TOKEN_KW_IMPORT;       // Hinglish IMPORT
    
    // NoSQL Enhanced Keywords
    if (strcasecmp(str, "COLLECTION") == 0) return TOKEN_KW_COLLECTION;
    if (strcasecmp(str, "SANGRAH") == 0) return TOKEN_KW_COLLECTION;// Hinglish COLLECTION
    if (strcasecmp(str, "DOCUMENT") == 0) return TOKEN_KW_DOCUMENT;
    if (strcasecmp(str, "DASTAVEZ") == 0) return TOKEN_KW_DOCUMENT; // Hinglish DOCUMENT
    if (strcasecmp(str, "FIND") == 0) return TOKEN_KW_FIND;
    if (strcasecmp(str, "KHOJO") == 0) return TOKEN_KW_FIND;        // Hinglish FIND
    if (strcasecmp(str, "UPSERT") == 0) return TOKEN_KW_UPSERT;
    if (strcasecmp(str, "DAL_YA_BADLO") == 0) return TOKEN_KW_UPSERT;// Hinglish UPSERT
    if (strcasecmp(str, "AGGREGATE") == 0) return TOKEN_KW_AGGREGATE;
    if (strcasecmp(str, "IKATHA") == 0) return TOKEN_KW_AGGREGATE;  // Hinglish AGGREGATE
    if (strcasecmp(str, "MATCH") == 0) return TOKEN_KW_MATCH;
    if (strcasecmp(str, "MILA") == 0) return TOKEN_KW_MATCH;        // Hinglish MATCH
    if (strcasecmp(str, "PROJECT") == 0) return TOKEN_KW_PROJECT;
    if (strcasecmp(str, "PRADARSHI") == 0) return TOKEN_KW_PROJECT; // Hinglish PROJECT
    if (strcasecmp(str, "UNWIND") == 0) return TOKEN_KW_UNWIND;
    if (strcasecmp(str, "KHOLO") == 0) return TOKEN_KW_UNWIND;      // Hinglish UNWIND
    if (strcasecmp(str, "LOOKUP") == 0) return TOKEN_KW_LOOKUP;
    if (strcasecmp(str, "DEKHO_MILA") == 0) return TOKEN_KW_LOOKUP; // Hinglish LOOKUP
    if (strcasecmp(str, "SORT") == 0) return TOKEN_KW_SORT;
    if (strcasecmp(str, "KRAM_SE") == 0) return TOKEN_KW_SORT;      // Hinglish SORT
    if (strcasecmp(str, "FORMAT") == 0) return TOKEN_KW_FORMAT;
    if (strcasecmp(str, "ROOP") == 0) return TOKEN_KW_FORMAT;       // Hinglish FORMAT
    if (strcasecmp(str, "DROP") == 0) return TOKEN_KW_DROP;
    
    // Additional Hinglish/Urdlish Keywords for Complete Coverage
    // HAVING clause
    if (strcasecmp(str, "HAVING") == 0) return TOKEN_KW_HAVING;
    if (strcasecmp(str, "RAKHTE") == 0) return TOKEN_KW_HAVING;     // Hinglish HAVING
    if (strcasecmp(str, "JISME") == 0) return TOKEN_KW_HAVING;      // Urdlish HAVING
    
    // DISTINCT
    if (strcasecmp(str, "DISTINCT") == 0) return TOKEN_KW_DISTINCT;
    if (strcasecmp(str, "ALAG") == 0) return TOKEN_KW_DISTINCT;     // Hinglish DISTINCT
    if (strcasecmp(str, "MUKHTALIF") == 0) return TOKEN_KW_DISTINCT;// Urdlish DISTINCT
    
    // LIKE for pattern matching
    if (strcasecmp(str, "LIKE") == 0) return TOKEN_KW_LIKE;
    if (strcasecmp(str, "JAISA") == 0) return TOKEN_KW_LIKE;        // Hinglish LIKE
    
    // IN clause
    if (strcasecmp(str, "IN") == 0) return TOKEN_KW_IN;
    if (strcasecmp(str, "ANDAR") == 0) return TOKEN_KW_IN;          // Hinglish IN
    
    // BETWEEN
    if (strcasecmp(str, "BETWEEN") == 0) return TOKEN_KW_BETWEEN;
    if (strcasecmp(str, "BEECH") == 0) return TOKEN_KW_BETWEEN;     // Hinglish BETWEEN
    
    // EXISTS
    if (strcasecmp(str, "EXISTS") == 0) return TOKEN_KW_EXISTS;
    if (strcasecmp(str, "MAUJOOD") == 0) return TOKEN_KW_EXISTS;    // Hinglish EXISTS
    
    // TRUE/FALSE
    if (strcasecmp(str, "TRUE") == 0) return TOKEN_KW_TRUE;
    if (strcasecmp(str, "SACH") == 0) return TOKEN_KW_TRUE;         // Hinglish TRUE
    if (strcasecmp(str, "HAAN") == 0) return TOKEN_KW_TRUE;         // Urdlish TRUE
    if (strcasecmp(str, "FALSE") == 0) return TOKEN_KW_FALSE;
    if (strcasecmp(str, "JHOOTH") == 0) return TOKEN_KW_FALSE;      // Hinglish FALSE
    if (strcasecmp(str, "NA") == 0) return TOKEN_KW_FALSE;          // Urdlish FALSE
    
    // CASE/WHEN/THEN/ELSE/END
    if (strcasecmp(str, "CASE") == 0) return TOKEN_KW_CASE;
    if (strcasecmp(str, "MAAMLA") == 0) return TOKEN_KW_CASE;       // Hinglish CASE
    if (strcasecmp(str, "WHEN") == 0) return TOKEN_KW_WHEN;
    if (strcasecmp(str, "JAB") == 0) return TOKEN_KW_WHEN;          // Hinglish WHEN
    if (strcasecmp(str, "THEN") == 0) return TOKEN_KW_THEN;
    if (strcasecmp(str, "PHIR") == 0) return TOKEN_KW_THEN;         // Hinglish THEN
    if (strcasecmp(str, "TAB") == 0) return TOKEN_KW_THEN;          // Urdlish THEN
    if (strcasecmp(str, "ELSE") == 0) return TOKEN_KW_ELSE;
    if (strcasecmp(str, "WARNA") == 0) return TOKEN_KW_ELSE;        // Hinglish ELSE
    if (strcasecmp(str, "END") == 0) return TOKEN_KW_END;
    if (strcasecmp(str, "KHATAM") == 0) return TOKEN_KW_END;        // Hinglish END
    
    // Cluster/Distributed Keywords
    if (strcasecmp(str, "CLUSTER") == 0) return TOKEN_KW_CLUSTER;
    if (strcasecmp(str, "JHAAD") == 0) return TOKEN_KW_CLUSTER;     // Hinglish CLUSTER
    if (strcasecmp(str, "NODES") == 0) return TOKEN_KW_NODES;
    if (strcasecmp(str, "GRANTHI") == 0) return TOKEN_KW_NODES;     // Hinglish NODES
    if (strcasecmp(str, "REPLICATE") == 0) return TOKEN_KW_REPLICATE;
    if (strcasecmp(str, "NAKAL") == 0) return TOKEN_KW_REPLICATE;   // Hinglish REPLICATE
    if (strcasecmp(str, "PARTITION") == 0) return TOKEN_KW_PARTITION;
    if (strcasecmp(str, "VIBHAJAN") == 0) return TOKEN_KW_PARTITION;// Hinglish PARTITION
    if (strcasecmp(str, "SYNC") == 0) return TOKEN_KW_SYNC;
    if (strcasecmp(str, "MILAO_SAATH") == 0) return TOKEN_KW_SYNC;  // Hinglish SYNC
    
    // Help/Exit Commands
    if (strcasecmp(str, "HELP") == 0) return TOKEN_KW_HELP;
    if (strcasecmp(str, "MADAD") == 0) return TOKEN_KW_HELP;        // Hinglish HELP
    if (strcasecmp(str, "QUIT") == 0) return TOKEN_KW_QUIT;
    if (strcasecmp(str, "NIKLO") == 0) return TOKEN_KW_QUIT;        // Hinglish QUIT
    if (strcasecmp(str, "BAHAR") == 0) return TOKEN_KW_QUIT;        // Urdlish QUIT
    if (strcasecmp(str, "EXIT") == 0) return TOKEN_KW_EXIT;
    if (strcasecmp(str, "KHATAM_KARO") == 0) return TOKEN_KW_EXIT;  // Hinglish EXIT
    
    // STATUS Commands
    if (strcasecmp(str, "STATUS") == 0) return TOKEN_KW_STATUS;
    if (strcasecmp(str, "HALAT") == 0) return TOKEN_KW_STATUS;      // Hinglish STATUS
    if (strcasecmp(str, "STHITI") == 0) return TOKEN_KW_STATUS;     // Hindi STATUS
    
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
        if (input[i] == '[') { add_token(list, TOKEN_LBRACKET, "[", line); i++; continue; }
        if (input[i] == ']') { add_token(list, TOKEN_RBRACKET, "]", line); i++; continue; }
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
        case TOKEN_KW_UPDATE: return "UPDATE";
        case TOKEN_KW_SET: return "SET";
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
