#include "vmm.h"
#include "pmm.h"
#include "limine.h"
#include "term.h"

extern volatile struct limine_kernel_address_request kernel_address_request;

extern volatile struct limine_hhdm_request hhdm_request;

static uint64_t vmm_hhdm_offset = 0;
static uint64_t* kernel_pml4 = NULL;

static inline void invlpg(uint64_t m) {
    asm volatile("invlpg (%0)" : : "r"(m) : "memory");
}

void vmm_init(void) {
    if (hhdm_request.response == NULL) {
        term_print_error("VMM: No HHDM response from bootloader.\n");
        return;
    }
    vmm_hhdm_offset = hhdm_request.response->offset;

    // Get the current CR3 (PML4 allocated by Limine)
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    
    // Physical address of PML4 is cr3 without the lower 12 bits
    kernel_pml4 = (uint64_t*)((cr3 & PTE_ADDR_MASK) + vmm_hhdm_offset);

    term_print_success("VMM initialized.\n");
}

uint64_t* vmm_get_kernel_pml4(void) {
    return kernel_pml4;
}

void vmm_switch_pml4(uint64_t* pml4) {
    uint64_t cr3 = (uint64_t)pml4 - vmm_hhdm_offset;
    asm volatile("mov %0, %%cr3" : : "r"(cr3));
}

static uint64_t* get_next_level(uint64_t* current_level, size_t index, bool allocate) {
    uint64_t entry = current_level[index];
    if ((entry & PTE_PRESENT) != 0) {
        // Return virtual address of the next level table
        uint64_t phys = entry & PTE_ADDR_MASK;
        return (uint64_t*)(phys + vmm_hhdm_offset);
    }

    if (!allocate) {
        return NULL;
    }

    void* new_table_phys = pmm_alloc_page();
    if (new_table_phys == NULL) {
        return NULL;
    }

    // Since pmm_alloc_page zeros the page, we don't need to do it here
    current_level[index] = (uint64_t)new_table_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;

    return (uint64_t*)((uint64_t)new_table_phys + vmm_hhdm_offset);
}

void vmm_map_page(uint64_t* pml4, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    size_t pml4_index = (vaddr >> 39) & 0x1ff;
    size_t pdp_index  = (vaddr >> 30) & 0x1ff;
    size_t pd_index   = (vaddr >> 21) & 0x1ff;
    size_t pt_index   = (vaddr >> 12) & 0x1ff;

    uint64_t* pdp = get_next_level(pml4, pml4_index, true);
    if (!pdp) return;

    uint64_t* pd = get_next_level(pdp, pdp_index, true);
    if (!pd) return;

    uint64_t* pt = get_next_level(pd, pd_index, true);
    if (!pt) return;

    pt[pt_index] = (paddr & PTE_ADDR_MASK) | flags | PTE_PRESENT;
    invlpg(vaddr);
}

void vmm_unmap_page(uint64_t* pml4, uint64_t vaddr) {
    size_t pml4_index = (vaddr >> 39) & 0x1ff;
    size_t pdp_index  = (vaddr >> 30) & 0x1ff;
    size_t pd_index   = (vaddr >> 21) & 0x1ff;
    size_t pt_index   = (vaddr >> 12) & 0x1ff;

    uint64_t* pdp = get_next_level(pml4, pml4_index, false);
    if (!pdp) return;

    uint64_t* pd = get_next_level(pdp, pdp_index, false);
    if (!pd) return;

    uint64_t* pt = get_next_level(pd, pd_index, false);
    if (!pt) return;

    pt[pt_index] = 0;
    invlpg(vaddr);
}

uint64_t vmm_get_phys(uint64_t* pml4, uint64_t vaddr) {
    size_t pml4_index = (vaddr >> 39) & 0x1ff;
    size_t pdp_index  = (vaddr >> 30) & 0x1ff;
    size_t pd_index   = (vaddr >> 21) & 0x1ff;
    size_t pt_index   = (vaddr >> 12) & 0x1ff;

    uint64_t* pdp = get_next_level(pml4, pml4_index, false);
    if (!pdp) return 0;

    uint64_t* pd = get_next_level(pdp, pdp_index, false);
    if (!pd) return 0;

    uint64_t* pt = get_next_level(pd, pd_index, false);
    if (!pt) return 0;

    if (!(pt[pt_index] & PTE_PRESENT)) return 0;

    return (pt[pt_index] & PTE_ADDR_MASK) + (vaddr & 0xfff);
}
