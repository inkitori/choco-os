extern void trigger_test_interrupt(void);

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include "framebuffer.h"
#include "asm_wrappers.h"
#include "idt.h"
#include "lib.h"
#include "term.h"
#include "ps2.h"
#include "keyboard.h"

__attribute__((used, section(".requests"))) static volatile LIMINE_BASE_REVISION(2);

__attribute__((used, section(".requests_start_marker"))) static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end_marker"))) static volatile LIMINE_REQUESTS_END_MARKER;

__attribute__((used, section(".requests"))) static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0};

__attribute__((used, section(".requests"))) static volatile struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0};

// The following will be our kernel's entry point.
// If renaming _start() to something else, make sure to change the
// linker script accordingly.
void _start(void)
{
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED == false)
    {
        hcf();
    }

    term_init();
    ps2_init_controller();
    keyboard_init();

    // struct limine_memmap_entry **entries = memmap_request.response->entries;

    // for (int i = 0; (uint64_t)i < memmap_request.response->entry_count; i++)
    // {
    //     uint64_t memory_type = entries[i]->type;

    //     char baseBuf[64];
    //     to_string(entries[i]->base, baseBuf);

    //     char lengthBuf[64];
    //     to_string(entries[i]->length, lengthBuf);

    //     switch (memory_type)
    //     {
    //     case LIMINE_MEMMAP_USABLE:
    //         framebuffer_put_string("[Usable RAM]", 0, 3 * i, 0xFFFFFF, 0x272C34);
    //         framebuffer_put_string(baseBuf, 0, 3 * i + 1, 0xFFFFFF, 0x272C34);
    //         framebuffer_put_string(lengthBuf, 0, 3 * i + 2, 0xFFFFFF, 0x272C34);
    //         break;

    //     case LIMINE_MEMMAP_RESERVED:
    //         framebuffer_put_string("[Reserved]", 0, 3 * i, 0xFFFFFF, 0x272C34);
    //         framebuffer_put_string(baseBuf, 0, 3 * i + 1, 0xFFFFFF, 0x272C34);
    //         framebuffer_put_string(lengthBuf, 0, 3 * i + 2, 0xFFFFFF, 0x272C34);
    //         break;

    //     case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
    //         framebuffer_put_string("[ACPI Reclaimable]", 0, 3 * i, 0xFFFFFF, 0x272C34);
    //         framebuffer_put_string(baseBuf, 0, 3 * i + 1, 0xFFFFFF, 0x272C34);
    //         framebuffer_put_string(lengthBuf, 0, 3 * i + 2, 0xFFFFFF, 0x272C34);
    //         break;

    //     case LIMINE_MEMMAP_ACPI_NVS:
    //         framebuffer_put_string("[ACPI NVS]", 0, 3 * i, 0xFFFFFF, 0x272C34);
    //         framebuffer_put_string(baseBuf, 0, 3 * i + 1, 0xFFFFFF, 0x272C34);
    //         framebuffer_put_string(lengthBuf, 0, 3 * i + 2, 0xFFFFFF, 0x272C34);
    //         break;

    //     case LIMINE_MEMMAP_BAD_MEMORY:
    //         framebuffer_put_string("[Bad Memory]", 0, 3 * i, 0xFFFFFF, 0x272C34);
    //         framebuffer_put_string(baseBuf, 0, 3 * i + 1, 0xFFFFFF, 0x272C34);
    //         framebuffer_put_string(lengthBuf, 0, 3 * i + 2, 0xFFFFFF, 0x272C34);
    //         break;

    //     case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
    //         framebuffer_put_string("[Bootloader Reclaimable]", 0, 3 * i, 0xFFFFFF, 0x272C34);
    //         framebuffer_put_string(baseBuf, 0, 3 * i + 1, 0xFFFFFF, 0x272C34);
    //         framebuffer_put_string(lengthBuf, 0, 3 * i + 2, 0xFFFFFF, 0x272C34);
    //         break;

    //     case LIMINE_MEMMAP_KERNEL_AND_MODULES:
    //         framebuffer_put_string("[Kernel & Modules]", 0, 3 * i, 0xFFFFFF, 0x272C34);
    //         framebuffer_put_string(baseBuf, 0, 3 * i + 1, 0xFFFFFF, 0x272C34);
    //         framebuffer_put_string(lengthBuf, 0, 3 * i + 2, 0xFFFFFF, 0x272C34);
    //         break;

    //     case LIMINE_MEMMAP_FRAMEBUFFER:
    //         framebuffer_put_string("[Framebuffer]", 0, 3 * i, 0xFFFFFF, 0x272C34);
    //         framebuffer_put_string(baseBuf, 0, 3 * i + 1, 0xFFFFFF, 0x272C34);
    //         framebuffer_put_string(lengthBuf, 0, 3 * i + 2, 0xFFFFFF, 0x272C34);
    //         break;

    //     default:
    //         // Handle unknown memory types or add logging/error handling
    //         break;
    //     }
    // }

    // idt_init();

    // framebuffer_put_string("IDT Loaded", 0, 1, 0xFFFFFF, 0x272C34);

    // trigger_test_interrupt();

    idt_init();

    while (1)
    {
    }

    term_print("Hopefully you can't see this");
}
