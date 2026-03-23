#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PAGE_SIZE 4096

#define PTE_PRESENT (1ull << 0)
#define PTE_WRITABLE (1ull << 1)
#define PTE_USER (1ull << 2)
#define PTE_WRITE_THROUGH (1ull << 3)
#define PTE_CACHE_DISABLE (1ull << 4)
#define PTE_ACCESSED (1ull << 5)
#define PTE_DIRTY (1ull << 6)
#define PTE_HUGE (1ull << 7)
#define PTE_GLOBAL (1ull << 8)
#define PTE_NO_EXECUTE (1ull << 63)

#define PTE_ADDR_MASK 0x000ffffffffff000

void vmm_init(void);
void vmm_map_page(uint64_t* pml4, uint64_t vaddr, uint64_t paddr, uint64_t flags);
void vmm_unmap_page(uint64_t* pml4, uint64_t vaddr);
uint64_t vmm_get_phys(uint64_t* pml4, uint64_t vaddr);
uint64_t* vmm_get_kernel_pml4(void);
void vmm_switch_pml4(uint64_t* pml4);

#endif
