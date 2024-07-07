#include "keyboard.h"
#include "ps2.h"
#include "stdint.h"
#include "lib.h"
#include "term.h"
#include <stdint.h>

void keyboard_init()
{
	ps2_data_out(KEYBOARD_ENABLE_SCANNING);
	ps2_data_in(); // handle ack
}