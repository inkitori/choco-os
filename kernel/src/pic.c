#include "pic.h"

#include "stdint.h"
#include "io.h"
#include "term.h"

void pic_init()
{
	pic_remap(PIC_MASTER_OFFSET, PIC_SLAVE_OFFSET);

	// pic_unmask_irq(PIC_TIMER_IRQ_LINE); // timer
	// FIXME: FOR WHATEVER REASON WHEN THE TIMER IRQ WAS UNMASKED THERE WOULD BE A PAGE FAULT WHEN ACCESSING THE FRAMEBUFFER???
	// LOOK INTO THIS LATER

	pic_unmask_irq(PIC_KEYBOARD_IRQ_LINE); // keyboard

	term_print("PIC initialized");
}

static inline void pic_out(uint16_t port, uint8_t val)
{
	outb(port, val);
	io_wait();
}

void pic_remap(int master_offset, int slave_offset)
{
	uint8_t a1, a2;

	a1 = inb(PIC1_DATA); // save masks
	a2 = inb(PIC2_DATA);

	pic_out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4); // starts the initialization sequence (in cascade mode)
	pic_out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
	pic_out(PIC1_DATA, master_offset); // ICW2: Master PIC vector offset
	pic_out(PIC2_DATA, slave_offset);  // ICW2: Slave PIC vector offset
	pic_out(PIC1_DATA, 4);			   // ICW3: tell Master PIC that there is a slave PIC at IRQ2 (0000 0100)
	pic_out(PIC2_DATA, 2);			   // ICW3: tell Slave PIC its cascade identity (0000 0010)

	pic_out(PIC1_DATA, ICW4_8086); // ICW4: have the PICs use 8086 mode (and not 8080 mode)
	pic_out(PIC2_DATA, ICW4_8086);

	// not really sure why we dont have to wait here?
	outb(PIC1_DATA, a1); // restore saved masks.
	outb(PIC2_DATA, a2);
}

void pic_mask_irq(uint8_t irq_line)
{
	uint16_t port;
	uint8_t value;

	if (irq_line < PIC_SLAVE_IRQ_START)
	{
		port = PIC1_DATA;
	}
	else
	{
		port = PIC2_DATA;
		irq_line -= PIC_SLAVE_IRQ_START;
	}
	value = inb(port) | (1 << irq_line);
	outb(port, value);
}

void pic_unmask_irq(uint8_t irq_line)
{
	uint16_t port;
	uint8_t value;

	if (irq_line < PIC_SLAVE_IRQ_START)
	{
		port = PIC1_DATA;
	}
	else
	{
		port = PIC2_DATA;
		irq_line -= PIC_SLAVE_IRQ_START;
	}
	value = inb(port) & ~(1 << irq_line);
	outb(port, value);
}

void pic_send_eoi(uint8_t irq)
{
	if (irq >= PIC_SLAVE_IRQ_START)
		outb(PIC2_COMMAND, PIC_EOI);

	outb(PIC1_COMMAND, PIC_EOI);
}