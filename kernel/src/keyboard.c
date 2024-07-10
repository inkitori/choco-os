#include "keyboard.h"
#include "ps2.h"
#include "io.h"
#include "stdint.h"
#include "lib.h"
#include "term.h"
#include "lib.h"

static uint8_t scan_code = 0;

void keyboard_init()
{
	ps2_data_out(KEYBOARD_ENABLE_SCANNING);
	ps2_data_in(); // flush for ack
				   // TODO: have actual wrapper function that handles checking for ack and resends

	ps2_data_out(KEYBOARD_SCAN_CODE_SET); // set scancode set
	ps2_data_in();

	ps2_data_out(KEYBOARD_SET_SCAN_CODE_2);
	ps2_data_in();

	term_print("Keyboard initialized");
}

void keyboard_handler()
{
	scan_code = ps2_data_in();
}

uint8_t keyboard_get_scan_code()
{
	return scan_code;
}
