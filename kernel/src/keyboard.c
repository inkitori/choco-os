#include "keyboard.h"
#include "ps2.h"
#include "stdint.h"
#include "lib.h"
#include "term.h"
#include "lib.h"

static char charBuf = 0;

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
	uint8_t scan_code = ps2_data_in();

	// term_print("scan code: ");
	char buf[10];
	to_string(scan_code, buf);

	// term_print(buf);

	switch (scan_code)
	{
	case 0x1C:
		term_print("Keyboard handler");
		charBuf = 'a';
		break;
	case 0x32:
		charBuf = 'b';
		break;
	case 0x21:
		charBuf = 'c';
		break;
	}

	buf[0] = charBuf;
	buf[1] = '\0';
	// term_print(buf);
}

char keyboard_getchar()
{
	while (1)
	{
		char buf[10];
		buf[0] = charBuf;
		buf[1] = '\0';
		term_print(buf);

		if (charBuf != 0)
		{
			break;
		}
	}

	term_print("fnally out");

	term_print("a");

	char retValue = charBuf;
	charBuf = 0;

	return retValue;
}