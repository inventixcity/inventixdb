#ifndef INVENTIX_ROW_H
#define INVENTIX_ROW_H

#include <stddef.h>
#include "parser.h" // For NodeList

// Serialize a list of values into a binary buffer
// format: [Total Cols (4B)] [ [Len(4B)][Data...] ... ]
void* row_serialize(NodeList *values, size_t *out_size);

// Deserialize a binary buffer into an array of strings
char** row_deserialize(void *data, size_t size, int *out_count);

// Free the array of strings returned by deserialization
void row_free_result(char **columns, int count);

#endif
