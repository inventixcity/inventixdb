#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include "pager.h"

Pager* pager_open(const char* filename) {
    FILE *fd = fopen(filename, "r+b"); // Try open existing
    if (!fd) {
        fd = fopen(filename, "w+b"); // Create new
        if (!fd) {
            printf("Could not open file: %s\n", filename);
            return NULL;
        }
    }

    Pager* pager = malloc(sizeof(Pager));
    pager->file_descriptor = fd;
    pager->filename = strdup(filename);

    fseek(fd, 0, SEEK_END);
    pager->file_length = ftell(fd);
    pager->num_pages = pager->file_length / PAGE_SIZE;

    if (pager->file_length % PAGE_SIZE != 0) {
        printf("Db file is not a whole number of pages. Corrupt file.\n");
        // Handling corruption or partial writes is complex, simplified here.
    }

    return pager;
}

void pager_read(Pager* pager, uint32_t page_num, void* out_data) {
    if (page_num >= TABLE_MAX_PAGES) {
         printf("Tried to fetch page number out of bounds. %d > %d\n", page_num, TABLE_MAX_PAGES);
         return; 
         // In real DB, we would grow file. Here we treat read OOB as error or empty?
         // Proper flow checks num_pages first.
    }
    
    // Check key
    fseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
    size_t bytes_read = fread(out_data, 1, PAGE_SIZE, pager->file_descriptor);
    
    if (bytes_read < PAGE_SIZE) {
        // If could not read full page, it might be a new page allocation scenario
        // Zero out the rest
        memset((char*)out_data + bytes_read, 0, PAGE_SIZE - bytes_read);
    }
}

void pager_write(Pager* pager, uint32_t page_num, const void* data) {
    fseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
    size_t bytes_written = fwrite(data, 1, PAGE_SIZE, pager->file_descriptor);
    if (bytes_written != PAGE_SIZE) {
        printf("Could not write full page to disk.\n");
    }
    fflush(pager->file_descriptor);
}

void pager_close(Pager* pager) {
    if (pager->file_descriptor) {
         fclose(pager->file_descriptor);
    }
    if (pager->filename) {
        free(pager->filename);
    }
    free(pager);
}

uint32_t pager_get_num_pages(Pager* pager) {
    // Refresh length
    fseek(pager->file_descriptor, 0, SEEK_END);
    pager->file_length = ftell(pager->file_descriptor);
    pager->num_pages = pager->file_length / PAGE_SIZE;
    return pager->num_pages;
}
