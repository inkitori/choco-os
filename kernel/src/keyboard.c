#include "keyboard.h"
#include "ps2.h"
#include "io.h"
#include "stdint.h"
#include "lib.h"
#include "term.h"
#include "lib.h"
#include "stdbool.h"

static uint8_t scan_code = 0;
static char key_buffer[KEYBOARD_BUFFER_SIZE];
static uint8_t read_pointer = 0;
static uint8_t write_pointer = 0;

static bool release_state = false;

static inline char keyboard_convert_scan_code(uint8_t scan_code)
{
	switch (scan_code)
	{
	case KEYBOARD_SCAN_CODE_A:
		return 'a';
	case KEYBOARD_SCAN_CODE_B:
		return 'b';
	case KEYBOARD_SCAN_CODE_C:
		return 'c';
	case KEYBOARD_SCAN_CODE_D:
		return 'd';
	case KEYBOARD_SCAN_CODE_E:
		return 'e';
	case KEYBOARD_SCAN_CODE_F:
		return 'f';
	case KEYBOARD_SCAN_CODE_G:
		return 'g';
	case KEYBOARD_SCAN_CODE_H:
		return 'h';
	case KEYBOARD_SCAN_CODE_I:
		return 'i';
	case KEYBOARD_SCAN_CODE_J:
		return 'j';
	case KEYBOARD_SCAN_CODE_K:
		return 'k';
	case KEYBOARD_SCAN_CODE_L:
		return 'l';
	case KEYBOARD_SCAN_CODE_M:
		return 'm';
	case KEYBOARD_SCAN_CODE_N:
		return 'n';
	case KEYBOARD_SCAN_CODE_O:
		return 'o';
	case KEYBOARD_SCAN_CODE_P:
		return 'p';
	case KEYBOARD_SCAN_CODE_Q:
		return 'q';
	case KEYBOARD_SCAN_CODE_R:
		return 'r';
	case KEYBOARD_SCAN_CODE_S:
		return 's';
	case KEYBOARD_SCAN_CODE_T:
		return 't';
	case KEYBOARD_SCAN_CODE_U:
		return 'u';
	case KEYBOARD_SCAN_CODE_V:
		return 'v';
	case KEYBOARD_SCAN_CODE_W:
		return 'w';
	case KEYBOARD_SCAN_CODE_X:
		return 'x';
	case KEYBOARD_SCAN_CODE_Y:
		return 'y';
	case KEYBOARD_SCAN_CODE_Z:
		return 'z';
	default:
		return 0;
	}
}

void keyboard_init()
{
	ps2_data_out(KEYBOARD_ENABLE_SCANNING);
	ps2_data_in(); // flush for ack
				   // TODO: have actual wrapper function that handles checking for ack and resends

	ps2_data_out(KEYBOARD_SCAN_CODE_SET); // set scancode set
	ps2_data_in();

	ps2_data_out(KEYBOARD_SET_SCAN_CODE_2);
	ps2_data_in();

	term_print("Keyboard initialized\n");
}

void keyboard_handler()
{
	if ((write_pointer + 1) % KEYBOARD_BUFFER_SIZE == read_pointer)
		return;

	scan_code = ps2_data_in();

	if (scan_code == KEYBOARD_SCAN_CODE_RELEASE)
	{
		release_state = true;
		return;
	}
	if (release_state)
	{
		release_state = false;
		return;
	}

	char c = keyboard_convert_scan_code(scan_code);
	if (c == 0)
		return;

	key_buffer[write_pointer] = c;
	write_pointer = (write_pointer + 1) % KEYBOARD_BUFFER_SIZE;
}

uint8_t keyboard_get_scan_code()
{

	return scan_code;
}

char keyboard_getch()
{
	char retVal = key_buffer[read_pointer];
	if (retVal == 0)
		return 0;
	key_buffer[read_pointer] = 0;
	read_pointer = (read_pointer + 1) % KEYBOARD_BUFFER_SIZE;

	return retVal;
}