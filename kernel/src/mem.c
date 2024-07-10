#include "mem.h"
#include "limine.h"
#include "term.h"
#include "lib.h"

__attribute__((used, section(".requests"))) static volatile struct limine_memmap_request memmap_request = {
	.id = LIMINE_MEMMAP_REQUEST,
	.revision = 0};

void debug_memmap()
{
	struct limine_memmap_entry **entries = memmap_request.response->entries;

	for (int i = 0; i < memmap_request.response->entry_count; i++)
	{
		uint64_t memory_type = entries[i]->type;

		char baseBuf[64];
		to_string(entries[i]->base, baseBuf);

		char lengthBuf[64];
		to_string(entries[i]->length, lengthBuf);

		switch (memory_type)
		{
		case LIMINE_MEMMAP_USABLE:
			term_print_with_color("[Usable RAM]\n", 0x0cb1e8, TERM_COLOR_BLACK);

			term_print(baseBuf);
			term_print("\n");
			term_print(lengthBuf);
			term_print("\n");
			break;

		case LIMINE_MEMMAP_RESERVED:
			term_print_with_color("[Reserved]\n", 0x0cb1e8, TERM_COLOR_BLACK);

			term_print(baseBuf);
			term_print("\n");
			term_print(lengthBuf);
			term_print("\n");
			break;

		case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
			term_print_with_color("[ACPI Reclaimable]\n", 0x0cb1e8, TERM_COLOR_BLACK);

			term_print(baseBuf);
			term_print("\n");
			term_print(lengthBuf);
			term_print("\n");
			break;

		case LIMINE_MEMMAP_ACPI_NVS:
			term_print_with_color("[ACPI NVS]\n", 0x0cb1e8, TERM_COLOR_BLACK);

			term_print(baseBuf);
			term_print("\n");
			term_print(lengthBuf);
			term_print("\n");
			break;

		case LIMINE_MEMMAP_BAD_MEMORY:
			term_print_with_color("[Bad Memory]\n", 0x0cb1e8, TERM_COLOR_BLACK);

			term_print(baseBuf);
			term_print("\n");
			term_print(lengthBuf);
			term_print("\n");
			break;

		case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
			term_print_with_color("[Bootloader Reclaimable]\n", 0x0cb1e8, TERM_COLOR_BLACK);

			term_print(baseBuf);
			term_print("\n");
			term_print(lengthBuf);
			term_print("\n");
			break;

		case LIMINE_MEMMAP_KERNEL_AND_MODULES:
			term_print_with_color("[Kernel and Modules]\n", 0x0cb1e8, TERM_COLOR_BLACK);

			term_print(baseBuf);
			term_print("\n");
			term_print(lengthBuf);
			term_print("\n");
			break;

		case LIMINE_MEMMAP_FRAMEBUFFER:
			term_print_with_color("[Framebuffer]\n", 0x0cb1e8, TERM_COLOR_BLACK);

			term_print(baseBuf);
			term_print("\n");
			term_print(lengthBuf);
			term_print("\n");
			break;

		default:
			term_print_with_color("[Unknown Memory Type]\n", TERM_COLOR_WHITE, TERM_COLOR_RED);
			break;
		}
	}
}