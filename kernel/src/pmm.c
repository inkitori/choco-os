#include "pmm.h"
#include "limine.h"
#include "term.h"
#include <stdbool.h>

extern volatile struct limine_memmap_request memmap_request;

extern volatile struct limine_hhdm_request hhdm_request;

static uint8_t* pmm_bitmap = NULL;
static size_t bitmap_size = 0;
static size_t highest_page = 0;
static uint64_t hhdm_offset = 0;

static inline void bitmap_set(size_t bit) {
    pmm_bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void bitmap_clear(size_t bit) {
    pmm_bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static inline bool bitmap_test(size_t bit) {
    return (pmm_bitmap[bit / 8] & (1 << (bit % 8))) != 0;
}

void pmm_init(void) {
    if (memmap_request.response == NULL) {
        term_print_error("PMM: No memmap response from bootloader.\n");
        return;
    }
    if (hhdm_request.response == NULL) {
        term_print_error("PMM: No HHDM response from bootloader.\n");
        return;
    }

    hhdm_offset = hhdm_request.response->offset;

    struct limine_memmap_response* mmap = memmap_request.response;
    uint64_t highest_addr = 0;

    for (size_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry* entry = mmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE ||
            entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
            entry->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE ||
            entry->type == LIMINE_MEMMAP_KERNEL_AND_MODULES) {
            uint64_t top = entry->base + entry->length;
            if (top > highest_addr) {
                highest_addr = top;
            }
        }
    }

    highest_page = highest_addr / PAGE_SIZE;
    bitmap_size = highest_page / 8;
    if (bitmap_size * 8 < highest_page) {
        bitmap_size++;
    }

    // Find a place for the bitmap
    for (size_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry* entry = mmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_size) {
            pmm_bitmap = (uint8_t*)(entry->base + hhdm_offset);
            break;
        }
    }

    if (pmm_bitmap == NULL) {
        term_print_error("PMM: Could not find suitable region for bitmap.\n");
        return;
    }

    // Initialize all pages as used (1)
    for (size_t i = 0; i < bitmap_size; i++) {
        pmm_bitmap[i] = 0xFF;
    }

    // Free the usable regions
    for (size_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry* entry = mmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            for (uint64_t addr = entry->base; addr < entry->base + entry->length; addr += PAGE_SIZE) {
                bitmap_clear(addr / PAGE_SIZE);
            }
        }
    }

    // Mark the bitmap itself as used
    uint64_t bitmap_phys = (uint64_t)pmm_bitmap - hhdm_offset;
    for (uint64_t addr = bitmap_phys; addr < bitmap_phys + bitmap_size; addr += PAGE_SIZE) {
        bitmap_set(addr / PAGE_SIZE);
    }

    // Mark page 0 as used just in case
    bitmap_set(0);

    term_print_success("PMM initialized.\n");
}

void* pmm_alloc_page(void) {
    return pmm_alloc_pages(1);
}

void* pmm_alloc_pages(size_t count) {
    if (count == 0) return NULL;

    size_t run_length = 0;
    size_t run_start = 0;

    for (size_t i = 1; i < highest_page; i++) {
        if (!bitmap_test(i)) {
            if (run_length == 0) run_start = i;
            run_length++;
            if (run_length == count) {
                for (size_t j = 0; j < count; j++) {
                    bitmap_set(run_start + j);
                }
                
                // Zero out the page
                void* vaddr = (void*)((run_start * PAGE_SIZE) + hhdm_offset);
                uint64_t* ptr = (uint64_t*)vaddr;
                for (size_t k = 0; k < (PAGE_SIZE * count) / 8; k++) {
                    ptr[k] = 0;
                }

                return (void*)(run_start * PAGE_SIZE);
            }
        } else {
            run_length = 0;
        }
    }
    return NULL;
}

void pmm_free_page(void* page) {
    pmm_free_pages(page, 1);
}

void pmm_free_pages(void* page, size_t count) {
    if ((uint64_t)page % PAGE_SIZE != 0) return;
    size_t start_page = (uint64_t)page / PAGE_SIZE;

    for (size_t i = 0; i < count; i++) {
        bitmap_clear(start_page + i);
    }
}
