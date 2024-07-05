extern void trigger_test_interrupt(void);

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include "framebuffer.h"
#include "asm_wrappers.h"
#include "idt.h"

void to_string(uint64_t num, char *str)
{
    int i = 0;
    while (num > 0)
    {
        str[i] = num % 10 + '0';
        num /= 10;
        i++;
    }
    str[i] = '\0';
    int j = 0;
    i--;
    while (j < i)
    {
        char temp = str[j];
        str[j] = str[i];
        str[i] = temp;
        j++;
        i--;
    }
}

// Set the base revision to 2, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

__attribute__((used, section(".requests"))) static volatile LIMINE_BASE_REVISION(2);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used, section(".requests_start_marker"))) static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end_marker"))) static volatile LIMINE_REQUESTS_END_MARKER;

__attribute__((used, section(".requests"))) static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0};

__attribute__((used, section(".requests"))) static volatile struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0};
// GCC and Clang reserve the right to generate calls to the following
// 4 functions even if they are not directly called.
// Implement them as the C specification mandates.
// DO NOT remove or rename these functions, or stuff will eventually break!
// They CAN be moved to a different .c file.

void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    for (size_t i = 0; i < n; i++)
    {
        pdest[i] = psrc[i];
    }

    return dest;
}

void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;

    for (size_t i = 0; i < n; i++)
    {
        p[i] = (uint8_t)c;
    }

    return s;
}

void *memmove(void *dest, const void *src, size_t n)
{
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    if (src > dest)
    {
        for (size_t i = 0; i < n; i++)
        {
            pdest[i] = psrc[i];
        }
    }
    else if (src < dest)
    {
        for (size_t i = n; i > 0; i--)
        {
            pdest[i - 1] = psrc[i - 1];
        }
    }

    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;

    for (size_t i = 0; i < n; i++)
    {
        if (p1[i] != p2[i])
        {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}

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

    framebuffer_init();
    framebuffer_clear(0x272C34);

    char kernel_address[64];
    to_string(kernel_address_request.response->physical_base, kernel_address);

    framebuffer_put_string(kernel_address, 0, 0, 0xFFFFFF, 0x272C34);

    // framebuffer_put_string("ChocoOS", 0, 0, 0xFFFFFF, 0x272C34);
    // framebuffer_put_string(">", 0, 16, 0xFFFFFF, 0x272C34);

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

    idt_init();

    trigger_test_interrupt();

    // We're done, just hang...
    hcf();
}
