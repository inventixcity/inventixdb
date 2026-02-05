#ifndef INVENTIX_PAGER_H
#define INVENTIX_PAGER_H

#include <stdint.h>
#include <stdio.h>

#define PAGE_SIZE 4096
#define TABLE_MAX_PAGES 100

// Page Structure
typedef struct {
    void *data; // 4KB raw data
} Page;

// Pager manages the file IO
typedef struct {
    FILE *file_descriptor;
    uint32_t file_length;
    uint32_t num_pages;
    char *filename;
} Pager;

Pager* pager_open(const char* filename);
void pager_read(Pager* pager, uint32_t page_num, void* out_data);
void pager_write(Pager* pager, uint32_t page_num, const void* data);
void pager_close(Pager* pager);
uint32_t pager_get_num_pages(Pager* pager);

#endif
