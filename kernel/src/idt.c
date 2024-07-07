#include "idt.h"
#include "io.h"

#include "stdbool.h"
#include "framebuffer.h"
#include "lib.h"

#define IDT_MAX_DESCRIPTORS 256
#define GDT_OFFSET_KERNEL_CODE 0x28

static bool vectors[IDT_MAX_DESCRIPTORS];

extern void *isr_stub_table[];

static idtr_t idtr;

#define PIC1 0x20 /* IO base address for master PIC */
#define PIC2 0xA0 /* IO base address for slave PIC */
#define PIC1_COMMAND PIC1
#define PIC1_DATA (PIC1 + 1)
#define PIC2_COMMAND PIC2
#define PIC2_DATA (PIC2 + 1)
/* reinitialize the PIC controllers, giving them specified vector offsets
   rather than 8h and 70h, as configured by default */

#define ICW1_ICW4 0x01		/* Indicates that ICW4 will be present */
#define ICW1_SINGLE 0x02	/* Single (cascade) mode */
#define ICW1_INTERVAL4 0x04 /* Call address interval 4 (8) */
#define ICW1_LEVEL 0x08		/* Level triggered (edge) mode */
#define ICW1_INIT 0x10		/* Initialization - required! */

#define ICW4_8086 0x01		 /* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO 0x02		 /* Auto (normal) EOI */
#define ICW4_BUF_SLAVE 0x08	 /* Buffered mode/slave */
#define ICW4_BUF_MASTER 0x0C /* Buffered mode/master */
#define ICW4_SFNM 0x10		 /* Special fully nested (not) */

/*
arguments:
	offset1 - vector offset for master PIC
		vectors on the master become offset1..offset1+7
	offset2 - same for slave PIC: offset2..offset2+7
*/
void PIC_remap(int offset1, int offset2)
{
	uint8_t a1, a2;

	a1 = inb(PIC1_DATA); // save masks
	a2 = inb(PIC2_DATA);

	outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4); // starts the initialization sequence (in cascade mode)
	io_wait();
	outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
	io_wait();
	outb(PIC1_DATA, offset1); // ICW2: Master PIC vector offset
	io_wait();
	outb(PIC2_DATA, offset2); // ICW2: Slave PIC vector offset
	io_wait();
	outb(PIC1_DATA, 4); // ICW3: tell Master PIC that there is a slave PIC at IRQ2 (0000 0100)
	io_wait();
	outb(PIC2_DATA, 2); // ICW3: tell Slave PIC its cascade identity (0000 0010)
	io_wait();

	outb(PIC1_DATA, ICW4_8086); // ICW4: have the PICs use 8086 mode (and not 8080 mode)
	io_wait();
	outb(PIC2_DATA, ICW4_8086);
	io_wait();

	outb(PIC1_DATA, a1); // restore saved masks.
	outb(PIC2_DATA, a2);
}

void IRQ_set_mask(uint8_t IRQline)
{
	uint16_t port;
	uint8_t value;

	if (IRQline < 8)
	{
		port = PIC1_DATA;
	}
	else
	{
		port = PIC2_DATA;
		IRQline -= 8;
	}
	value = inb(port) | (1 << IRQline);
	outb(port, value);
}

void IRQ_clear_mask(uint8_t IRQline)
{
	uint16_t port;
	uint8_t value;

	if (IRQline < 8)
	{
		port = PIC1_DATA;
	}
	else
	{
		port = PIC2_DATA;
		IRQline -= 8;
	}
	value = inb(port) & ~(1 << IRQline);
	outb(port, value);
}

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

__attribute__((noreturn)) void keyboard_handler()
{
	framebuffer_clear(0x000000);
	framebuffer_put_string("Keyboard IRQ raised 1", 0, 0, 0xFF0000, 0x000000);
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

#define KB_CONTROLLER_DATA 0x60	   // keyboard controller data register
#define KB_CONTROLLER_COMMAND 0x64 // keyboard controller command register

	// Disabling PS/2 Devices
	outb(KB_CONTROLLER_COMMAND, 0xAD);
	outb(KB_CONTROLLER_COMMAND, 0xA7);

	// Flushing output buffer
	inb(KB_CONTROLLER_DATA);

	// clearing bits 0 1 and 6 of the configuration byte
	outb(KB_CONTROLLER_COMMAND, 0x20);
	uint8_t configByte = inb(KB_CONTROLLER_DATA);

	configByte ^= 1 << 0;
	configByte ^= 1 << 1;
	configByte ^= 1 << 6;

	outb(KB_CONTROLLER_COMMAND, 0x60);
	while (inb(KB_CONTROLLER_COMMAND) & 0x2)
	{
		io_wait();
	}

	outb(KB_CONTROLLER_DATA, configByte);

	// self test
	outb(KB_CONTROLLER_COMMAND, 0xAA);

	while (!(inb(KB_CONTROLLER_COMMAND) & 0x1))
	{
		io_wait();
	}

	uint8_t selfCheck = inb(KB_CONTROLLER_DATA);

	if (selfCheck != 0x55)
	{
		framebuffer_clear(0x000000);
		framebuffer_put_string("PS/2 controller self test failed", 0, 0, 0xFF0000, 0x000000);
		__asm__ volatile("cli; hlt");
	}

	// Re enabling PS/2 Ports
	outb(KB_CONTROLLER_COMMAND, 0xAE);

	// Setting bit 0 of the configuration byte
	outb(KB_CONTROLLER_COMMAND, 0x20);
	configByte = inb(KB_CONTROLLER_DATA);

	configByte |= 1 << 0;

	outb(KB_CONTROLLER_COMMAND, 0x60);
	while (inb(KB_CONTROLLER_COMMAND) & 0x2)
	{
		io_wait();
	}

	outb(KB_CONTROLLER_DATA, configByte);

	outb(KB_CONTROLLER_DATA, 0xFF);

	while (inb(KB_CONTROLLER_DATA) != 0xAA)
	{
		io_wait();
	}

	while (inb(KB_CONTROLLER_COMMAND) & 0x2)
	{
		outb(KB_CONTROLLER_DATA, 0xF4);
		io_wait();
	}

	framebuffer_clear(0x000000);
	framebuffer_put_string("LIDT", 0, 0, 0xFF0000, 0x000000);

	PIC_remap(0x20, 0x28);
	// IRQ_clear_mask(0);
	IRQ_clear_mask(1);

	__asm__ volatile("lidt %0" : : "m"(idtr));
	__asm__ volatile("sti");
}