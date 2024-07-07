#include "keyboard.h"
#include "ps2.h"
#include "stdint.h"
#include "lib.h"
#include "term.h"

void keyboard_init()
{
	ps2_data_out(KEYBOARD_ENABLE_SCANNING);
	ps2_data_in(); // flush for ack
				   // TODO: have actual wrapper function that handles checking for ack and resends
}