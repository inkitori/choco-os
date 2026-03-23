#include "malloc.h"
#include <stdint.h>
#include <stdbool.h>

#define HEAP_SIZE (32 * 1024 * 1024) // 32 mb
static uint8_t heap_memory[HEAP_SIZE] __attribute__((aligned(8)));

typedef struct Block {
    size_t size;
    bool free;
    struct Block* next;
} Block;

static Block* free_list = NULL;

static void heap_init() {
    free_list = (Block*)heap_memory;
    free_list->size = HEAP_SIZE - sizeof(Block);
    free_list->free = true;
    free_list->next = NULL;
}

void* malloc(size_t size) {
    if (size == 0) return NULL;
    
    if (free_list == NULL) {
        heap_init();
    }

    // aligning size to 8 bytes
    size = (size + 7) & ~7;

    Block* curr = free_list;
    while (curr != NULL) {
        if (curr->free && curr->size >= size) {
            // split the block if it's large enough to hold another block
            if (curr->size >= size + sizeof(Block) + 8) {
                Block* new_block = (Block*)((uint8_t*)curr + sizeof(Block) + size);
                new_block->size = curr->size - size - sizeof(Block);
                new_block->free = true;
                new_block->next = curr->next;
                
                curr->size = size;
                curr->next = new_block;
            }
            curr->free = false;
            return (void*)((uint8_t*)curr + sizeof(Block));
        }
        curr = curr->next;
    }
    
    // out of memory
    return NULL;
}

void free(void* ptr) {
    if (ptr == NULL) return;

    Block* block = (Block*)((uint8_t*)ptr - sizeof(Block));
    block->free = true;

    // coalesce adjacent free blocks
    Block* curr = free_list;
    while (curr != NULL && curr->next != NULL) {
        if (curr->free && curr->next->free) {
            curr->size += sizeof(Block) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

void* calloc(size_t num, size_t size) {
    if (num && size > SIZE_MAX / num) return NULL;
    size_t total = num * size;
    void* ptr = malloc(total);
    if (ptr != NULL) {
        uint8_t* p = (uint8_t*)ptr;
        for (size_t i = 0; i < total; i++) {
            p[i] = 0;
        }
    }
    return ptr;
}

void* realloc(void* ptr, size_t size) {
    if (ptr == NULL) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    Block* block = (Block*)((uint8_t*)ptr - sizeof(Block));
    if (block->size >= size) {
        return ptr;
    }

    void* new_ptr = malloc(size);
    if (new_ptr != NULL) {
        // copy old data
        uint8_t* src = (uint8_t*)ptr;
        uint8_t* dst = (uint8_t*)new_ptr;
        for (size_t i = 0; i < block->size; i++) {
            dst[i] = src[i];
        }
        free(ptr);
    }
    return new_ptr;
}

#include "term.h"

void test_malloc() {
    term_print("Starting malloc tests...\n");

    term_print("1. Testing basic malloc & write... ");
    uint8_t *p1 = malloc(128);
    if (!p1) { term_print_error("FAIL\n"); return; }
    for(int i = 0; i < 128; i++) p1[i] = (uint8_t)i;
    
    bool ok = true;
    for(int i = 0; i < 128; i++) {
        if(p1[i] != (uint8_t)i) ok = false;
    }
    if(ok) term_print_success("PASS\n"); else term_print_error("FAIL\n");

    term_print("2. Testing calloc zeroing... ");
    uint8_t *p2 = calloc(10, 10); // 100 bytes
    ok = true;
    if (!p2) { term_print_error("FAIL (NULL)\n"); } else {
        for(int i = 0; i < 100; i++) {
            if(p2[i] != 0) ok = false;
        }
        if(ok) term_print_success("PASS\n"); else term_print_error("FAIL\n");
    }

    term_print("3. Testing realloc... ");
    uint8_t *p3 = realloc(p1, 256);
    ok = true;
    if (!p3) { 
        term_print_error("FAIL (NULL)\n");
        free(p1);
    } else {
        for(int i = 0; i < 128; i++) {
            if(p3[i] != (uint8_t)i) ok = false;
        }
        if(ok) term_print_success("PASS\n"); else term_print_error("FAIL\n");
    }

    term_print("4. Testing free and reuse... ");
    free(p2);
    free(p3); // Freeing the reallocated block

    // Allocate again, it should reuse space if free list is coalescing
    uint8_t *p4 = malloc(50);
    if (p4) term_print_success("PASS\n"); else term_print_error("FAIL\n");

    free(p4);

    term_print("Malloc tests completed!\n");
}