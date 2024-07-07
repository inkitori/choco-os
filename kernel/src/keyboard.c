#include "keyboard.h"
#include "ps2.h"
#include "stdint.h"
#include "lib.h"
#include "term.h"
#include <stdint.h>

void keyboard_init()
{
	ps2_data_out(KEYBOARD_ENABLE_SCANNING);

	// while (1)
	// {
	// 	uint8_t scan_code = ps2_data_in();

	// 	char buf[64];
	// 	to_string(scan_code, buf);
	// 	term_print(buf);
	// }
}