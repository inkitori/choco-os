#include <stdint.h>
#include "framebuffer.h"
#include "pic.h"
#include "io.h"
#include "kprintf.h"
#include "term.h"
#include "keyboard.h"
#include "timer.h"
#include "sched.h"
#include "asm_utils.h"

uint64_t isr_timer_handler(uint64_t rsp)
{
	timer_handler();
	pic_send_eoi(PIC_TIMER_IRQ_LINE);
	return sched_tick(rsp);
}

uint64_t isr_yield_handler(uint64_t rsp)
{
	return sched_preempt(rsp);
}

void isr_keyboard_handler(void)
{
	keyboard_handler();
	pic_send_eoi(PIC_KEYBOARD_IRQ_LINE);
}

static const char *exception_names[32] = {
	"Division Error", "Debug", "NMI", "Breakpoint", "Overflow",
	"Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
	"Double Fault", "Coprocessor Segment Overrun", "Invalid TSS",
	"Segment Not Present", "Stack-Segment Fault", "General Protection Fault",
	"Page Fault", "Reserved", "x87 FP Exception", "Alignment Check",
	"Machine Check", "SIMD FP Exception", "Virtualization Exception",
	"Control Protection Exception", "Reserved", "Reserved", "Reserved",
	"Reserved", "Reserved", "Reserved", "Hypervisor Injection",
	"VMM Communication", "Security Exception", "Reserved"};

__attribute__((noreturn)) void isr_exception_handler(uint64_t vector,
													 uint64_t error_code,
													 uint64_t rip)
{
	const char *name = vector < 32 ? exception_names[vector] : "Unknown";

	kprintf("\n!!! EXCEPTION %lu (%s) err=%lx rip=%lx\n", vector, name,
			error_code, rip);
	if (vector == 14)
	{
		uint64_t cr2;
		__asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
		kprintf("    page fault address: %lx\n", cr2);
	}

	char buf[128];
	framebuffer_clear(0x300000);
	snprintf(buf, sizeof(buf), "EXCEPTION %lu: %s", vector, name);
	framebuffer_put_string(buf, 2, 2, 0xFFFFFF, 0x300000);
	snprintf(buf, sizeof(buf), "error=0x%lx rip=0x%lx", error_code, rip);
	framebuffer_put_string(buf, 2, 4, 0xFFFFFF, 0x300000);
	framebuffer_put_string("system halted", 2, 6, 0xFF8888, 0x300000);

	hcf();
}
