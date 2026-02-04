#ifndef INVENTIX_COMMON_H
#define INVENTIX_COMMON_H

// Common definitions and types for InventixDB

typedef enum {
    CMD_UNKNOWN,
    CMD_CREATE_TABLE,   // TABLE banao
    CMD_INSERT,         // INSERT karo
    CMD_SELECT,         // SELECT karo
    CMD_DOC_INSERT,     // RAKHO
    CMD_DOC_FIND,       // DHUNDO
    CMD_DELETE          // NIKALO
} CommandType;

typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING
} DataType;

#endif // INVENTIX_COMMON_H
