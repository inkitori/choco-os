#include "idt.h"
#include "io.h"

#include "stdbool.h"
#include "framebuffer.h"
#include "lib.h"
#include "term.h"
#include "ps2.h"
#include "pic.h"

extern void *isr_stub_table[];

static idtr_t idtr;

__attribute__((aligned(0x10))) static idt_entry_t idt[IDT_MAX_DESCRIPTORS]; // Create an array of IDT entries; aligned for performance

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

	for (uint8_t vector = 0; vector < IDT_EXCEPTION_COUNT; vector++)
	{
		idt_set_descriptor(vector, isr_stub_table[vector], IDT_INTERRUPT_GATE); // should this be a trap gate instead?
	}

	// TODO: write custom functions for registering IRQs
	idt_set_descriptor(32, isr_stub_table[32], IDT_INTERRUPT_GATE); // Timer
	idt_set_descriptor(33, isr_stub_table[33], IDT_INTERRUPT_GATE); // Keyboard

	__asm__ volatile("lidt %0" : : "m"(idtr));
	__asm__ volatile("sti");

	term_print_success("IDT initialized");
}