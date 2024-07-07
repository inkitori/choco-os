#include "stdint.h"
#include "framebuffer.h"
#include "pic.h"
#include "ps2.h"
#include "io.h"
#include "lib.h"
#include "term.h"

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

__attribute__((noreturn)) void exception_handler(uint64_t exception)
{
	char buffer[64];
	to_string(exception, buffer);

	framebuffer_clear(0x000000);
	framebuffer_put_string("Exception occurred; system halted", 0, 0, 0xFF0000, 0x000000);
	framebuffer_put_string(buffer, 0, 1, 0xFF0000, 0x000000);

	__asm__ volatile("cli; hlt"); // Completely hangs the computer
}