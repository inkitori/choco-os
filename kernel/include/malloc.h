#ifndef MALLOC_H
#define MALLOC_H

#include <stddef.h>

void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t num, size_t size);
void* realloc(void* ptr, size_t size);
void test_malloc();

#endif