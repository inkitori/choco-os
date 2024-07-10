#include "snake.h"
#include "timer.h"
#include "stdint.h"
#include "term.h"
#include "keyboard.h"

static void snake_update()
{
	if (keyboard_get_scan_code() == KEYBOARD_SCAN_CODE_A)
	{
		term_print("Left");
	}
	else if (keyboard_get_scan_code() == KEYBOARD_SCAN_CODE_D)
	{
		term_print("Right");
	}
	else if (keyboard_get_scan_code() == KEYBOARD_SCAN_CODE_W)
	{
		term_print("Up");
	}
	else if (keyboard_get_scan_code() == KEYBOARD_SCAN_CODE_S)
	{
		term_print("Down");
	}
}

void snake_init()
{
	term_clear(TERM_COLOR_BLACK);
	uint64_t start_time = timer_get_ticks();

	while (1)
	{
		if (timer_get_ticks() - start_time >= 10)
		{
			start_time = timer_get_ticks();
			snake_update();
		}
	}
}
