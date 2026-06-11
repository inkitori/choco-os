#ifndef MALLOC_H
#define MALLOC_H

#include <stddef.h>
#include <stdint.h>

void heap_init(void);
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t num, size_t size);
void *realloc(void *ptr, size_t size);

size_t heap_total(void);
size_t heap_used(void);

void test_malloc(void);

#endif
