#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "btree.h"

// Accessors for Leaf Node Headers
uint32_t* leaf_node_num_cells(void* node) {
    return (uint32_t *)((char*)node + LEAF_NODE_NUM_CELLS_OFFSET);
}

void* leaf_node_cell(void* node, uint32_t cell_num) {
    return (char*)node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

uint32_t* leaf_node_key(void* node, uint32_t cell_num) {
    return (uint32_t*)leaf_node_cell(node, cell_num);
}

void* leaf_node_value(void* node, uint32_t cell_num) {
    return (char*)leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
}

void leaf_node_init(void* node) {
    uint8_t *node_type = (uint8_t*)((char*)node + NODE_TYPE_OFFSET);
    *node_type = NODE_LEAF;
    
    uint8_t *is_root = (uint8_t*)((char*)node + IS_ROOT_OFFSET);
    *is_root = 0;
    
    uint32_t *num_cells = leaf_node_num_cells(node);
    *num_cells = 0;
}

// DB Management
Table* db_open(const char* filename) {
    Table* table = malloc(sizeof(Table));
    table->pager = buffer_pool_init(filename);
    
    uint32_t num_pages = table->pager->pager->file_length / PAGE_SIZE;
    if (num_pages == 0) {
        // New database file. Initialize page 0 as leaf root
        void* root_node = buffer_pool_get_page(table->pager, 0);
        leaf_node_init(root_node);
        uint8_t *is_root = (uint8_t*)((char*)root_node + IS_ROOT_OFFSET);
        *is_root = 1;
        buffer_pool_mark_dirty(table->pager, 0);
        buffer_pool_unpin(table->pager, 0); // Done initializing
        table->root_page_num = 0;
    } else {
        table->root_page_num = 0;
    }
    
    return table;
}

void db_close(Table* table) {
    buffer_pool_close(table->pager);
    free(table);
}

// Cursor / Find
Cursor* table_start(Table* table) {
    Cursor* cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = table->root_page_num;
    cursor->cell_num = 0;
    
    void* root_node = buffer_pool_get_page(table->pager, table->root_page_num);
    uint32_t num_cells = *leaf_node_num_cells(root_node);
    cursor->end_of_table = (num_cells == 0);
    
    buffer_pool_unpin(table->pager, table->root_page_num);
    return cursor;
}

Cursor* table_find(Table* table, uint32_t key) {
    uint32_t root_page_num = table->root_page_num;
    void* root_node = buffer_pool_get_page(table->pager, root_page_num);
    
    // Binary search would be better, but linear scan for now
    uint32_t num_cells = *leaf_node_num_cells(root_node);
    
    Cursor* cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = root_page_num;
    
    uint32_t min_index = 0;
    uint32_t one_past_max_index = num_cells;
    
    while (one_past_max_index != min_index) {
        uint32_t index = (min_index + one_past_max_index) / 2;
        uint32_t key_at_index = *leaf_node_key(root_node, index);
        if (key == key_at_index) {
            cursor->cell_num = index;
            buffer_pool_unpin(table->pager, root_page_num);
            return cursor;
        }
        if (key < key_at_index) {
            one_past_max_index = index;
        } else {
            min_index = index + 1;
        }
    }
    
    cursor->cell_num = min_index;
    buffer_pool_unpin(table->pager, root_page_num);
    return cursor;
}

void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* node = buffer_pool_get_page(cursor->table->pager, cursor->page_num);
    
    uint32_t num_cells = *leaf_node_num_cells(node);
    if (num_cells >= LEAF_NODE_MAX_CELLS) {
        // Node full: Split (Not implemented in this step, simple error)
        printf("Error: Leaf node full (Splitting not implemented)\n");
        buffer_pool_unpin(cursor->table->pager, cursor->page_num);
        return;
    }
    
    if (cursor->cell_num < num_cells) {
        // Make room for new cell
        for (uint32_t i = num_cells; i > cursor->cell_num; i--) {
            memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1), LEAF_NODE_CELL_SIZE);
        }
    }
    
    *leaf_node_num_cells(node) += 1;
    *leaf_node_key(node, cursor->cell_num) = key;
    memcpy(leaf_node_value(node, cursor->cell_num), value, ROW_SIZE);
    
    buffer_pool_mark_dirty(cursor->table->pager, cursor->page_num);
    buffer_pool_unpin(cursor->table->pager, cursor->page_num);
}

void print_leaf_node(void* node) {
    uint32_t num_cells = *leaf_node_num_cells(node);
    printf("Leaf (size %d)\n", num_cells);
    for (uint32_t i = 0; i < num_cells; i++) {
        uint32_t key = *leaf_node_key(node, i);
        printf("  - %d : [Row Data]\n", key);
    }
}
