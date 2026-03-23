#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include "idt.h"
#include "framebuffer.h"
#include "lib.h"
#include "term.h"
#include "ps2.h"
#include "keyboard.h"
#include "pic.h"
#include "shell.h"
#include "snake.h"
#include "asm_utils.h"
#include "pmm.h"
#include "vmm.h"

__attribute__((used, section(".requests"))) static volatile LIMINE_BASE_REVISION(2);

__attribute__((used, section(".requests_start_marker"))) static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end_marker"))) static volatile LIMINE_REQUESTS_END_MARKER;


__attribute__((used, section(".requests"))) volatile struct limine_hhdm_request hhdm_request = {
	.id = LIMINE_HHDM_REQUEST,
	.revision = 0};

__attribute__((used, section(".requests"))) volatile struct limine_kernel_address_request kernel_address_request = {
	.id = LIMINE_KERNEL_ADDRESS_REQUEST,
	.revision = 0};

__attribute__((used, section(".requests"))) volatile struct limine_memmap_request memmap_request = {
	.id = LIMINE_MEMMAP_REQUEST,
	.revision = 0};


void _start(void)
{
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED == false)
    {
        hcf();
    }

    term_init();
    pmm_init();
    vmm_init();
    ps2_init_controller();
    keyboard_init();
    pic_init();
    idt_init();

    shell_init();

    spinlock();
}
