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
#include "asm_utils.h"
#include "pmm.h"
#include "vmm.h"
#include "malloc.h"
#include "serial.h"
#include "kprintf.h"
#include "timer.h"
#include "sched.h"
#include "initrd.h"
#include "net.h"

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

__attribute__((used, section(".requests"))) volatile struct limine_module_request module_request = {
	.id = LIMINE_MODULE_REQUEST,
	.revision = 0};

// Enable SSE so the LLM engine (and anything else compiled with hard-float)
// can run. Limine sets most of this up already; make it explicit.
static void enable_sse(void)
{
	uint64_t cr0, cr4;
	__asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
	cr0 &= ~(1ull << 2); // clear EM
	cr0 |= (1ull << 1);	 // set MP
	__asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

	__asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
	cr4 |= (1ull << 9) | (1ull << 10); // OSFXSR | OSXMMEXCPT
	__asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
}

static void shell_thread(void *arg)
{
	(void)arg;
	shell_init();
}

void _start(void)
{
	if (LIMINE_BASE_REVISION_SUPPORTED == false)
	{
		hcf();
	}

	enable_sse();
	serial_init();
	kprintf("\nchoco-os: booting\n");

	term_init();
	pmm_init();
	heap_init();
	vmm_init();
	ps2_init_controller();
	keyboard_init();
	timer_init();
	initrd_init();
	pic_init();
	idt_init();
	sched_init();
	net_init();

	thread_create("shell", shell_thread, NULL);
	sched_start(); // becomes the idle thread, never returns

	hcf();
}
