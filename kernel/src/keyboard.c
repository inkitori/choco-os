#include "keyboard.h"
#include "ps2.h"
#include "io.h"
#include "stdint.h"
#include "lib.h"
#include "term.h"
#include "lib.h"
#include "stdbool.h"

static uint8_t scan_code = 0;
static Key key_buffer[KEYBOARD_BUFFER_SIZE];
static uint8_t read_pointer = 0;
static uint8_t write_pointer = 0;

static bool release_state = false;

static inline Key keyboard_convert_scan_code(uint8_t scan_code)
{
	switch (scan_code)
	{
	case KEYBOARD_SCAN_CODE_A:
		return A;
	case KEYBOARD_SCAN_CODE_B:
		return B;
	case KEYBOARD_SCAN_CODE_C:
		return C;
	case KEYBOARD_SCAN_CODE_D:
		return D;
	case KEYBOARD_SCAN_CODE_E:
		return E;
	case KEYBOARD_SCAN_CODE_F:
		return F;
	case KEYBOARD_SCAN_CODE_G:
		return G;
	case KEYBOARD_SCAN_CODE_H:
		return H;
	case KEYBOARD_SCAN_CODE_I:
		return I;
	case KEYBOARD_SCAN_CODE_J:
		return J;
	case KEYBOARD_SCAN_CODE_K:
		return K;
	case KEYBOARD_SCAN_CODE_L:
		return L;
	case KEYBOARD_SCAN_CODE_M:
		return M;
	case KEYBOARD_SCAN_CODE_N:
		return N;
	case KEYBOARD_SCAN_CODE_O:
		return O;
	case KEYBOARD_SCAN_CODE_P:
		return P;
	case KEYBOARD_SCAN_CODE_Q:
		return Q;
	case KEYBOARD_SCAN_CODE_R:
		return R;
	case KEYBOARD_SCAN_CODE_S:
		return S;
	case KEYBOARD_SCAN_CODE_T:
		return T;
	case KEYBOARD_SCAN_CODE_U:
		return U;
	case KEYBOARD_SCAN_CODE_V:
		return V;
	case KEYBOARD_SCAN_CODE_W:
		return W;
	case KEYBOARD_SCAN_CODE_X:
		return X;
	case KEYBOARD_SCAN_CODE_Y:
		return Y;
	case KEYBOARD_SCAN_CODE_Z:
		return Z;
	case KEYBOARD_SCAN_CODE_ENTER:
		return ENTER;
	case KEYBOARD_SCAN_CODE_SPACE:
		return SPACE;
	case KEYBOARD_SCAN_CODE_ESCAPE:
		return ESCAPE;
	default:
		return NONE;
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

	Key key = keyboard_convert_scan_code(scan_code);
	if (key == NONE)
		return;

	key_buffer[write_pointer] = key;
	write_pointer = (write_pointer + 1) % KEYBOARD_BUFFER_SIZE;
}

uint8_t keyboard_get_scan_code()
{
	return scan_code;
}

Key keyboard_get_key()
{
	if (read_pointer == write_pointer)
		return NONE;

	char retVal = key_buffer[read_pointer];
	read_pointer = (read_pointer + 1) % KEYBOARD_BUFFER_SIZE;

	return retVal;
}

char keyboard_key_to_char(Key key)
{
	switch (key)
	{
	case A:
		return 'a';
	case B:
		return 'b';
	case C:
		return 'c';
	case D:
		return 'd';
	case E:
		return 'e';
	case F:
		return 'f';
	case G:
		return 'g';
	case H:
		return 'h';
	case I:
		return 'i';
	case J:
		return 'j';
	case K:
		return 'k';
	case L:
		return 'l';
	case M:
		return 'm';
	case N:
		return 'n';
	case O:
		return 'o';
	case P:
		return 'p';
	case Q:
		return 'q';
	case R:
		return 'r';
	case S:
		return 's';
	case T:
		return 't';
	case U:
		return 'u';
	case V:
		return 'v';
	case W:
		return 'w';
	case X:
		return 'x';
	case Y:
		return 'y';
	case Z:
		return 'z';
	case SPACE:
		return ' ';
	default:
		return '\0';
	}
}