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