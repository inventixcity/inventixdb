#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "row.h"

// Format:
// [4 bytes: Count]
// For each column:
//   [4 bytes: Length]
//   [Length bytes: Data]

void* row_serialize(NodeList *values, size_t *out_size) {
    // 1. Calculate size
    int count = 0;
    size_t total_size = sizeof(int); // For count
    NodeList *curr = values;
    
    while(curr) {
        count++;
        total_size += sizeof(int); // For length
        total_size += strlen(curr->value); // For data
        curr = curr->next;
    }

    // 2. Allocate
    unsigned char *buffer = malloc(total_size);
    if (!buffer) return NULL;

    // 3. Write Data
    unsigned char *ptr = buffer;
    
    // Write Count
    memcpy(ptr, &count, sizeof(int));
    ptr += sizeof(int);
    
    curr = values;
    while(curr) {
        int len = strlen(curr->value);
        memcpy(ptr, &len, sizeof(int));
        ptr += sizeof(int);
        
        memcpy(ptr, curr->value, len);
        ptr += len;
        
        curr = curr->next;
    }
    
    *out_size = total_size;
    return (void*)buffer;
}

char** row_deserialize(void *data, size_t size, int *out_count) {
    if (!data || size < sizeof(int)) return NULL;
    
    unsigned char *ptr = (unsigned char*)data;
    
    // Read count
    int count;
    memcpy(&count, ptr, sizeof(int));
    ptr += sizeof(int);
    
    if (count < 0 || count > 1000) { // Sanity check
        return NULL; 
    }
    
    char **result = malloc(sizeof(char*) * count);
    
    for(int i=0; i<count; i++) {
        int len;
        memcpy(&len, ptr, sizeof(int));
        ptr += sizeof(int);
        
        result[i] = malloc(len + 1);
        memcpy(result[i], ptr, len);
        result[i][len] = '\0';
        
        ptr += len;
    }
    
    *out_count = count;
    return result;
}

void row_free_result(char **columns, int count) {
    if (!columns) return;
    for(int i=0; i<count; i++) {
        if (columns[i]) free(columns[i]);
    }
    free(columns);
}
