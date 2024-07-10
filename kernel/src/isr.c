#include "stdint.h"
#include "framebuffer.h"
#include "pic.h"
#include "ps2.h"
#include "io.h"
#include "lib.h"
#include "term.h"
#include "keyboard.h"
#include "timer.h"

void isr_timer_handler()
{
	// term_print("Timer interrupt");

	timer_handler();

	pic_send_eoi(PIC_TIMER_IRQ_LINE);
}

void isr_keyboard_handler()
{
	// term_print("Keyboard Interrupt Raised");
	keyboard_handler();

	pic_send_eoi(PIC_KEYBOARD_IRQ_LINE);
}

__attribute__((noreturn)) void isr_exception_handler(uint64_t exception)
{
	char buffer[64];
	to_string(exception, buffer);

	framebuffer_clear(0x000000);
	framebuffer_put_string("Exception occurred; system halted", 0, 0, 0xFF0000, 0x000000);
	framebuffer_put_string(buffer, 0, 1, 0xFF0000, 0x000000);

	__asm__ volatile("cli; hlt"); // Completely hangs the computer
}