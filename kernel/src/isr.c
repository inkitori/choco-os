#include "stdint.h"
#include "framebuffer.h"
#include "pic.h"
#include "ps2.h"
#include "io.h"
#include "lib.h"
#include "term.h"
#include "keyboard.h"
#include "timer.h"
#include "asm_wrappers.h"

#define DIVISION_ERROR 0
#define DEBUG 1
#define NON_MASKABLE_INTERRUPT 2
#define BREAKPOINT 3
#define OVERFLOW 4
#define BOUND_RANGE_EXCEEDED 5
#define INVALID_OPCODE 6
#define DEVICE_NOT_AVAILABLE 7
#define DOUBLE_FAULT 8
#define COPROCESSOR_SEGMENT_OVERRUN 9
#define INVALID_TSS 0xA
#define SEGMENT_NOT_PRESENT 0xB
#define STACK_SEGMENT_FAULT 0xC
#define GENERAL_PROTECTION_FAULT 0xD
#define PAGE_FAULT 0xE
// 0xF Reserved
#define x87_FLOATING_POINT_EXCEPTION 0x10
#define ALIGNMENT_CHECK 0x11
#define MACHINE_CHECK 0x12
#define SIMD_FLOATING_POINT_EXCEPTION 0x13
#define VIRTUALIZATION_EXCEPTION 0x14
#define CONTROL_PROTECTION_EXCEPTION 0x15
// 0x16 - 0x1B Reserved
#define HYPERVISOR_INJECTION_EXCEPTION 0x1C
#define VMM_COMMUNICATION_EXCEPTION 0x1D
#define SECURITY_EXCEPTION 0x1E
// 0x1F Reserved

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

__attribute__((noreturn)) void isr_exception_handler(uint64_t exceptionIRQ)
{
	char buffer[64];
	to_string(exceptionIRQ, buffer);

	framebuffer_clear(0x000000);
	framebuffer_put_string("Exception occurred; system halted", 0, 0, 0xFF0000, 0x000000);
	framebuffer_put_string(buffer, 0, 1, 0xFF0000, 0x000000);

	hcf();
}