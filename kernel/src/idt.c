#include "idt.h"
#include "io.h"

#include "stdbool.h"
#include "framebuffer.h"
#include "lib.h"
#include "term.h"
#include "ps2.h"
#include "pic.h"

#define IDT_MAX_DESCRIPTORS 256
#define GDT_OFFSET_KERNEL_CODE 0x28

static bool vectors[IDT_MAX_DESCRIPTORS];

extern void *isr_stub_table[];

static idtr_t idtr;

__attribute__((aligned(0x10))) static idt_entry_t idt[256]; // Create an array of IDT entries; aligned for performance

__attribute__((noreturn)) void exception_handler(uint64_t exception)
{
	char buffer[64];
	to_string(exception, buffer);

	framebuffer_clear(0x000000);
	framebuffer_put_string("Exception occurred; system halted", 0, 0, 0xFF0000, 0x000000);
	framebuffer_put_string(buffer, 0, 1, 0xFF0000, 0x000000);

	uint8_t keyboard = inb(0x60);
	to_string(keyboard, buffer);

	framebuffer_put_string(buffer, 0, 2, 0xFF0000, 0x000000);

	__asm__ volatile("cli; hlt"); // Completely hangs the computer
}

void idt_set_descriptor(uint8_t vector, void *isr, uint8_t flags)
{
	idt_entry_t *descriptor = &idt[vector];

	descriptor->isr_low = (uint64_t)isr & 0xFFFF;
	descriptor->kernel_cs = GDT_OFFSET_KERNEL_CODE;
	descriptor->ist = 0;
	descriptor->attributes = flags;
	descriptor->isr_mid = ((uint64_t)isr >> 16) & 0xFFFF;
	descriptor->isr_high = ((uint64_t)isr >> 32) & 0xFFFFFFFF;
	descriptor->reserved = 0;
}

void timer_handler()
{
	// term_print("Timer interrupt");

	pic_send_eoi(0);
}

void keyboard_handler()
{
	term_print("Keyboard Interrupt Raised");
	uint8_t scan_code = ps2_data_in();

	char buf[64];
	// term_clear(TERM_COLOR_BLACK);
	to_string(scan_code, buf);
	term_print(buf);

	pic_send_eoi(1);
}

void idt_init()
{
	idtr.base = (uintptr_t)&idt[0];
	idtr.limit = (uint16_t)sizeof(idt_entry_t) * IDT_MAX_DESCRIPTORS - 1;

	for (uint8_t vector = 0; vector < 32; vector++)
	{
		idt_set_descriptor(vector, isr_stub_table[vector], 0x8E);
		vectors[vector] = true;
	}

	idt_set_descriptor(32, isr_stub_table[32], 0x8E);
	vectors[32] = true;

	idt_set_descriptor(33, isr_stub_table[33], 0x8E);
	vectors[33] = true;

	__asm__ volatile("lidt %0" : : "m"(idtr));
	__asm__ volatile("sti");

	term_print("IDT initialized");
}