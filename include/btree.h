#ifndef BTREE_H
#define BTREE_H

#include <stdint.h>
#include "buffer_pool.h"

// Node Type Constants
#define NODE_INTERNAL 0
#define NODE_LEAF 1

// Node Header Constants
#define NODE_TYPE_SIZE          sizeof(uint8_t)
#define NODE_TYPE_OFFSET        0
#define IS_ROOT_SIZE            sizeof(uint8_t)
#define IS_ROOT_OFFSET          (NODE_TYPE_SIZE)
#define PARENT_POINTER_SIZE     sizeof(uint32_t)
#define PARENT_POINTER_OFFSET   (IS_ROOT_OFFSET + IS_ROOT_SIZE)
#define COMMON_NODE_HEADER_SIZE (NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE)

// Leaf Node Constants
#define LEAF_NODE_NUM_CELLS_SIZE   sizeof(uint32_t)
#define LEAF_NODE_NUM_CELLS_OFFSET (COMMON_NODE_HEADER_SIZE)
#define LEAF_NODE_HEADER_SIZE      (COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE)

// Table Structure
typedef struct {
    uint32_t root_page_num;
    BufferPool* pager;
} Table;

// Cursor Structure
typedef struct {
    Table* table;
    uint32_t page_num;
    uint32_t cell_num;
    int end_of_table; // boolean
} Cursor;

// Row Structure (Fixed size for B+ Tree Demo)
// We will use a generic large buffer for the Row in the integrated version
#define INTERNAL_ROW_SIZE 512

typedef struct {
    uint8_t data[INTERNAL_ROW_SIZE];
} Row;

#define ROW_SIZE sizeof(Row)

// Leaf Node Body Layout
#define LEAF_NODE_KEY_SIZE        sizeof(uint32_t)
#define LEAF_NODE_VALUE_SIZE      ROW_SIZE
#define LEAF_NODE_CELL_SIZE       (LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE)
#define LEAF_NODE_SPACE_FOR_CELLS (PAGE_SIZE - LEAF_NODE_HEADER_SIZE)
#define LEAF_NODE_MAX_CELLS       (LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE)

// Internal Node Header Layout
#define INTERNAL_NODE_NUM_KEYS_SIZE      sizeof(uint32_t)
#define INTERNAL_NODE_NUM_KEYS_OFFSET    COMMON_NODE_HEADER_SIZE
#define INTERNAL_NODE_RIGHT_CHILD_SIZE   sizeof(uint32_t)
#define INTERNAL_NODE_RIGHT_CHILD_OFFSET (INTERNAL_NODE_NUM_KEYS_OFFSET + INTERNAL_NODE_NUM_KEYS_SIZE)
#define INTERNAL_NODE_HEADER_SIZE        (COMMON_NODE_HEADER_SIZE + INTERNAL_NODE_NUM_KEYS_SIZE + INTERNAL_NODE_RIGHT_CHILD_SIZE)

// Internal Node Body Layout
#define INTERNAL_NODE_KEY_SIZE           sizeof(uint32_t)
#define INTERNAL_NODE_CHILD_SIZE         sizeof(uint32_t)
#define INTERNAL_NODE_CELL_SIZE          (INTERNAL_NODE_CHILD_SIZE + INTERNAL_NODE_KEY_SIZE)
#define INTERNAL_NODE_MAX_CELLS          ((PAGE_SIZE - INTERNAL_NODE_HEADER_SIZE) / INTERNAL_NODE_CELL_SIZE) // Approximate

// Function prototypes
Table* db_open(const char* filename);
void db_close(Table* table);

void leaf_node_init(void* node);
void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value);
Cursor* table_find(Table* table, uint32_t key);
Cursor* table_start(Table* table);
void print_leaf_node(void* node);

// Node Accessors (Exposed for testing)
uint32_t* leaf_node_num_cells(void* node);
void* leaf_node_cell(void* node, uint32_t cell_num);
uint32_t* leaf_node_key(void* node, uint32_t cell_num);
void* leaf_node_value(void* node, uint32_t cell_num);

#endif
